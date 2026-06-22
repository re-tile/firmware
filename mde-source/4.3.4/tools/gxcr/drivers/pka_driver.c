// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors.
//   The software is licensed under the Tilera MDE License.
//
//   However, Licensee may elect to use this file under the terms of the
//   GNU Lesser General Public License version 2.1 as published by the
//   Free Software Foundation and appearing in the file src/COPYING.LIB
//   in the MDE distribution.  Please review the following information to
//   ensure the GNU Lesser General Public License version 2.1 requirements
//   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.



#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <sys/types.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <execinfo.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>
#include <gxio/mica.h>
#include <gxio/pka.h>
#include <arch/cycle.h>
#include <arch/mica_crypto_eng_def.h>
#include <tmc/perf.h>
#include <tmc/mem.h>
#include <gxcr/pka.h>
#include "pka_alloc.h"
#include "pka_driver.h"


#define ENABLE_ASSERT 1

// #define DEBUG_PKA 1

#define ENABLE_TRNG 1

#define MAX_CLOCK_CYCLES     0x01000000
#define NUM_RAND_BYTES       (64 * 1024)
#define NUM_RAND_CACHELINES  (NUM_RAND_BYTES / 64)


#define NOINLINE __attribute__((noinline))
#define INLINE   __attribute__((always_inline))
#define CACHELINE_ALIGNED   __attribute__((aligned(CHIP_L2_LINE_SIZE())))

#define DBG_LOG(fmt, args...)                         \
    fprintf(driver_log, fmt " @%u.%03u secs\n", args, \
            uptime_in_secs, msecs_in_second)

#define DBG_STRING(fmt)                           \
    fprintf(driver_log, fmt " @%u.%03u secs\n",   \
            uptime_in_secs, msecs_in_second)

#define DBG_NEWLINE            fprintf(driver_log, "\n");
#define DBG_LOGFLUSH()         fflush(driver_log);


#ifdef DEBUG_PKA
#define DBG_PRINT(fmt, args...)  printf(fmt, ##args)
#define DBG_FLUSH()              fflush(NULL);
#else
#define DBG_PRINT(fmt, args...)
#define DBG_FLUSH()
#endif


typedef struct
{
    uint8_t  lenA;
    uint8_t  lenB;
    uint16_t ptrA;
    uint16_t ptrB;
    uint16_t ptrC;
    uint16_t ptrD;
    uint16_t ptrE;
} tbn_t;

typedef struct
{
    // Fields relating to the src
    uint32_t base_src_offset;
    uint32_t max_src_offset;

    // Fields relating to the dst
    uint8_t *window_base_ptr;
    uint32_t dst_offset;
    uint32_t max_dst_offset;

    uint8_t  trace_flag;
} operands_alloc_t;

typedef struct  // 24 bytes long.
{
    void    *user_data;
    uint64_t start_cycles;  // cycle count when cmd added to hw ring.
    uint32_t tag;
    uint8_t  client_idx;
    uint8_t  ring;
    uint8_t  trace_flag;
    uint8_t  pad;
} user_data_fifo_entry_t;

typedef struct
{
    uint32_t head;
    uint32_t tail;
    uint32_t serial_num;
    user_data_fifo_entry_t entries[256];
} user_data_fifo_t;



// GX PKA command descriptor.
typedef struct
{
    uint32_t ptr_a;
    uint32_t ptr_b;
    uint32_t ptr_c;
    uint32_t ptr_d;
    uint32_t tag;
    uint32_t ptr_e;

    uint32_t rsvd_0 : 2;
    uint32_t len_a  : 9;
    uint32_t rsvd_1 : 7;
    uint32_t len_b  : 9;
    uint32_t rsvd_2 : 5;

    uint32_t command : 8;
    uint32_t rsvd_3 : 8;
    uint32_t enc_vectors_bitmask : 6;
    uint32_t kdr : 2;
    uint32_t odd_powers : 5;          // shiftCnt for shift ops
    uint32_t driver_status : 2;
    uint32_t linked : 1;
} pka_hw_cmd_desc_t;


// GX PKA result descriptor.
typedef struct
{
    uint32_t ptr_a;
    uint32_t ptr_b;
    uint32_t ptr_c;
    uint32_t ptr_d;
    uint32_t tag;

    uint32_t rsvd_0 : 2;
    uint32_t main_result_msw_offset : 11;
    uint32_t rsvd_1 : 2;
    uint32_t result_is_0 : 1;
    uint32_t rsvd_2 : 2;
    uint32_t ms_offset : 11;
    uint32_t rsvd_3 : 2;
    uint32_t modulo_is_0 : 1;

    uint32_t rsvd_4 : 2;
    uint32_t len_a  : 9;
    uint32_t rsvd_5 : 7;
    uint32_t len_b  : 9;
    uint32_t rsvd_6 : 2;
    uint32_t cmp_res : 3;

    uint32_t command : 8;
    uint32_t result_code : 8;
    uint32_t enc_vectors_bitmask : 6;
    uint32_t kdr : 2;
    uint32_t odd_powers : 5;          // shiftCnt for shift ops
    uint32_t written_zero : 2;
    uint32_t linked : 1;
} pka_hw_res_desc_t;



typedef struct
{
    uint64_t words[8];
} CACHELINE_ALIGNED cacheline_t;

// The following structure implements a intermediate buffer of random numbers
// between the HW and the client random number fifos.  The basic idea is
// to keep this local buffer filled by getting only ONE cacheline at a time
// from the HW (makes for less "bursty" main loop behavior).  The size of
// this buffer is such that it should remain in the L2 or L1 cache.  Then
// client random number refills are serviced out of here (again in smaller
// chunks of an integral number of cachelines).
typedef struct
{
    uint64_t    num_fills;
    uint32_t    head_idx;   // In units of cachelines
    uint32_t    tail_idx;   // In units of cachelines
    uint32_t    size;       // In units of cachelines
    uint32_t    min_cycles;
    uint32_t    avg_cycles;
    uint32_t    max_cycles;
    cacheline_t cachelines[NUM_RAND_CACHELINES];
} random_bytes_buf_t;

// This data structure is kept per client hande and its job is to
// decide how filled to keep the client random number fifos.  It calls this
// amount the fifo_size_goal.  It starts out with a low value (four cachelines),
// and based upon the number of random bytes provided and consumed it
// increases the fifo_size_goal.
typedef struct
{
    uint64_t bytes_provided;
    uint64_t bytes_consumed;
    uint32_t fifo_size_goal;
} client_rand_ctl_t;

// Add a bunch of per mica interesting statistics.
typedef struct
{
    uint64_t reqs_started;
    uint64_t reply_errs;
    uint64_t replies;
    uint64_t operand_bytes;
    uint64_t result_bytes;
    uint64_t total_cycles;  // Used to calculate the avg_cycles.
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint32_t peak_cmd_cnt;
} mica_stats_t;

// Add a bunch of per driver (i.e. per chip) interesting statistics.
typedef struct
{
    uint64_t requests;
    uint64_t reqs_started;
    uint64_t reply_errs;
    uint64_t replies;
    uint64_t operand_bytes;
    uint64_t result_bytes;
    uint64_t overflow_enqueue_cnt;
    uint64_t overflow_dequeue_cnt;
    uint32_t overflow_queue_peak_size;
    uint32_t overflow_enqueue_errs;
    uint32_t num_no_new_reqs_set;
    uint32_t num_no_new_reqs_cleared;
    uint32_t opened_client_idxs;
    uint32_t closed_client_idxs;
    uint32_t closed_by_request;
} total_stats_t;



#define NORMAL_CMD_RING  0
#define FAST_CMD_RING    1
#define PKA_WINDOW_RAM_SIZE  0x10000

#define ROUNDUP(val, align)  (((val) + ((align) - 1)) & (~ ((align) - 1)))
#define MAX(a, b)  (((a) <= (b)) ? (b) : (a))
#define MIN(a, b)  (((a) <= (b)) ? (a) : (b))

#ifdef ENABLE_ASSERT
#define Assert(cond)                                                    \
    ({                                                                  \
        if (! (cond))                                                   \
        {                                                               \
            DBG_LOG("Assertion failed at %s:%u",   __FILE__, __LINE__); \
            DBG_LOGFLUSH();                                             \
            printf ("Assertion failed at %s:%u\n", __FILE__, __LINE__); \
            abort();                                                    \
        }                                                               \
    })
#else
#define Assert(cond)
#endif


// The following two tables are the inverses of each other.
static const uint8_t HW_CMD_CODE_TBL[] =
{
    [PKA_ADD]          = 0x01,
    [PKA_SUBTRACT]     = 0x02,
    [PKA_MULTIPLY]     = 0x04,
    [PKA_DIVIDE]       = 0x05,
    [PKA_MODULO]       = 0x06,
    [PKA_SHIFT_LEFT]   = 0x07,
    [PKA_SHIFT_RIGHT]  = 0x08,
    [PKA_COMPARE]      = 0x09,
    [PKA_MOD_EXP]      = 0x10,
    [PKA_EXP_WITH_CRT] = 0x11,
    [PKA_MOD_INVERT]   = 0x12,
    [PKA_ECC_ADD]      = 0x14,
    [PKA_ECC_MULTIPLY] = 0x15,
    [PKA_ECDSA_GEN]    = 0x20,
    [PKA_ECDSA_VERIFY] = 0x25,
    [PKA_DSA_GEN]      = 0x22,
    [PKA_DSA_VERIFY]   = 0x27
};

static const uint8_t CMD_CODE_TO_OPCODE[] =
{
    [0x01] = PKA_ADD,
    [0x02] = PKA_SUBTRACT,
    [0x04] = PKA_MULTIPLY,
    [0x05] = PKA_DIVIDE,
    [0x06] = PKA_MODULO,
    [0x07] = PKA_SHIFT_LEFT,
    [0x08] = PKA_SHIFT_RIGHT,
    [0x09] = PKA_COMPARE,
    [0x10] = PKA_MOD_EXP,
    [0x11] = PKA_EXP_WITH_CRT,
    [0x12] = PKA_MOD_INVERT,
    [0x14] = PKA_ECC_ADD,
    [0x15] = PKA_ECC_MULTIPLY,
    [0x20] = PKA_ECDSA_GEN,
    [0x21] = PKA_ECDSA_VERIFY,
    [0x22] = PKA_DSA_GEN,
    [0x23] = PKA_DSA_VERIFY,
    [0x25] = PKA_ECDSA_VERIFY,
    [0x27] = PKA_DSA_VERIFY
};

typedef enum { PKA_HW_EMPTY, PKA_HW_BUSY, PKA_HW_FULL } pka_state_t;


static uint32_t                num_of_micas;
static gxio_mica_pka_context_t contexts[NUM_MICA_INSTANCES];
static uint32_t                last_mica_picked;
static pka_state_t             pka_state;

static FILE     *driver_log;
static int       shared_mem_fd;
static uint8_t  *shared_mem_ptr;

static uintptr_t        master_addr;
static master_record_t *master_record;

static user_data_fifo_t user_data_fifos[NUM_MICA_INSTANCES][NUM_MICA_RINGS];

static uint32_t random_test_block[100];

static random_bytes_buf_t random_bytes_buf;

static uint32_t memory_per_client;
static uint32_t operand_mem_per_client;
static uint32_t result_mem_per_client;

static uint32_t pka_hw_size;
static uint32_t mica_sizes[NUM_MICA_INSTANCES];

static total_stats_t total_stats;
static mica_stats_t  mica_stats[NUM_MICA_INSTANCES];

static volatile uint32_t dump_stats;


TMC_QUEUE(overflow_queue, pka_driver_cmd_desc_t, LOG2_OVERFLOW_QUEUE_SIZE,
          (TMC_QUEUE_SINGLE_RECEIVER | TMC_QUEUE_SINGLE_SENDER));

static overflow_queue_t      overflow_queue;
static uint32_t              overflow_queue_size;
static pka_driver_cmd_desc_t overflow_queue_head;

static uint32_t uptime_in_secs;
static uint32_t msecs_in_second;
static uint64_t cycles_per_millisec;

static client_rand_ctl_t client_rand_ctl[MAX_NUM_OF_CLIENTS];

static uint8_t  in_use[MAX_NUM_OF_CLIENTS];
static uint32_t num_in_use;

static uint32_t even_modulus_cnt = 0;

static void init_client_idx (uint32_t client_idx);



static INLINE uint32_t read_mmio_reg(gxio_mica_pka_context_t *context,
                                     uint32_t                 reg_offset)
{
    uint8_t *reg_ptr;

    reg_ptr = (uint8_t *) (context->mmio_regs_base + reg_offset);
    return __gxio_mmio_read(reg_ptr);
}

static INLINE void write_mmio_reg(gxio_mica_pka_context_t *context,
                                  uint32_t                 reg_offset,
                                  uint64_t                 reg_value)
{
    uint8_t *reg_ptr;

    reg_ptr = (uint8_t *) (context->mmio_regs_base + reg_offset);
    __gxio_mmio_write(reg_ptr, reg_value);
}

static boolean_t trng_avail(uint32_t mica_idx)
{
    gxio_mica_pka_context_t *context;
    uint32_t                 status_reg;

    context    = &contexts[mica_idx];
    status_reg = read_mmio_reg(context, MICA_CRYPTO_ENG_TRNG_STATUS);

    if ((status_reg & 0x7F) == 0x01)
        return TRUE;  // Ready with no errors.
    else if ((status_reg & 0x7E) == 0)
        return FALSE;   // Not ready - try again soon.

    // This is the case of an error bit being set.  Currently we always
    // try to clear the error.
    write_mmio_reg(context, MICA_CRYPTO_ENG_TRNG_INTACK, status_reg & 0x7E);
    write_mmio_reg(context, MICA_CRYPTO_ENG_TRNG_ALARMMASK, 0);
    write_mmio_reg(context, MICA_CRYPTO_ENG_TRNG_ALARMSTOP, 0);
    return FALSE;   // Error, but not clear - try again soon.
}

static void busy_delay(void)
{
    uint32_t cnt;

    // Wait for ~300 cycles.
    for (cnt = 1;  cnt < 50;  cnt++)
        cycle_relax();
}

static void wait_until_trng_avail(uint32_t mica_idx)
{
    while (trng_avail(mica_idx) == FALSE)
        busy_delay();
}

static void get_random_bytes(uint32_t  mica_idx,
                             uint32_t *dst_ptr,
                             uint32_t  byte_len)
{
    gxio_mica_pka_context_t *context;
    uint32_t                 idx;
    uint8_t                 *trng_output_reg, *trng_int_ack_reg;

    context          = &contexts[mica_idx];
    trng_output_reg  = (uint8_t *) (context->mmio_regs_base +
                                        MICA_CRYPTO_ENG_TRNG_OUTPUT_0);
    trng_int_ack_reg = (uint8_t *) (context->mmio_regs_base +
                                        MICA_CRYPTO_ENG_TRNG_INTACK);

    for (idx = 0; idx < byte_len / 16; idx++)
    {
        wait_until_trng_avail(mica_idx);
        *dst_ptr++ = __gxio_mmio_read(trng_output_reg + 0);
        *dst_ptr++ = __gxio_mmio_read(trng_output_reg + 8);
        *dst_ptr++ = __gxio_mmio_read(trng_output_reg + 16);
        *dst_ptr++ = __gxio_mmio_read(trng_output_reg + 24);

        __gxio_mmio_write(trng_int_ack_reg, 1);
    }
}

static uint32_t append_to_user_data_fifo(uint32_t  mica_idx,
                                         uint32_t  ring,
                                         void     *user_data,
                                         uint32_t  client_idx,
                                         boolean_t trace_flag)
{
    user_data_fifo_entry_t *fifo_entry;
    user_data_fifo_t       *fifo;
    uint32_t                serial_num, tag;

    fifo = &user_data_fifos[mica_idx][ring];
    fifo->serial_num++;
    serial_num = fifo->serial_num;
    tag        = trace_flag ? ((2 * serial_num) + 1) : (2 * serial_num);

    fifo_entry               = &fifo->entries[fifo->tail];
    fifo_entry->user_data    = user_data;
    fifo_entry->start_cycles = get_cycle_count();
    fifo_entry->tag          = tag;
    fifo_entry->client_idx   = client_idx;
    fifo_entry->ring         = ring;
    fifo_entry->trace_flag   = trace_flag;
    fifo->tail++;
    if (256 <= fifo->tail)
        fifo->tail = 0;

    return tag;
}


// Returns the number of clock cycles taken by this cmd - from the time it was
// added to the cmd queue until the reply is processed by this function.  The
// number of clock cycles is capped at MAX_CLOCK_CYCLES.

static uint32_t remove_from_user_data_fifo(uint32_t   mica_idx,
                                           uint32_t   ring,
                                           uint32_t   tag,
                                           void     **user_data,
                                           uint32_t  *client_idx,
                                           boolean_t *trace_flag)
{
    user_data_fifo_entry_t *fifo_entry;
    user_data_fifo_t       *fifo;
    uint64_t                cycles_taken;

    fifo       = &user_data_fifos[mica_idx][ring];
    fifo_entry = &fifo->entries[fifo->head];
    Assert(fifo_entry->tag == tag);

    cycles_taken = get_cycle_count() - fifo_entry->start_cycles;
    *user_data   = fifo_entry->user_data;
    *client_idx  = fifo_entry->client_idx;
    *trace_flag  = fifo_entry->trace_flag;
    Assert(fifo_entry->trace_flag == (tag & 1));
    fifo->head++;
    if (256 <= fifo->head)
        fifo->head = 0;

    return (uint32_t)  MIN(cycles_taken, MAX_CLOCK_CYCLES);
}



// The function pka_ring_chk_counts checks to see if the PKA HW is properly
// "cleared/initialized".  In particular many crashes have been caused by the
// fact that the HW result_count was not zero, and then the pka_driver gets
// confused and crashes.  This code tries to repair this case - either directly
// or by returning a value which will cause the caller to try a reset.

static status_t pka_ring_chk_counts(gxio_mica_pka_context_t *context,
                                    uint32_t                 num_rings,
                                    boolean_t                try_to_repair)
{
    boolean_t need_reinit;
    uint32_t  ring, cmd_count, result_count;
    uint8_t  *pka_regs_ptr, *cmd_cnt_base, *result_cnt_base;

    pka_regs_ptr    = (uint8_t *) context->mmio_regs_base;
    cmd_cnt_base    = pka_regs_ptr + MICA_CRYPTO_ENG_COMMAND_COUNT_0;
    result_cnt_base = pka_regs_ptr + MICA_CRYPTO_ENG_RESULT_COUNT_0;
    need_reinit     = FALSE;

    for (ring = 0; ring < num_rings; ring++)
    {
        cmd_count    = __gxio_mmio_read(cmd_cnt_base    + (ring * 8));
        result_count = __gxio_mmio_read(result_cnt_base + (ring * 8));
        if ((result_count != 0) && try_to_repair)
        {
            // Decrement result count by the number we read out.
            __gxio_mmio_write(result_cnt_base + (ring * 8), result_count);
            busy_delay();

            // Reread the result count to see if the repair worked.
            result_count = __gxio_mmio_read(result_cnt_base + (ring * 8));
            if (result_count == 0)
                DBG_LOG("Successfully cleared non-zero result_count=%u on "
                        "ring=%u\n", result_count, ring);
            else
                DBG_LOG("Failed to clear non-zero result_count=%u on "
                        "ring=%u\n", result_count, ring);

            DBG_LOGFLUSH();
        }

        if ((cmd_count != 0) || (result_count != 0))
            need_reinit = TRUE;
    }

    return need_reinit ? FAILURE : SUCCESS;
}

static int gxcr_pka_setup_rings(gxio_mica_pka_context_t     *context,
                                gxio_mica_pka_ring_config_t *ring_config)
{
    pka_ring_t *ring;
    uint32_t    num_rings, ring_mem_size, ring_opts, window_ptr, idx;
    uint8_t    *pka_regs_base_addr;

    num_rings = ring_config->num_rings;
    if (num_rings < 1 || num_rings > 4)
    {
        DBG_LOG("Invalid number of rings %d\n", num_rings);
        return GXIO_ERR_INVAL;
    }

    // Partition the window ram with command rings and data elements.
    // Just give each ring an equal portion of the window ram.  We can
    // make this configurable at some point if there's a good reason.
    ring_mem_size = (PKA_WINDOW_RAM_SIZE - 0xE000) / num_rings;

    // Round down to next 8-byte alignment.
    ring_mem_size = (ring_mem_size / 8) * 8;

    window_ptr = 0xE000;  // OPERAND_MEM_SIZE
    for (idx = 0;  idx < num_rings;  idx++)
    {
        ring = &context->ring[idx];
        ring->num_elems = ring_mem_size / (sizeof(pka_hw_cmd_desc_t) +
                                           ring_config->ring_elem_size[idx]);
        ring->elem_size = ring_config->ring_elem_size[idx];
        ring->cmd_base  = window_ptr;
        ring->cmd_head  = window_ptr;
        ring->cmd_tail  = window_ptr;
        ring->data_base = -1;
        ring->data_head = -1;
        ring->data_tail = -1;
        window_ptr     += ring_mem_size;
    }

    pka_regs_base_addr = (uint8_t *) context->mmio_regs_base;

    context->num_rings               = num_rings;
    context->ring_0_is_high_priority = ring_config->ring_0_is_high_priority;

    // Set up hardware registers.
    // __gxio_mmio_write(pka_regs_base_addr +
    //                   MICA_CRYPTO_ENG_PKA_MASTER_SEQ_CTRL, 0);
    __gxio_mmio_write(pka_regs_base_addr + MICA_CRYPTO_ENG_PKA_RING_OPTIONS, 0);

    for (idx = 0;  idx < num_rings;  idx++)
    {
        // These registers are somewhat irregularly spaced in the address
        // space.  We also multiply the offset from the ring 0 register by 2
        // because the registers are 8-byte aligned in the Tilera MMIO mapping.
      __gxio_mmio_write(pka_regs_base_addr +
                        MICA_CRYPTO_ENG_CMMD_RING_BASE_0 + ((idx * 16) << 1),
                        context->ring[idx].cmd_base);
      __gxio_mmio_write(pka_regs_base_addr +
                        MICA_CRYPTO_ENG_RSLT_RING_BASE_0 + ((idx * 16) << 1),
                        context->ring[idx].cmd_base);
      __gxio_mmio_write(pka_regs_base_addr +
                        MICA_CRYPTO_ENG_RING_SIZE_0 + ((idx * 4) << 1),
                        context->ring[idx].num_elems - 1);
      __gxio_mmio_write(pka_regs_base_addr +
                        MICA_CRYPTO_ENG_RING_RW_PTRS_0 + ((idx * 4) << 1), 0);
    }

    // Enable only the rings we are using.  All rings are in order.
    // Priority control is set to either full-rotating or to ring-0-is-highest.
    ring_opts = ((num_rings - 1) << 2) |
                (ring_config->ring_0_is_high_priority ? 2 : 0);
    __gxio_mmio_write(pka_regs_base_addr + MICA_CRYPTO_ENG_PKA_RING_OPTIONS,
                      0x460001f0 | ring_opts);
    DBG_LOG("Ring Options Register = 0x%016" PRIx64,
            (uint64_t) __gxio_mmio_read(pka_regs_base_addr +
                                        MICA_CRYPTO_ENG_PKA_RING_OPTIONS));
    return 0;
}

static void gxcr_pka_reinit(gxio_mica_pka_context_t     *context,
                            gxio_mica_pka_ring_config_t *ring_config)
{
    uint32_t master_seq_ctl_csr;
    uint8_t *pka_regs_ptr, *master_seq_ctl_ptr;

    pka_regs_ptr       = (uint8_t *) context->mmio_regs_base;
    master_seq_ctl_ptr = pka_regs_ptr + MICA_CRYPTO_ENG_PKA_MASTER_SEQ_CTRL;
    master_seq_ctl_csr = __gxio_mmio_read(master_seq_ctl_ptr);
    if (master_seq_ctl_csr != 0)
        __gxio_mmio_write(master_seq_ctl_ptr, 0);

    if (SUCCESS == pka_ring_chk_counts(context, ring_config->num_rings, TRUE))
        return;

    // Set the SW_reset bit and then wait for it to be cleared by the
    // embedded MiCA engine firmware
    DBG_STRING("Starting SW_reset");
    DBG_LOGFLUSH();

    __gxio_mmio_write(master_seq_ctl_ptr, 0x80);
    busy_delay();
    while (1)
    {
        master_seq_ctl_csr = __gxio_mmio_read(master_seq_ctl_ptr);
        if ((master_seq_ctl_csr & 0x80) == 0)
            break;
    }

    DBG_STRING("SW_reset done");
    DBG_LOGFLUSH();
}



static int gxcr_pka_init(gxio_mica_pka_context_t     *context,
                         uint32_t                     mica_index,
                         gxio_mica_pka_ring_config_t *ring_config)
{
    uint8_t *pka_regs_ptr, *master_seq_ctl_ptr, master_seq_csr;
    char     file[32];
    int      fd, rc;

    if (mica_index >= 2) // GXIO_MICA_PKA_INSTANCE_MAX)
        return -EINVAL;

    snprintf(file, sizeof(file), "/dev/iorpc/pka%d", mica_index);
    fd = open(file, O_RDWR);

    if (fd < 0)
    {
        printf("couldn't open %s, err = %s\n", file, strerror(errno));
        return -errno;
    }

    context->fd = fd;

    // Map in the control registers and the data memory in one shot,
    // then assign the base registers to the proper offsets.
    context->mmio_memory_base =
        mmap(NULL, HV_PKA_REGS_MMIO_SIZE + HV_PKA_DATA_MMIO_SIZE,
             PROT_READ | PROT_WRITE, MAP_SHARED | MAP_CACHE_HOME_HERE,
             context->fd, 0);
    if (context->mmio_memory_base == MAP_FAILED)
    {
        printf("mmap failed.  err = %s\n", strerror(errno));
        close(context->fd);
        return -errno;
    }

    context->mmio_regs_base = context->mmio_memory_base;
    context->mmio_data_base = context->mmio_regs_base + HV_PKA_REGS_MMIO_SIZE;

    pka_regs_ptr       = (uint8_t *) context->mmio_regs_base;
    master_seq_ctl_ptr = pka_regs_ptr + MICA_CRYPTO_ENG_PKA_MASTER_SEQ_CTRL;
    master_seq_csr     = __gxio_mmio_read(master_seq_ctl_ptr);
    DBG_LOG("master_seq_csr=0x%X", master_seq_csr);
    DBG_LOGFLUSH();

    if (master_seq_csr == 0)
        gxcr_pka_reinit(context, ring_config);
    else
        __gxio_mmio_write(master_seq_ctl_ptr, 0);

    rc = gxcr_pka_setup_rings(context, ring_config);
    if (rc != 0)
        return rc;

    if (SUCCESS == pka_ring_chk_counts(context, ring_config->num_rings, FALSE))
        return 0;

    gxcr_pka_reinit(context, ring_config);
    rc = gxcr_pka_setup_rings(context, ring_config);
    if (rc != 0)
        return rc;

    if (SUCCESS == pka_ring_chk_counts(context, ring_config->num_rings, TRUE))
        return 0;

    DBG_STRING("gxcr_pka_init failed even after a reinit\n");
    return -1;
}



static void gxcr_pka_clear_window_ram(operands_alloc_t *alloc,
                                      uint32_t          operands_size)
{
    uint64_t *dst64_ptr;
    uint32_t  offset, word_len, idx;

    word_len  = (operands_size + 3) / 4;
    offset    = (alloc->dst_offset + 7) & ~0x7;
    dst64_ptr = (uint64_t *) (alloc->window_base_ptr + offset);
    for (idx = 0;  idx < (word_len + 1) / 2;  idx++)
        __gxio_mmio_write(dst64_ptr++, 0);
}

void dump_window(gxio_mica_pka_context_t *context,
                 uint32_t                 src_offset,
                 uint32_t                 word_len)
{
    uint64_t *src64_ptr;
    uint32_t  idx;

    src64_ptr = (uint64_t *) (context->mmio_data_base + src_offset);
    for (idx = 0;  idx < (word_len + 1) / 2;  idx++)
    {
        printf("dump_window word_idx=%u addr=%p  data=0x%016" PRIX64 "\n",
               idx, src64_ptr, __gxio_mmio_read(src64_ptr));
        src64_ptr++;
    }

    DBG_FLUSH();
}

static void gxcr_pka_copy_to_window(void     *dst,
                                    void     *src,
                                    uint32_t  src_word_len,
                                    uint32_t  dst_word_len,
                                    boolean_t trace)
{
    uint64_t *dst64_ptr, *src64_ptr;
    uint32_t idx, *src32_ptr;

    Assert((((uintptr_t) dst) & 0x7) == 0);
    Assert((((uintptr_t) src) & 0x7) == 0);
    Assert(src_word_len <= dst_word_len);
    if (trace)
        DBG_LOG("\ngxcr_pka_copy_to_window src=%p dst=%p word_len=%u",
                src, dst, dst_word_len);

    dst64_ptr = dst;
    src64_ptr = src;
    for (idx = 0;  idx < src_word_len / 2;  idx++)
    {
        if (trace)
            DBG_LOG("gxcr_pka_copy_to_window idx=%u addr=%p data=0x%016" PRIX64,
                    idx, dst64_ptr, *src64_ptr);

        __gxio_mmio_write(dst64_ptr++, *src64_ptr++);
    }

    if ((src_word_len & 0x1) != 0)
    {
        // Special case when the src_word_len is odd.
        src32_ptr = (uint32_t *) src64_ptr;
        if (trace)
            DBG_LOG("gxcr_pka_copy_to_window idx=%u addr=%p data=0x%016X",
                    idx, dst64_ptr, *src32_ptr);

        __gxio_mmio_write(dst64_ptr++, *src32_ptr++);
        idx++;
    }

    // Now add any leading zero words required.
    for ( ; idx < (dst_word_len + 1) / 2;  idx++)
    {
        if (trace)
            DBG_LOG("gxcr_pka_copy_to_window idx=%u addr=%p data=0x%016X",
                    idx, dst64_ptr, 0);

        __gxio_mmio_write(dst64_ptr++, 0);
    }

    if (trace)
        DBG_LOGFLUSH();
}

static void gxcr_pka_copy_from_window(void     *dst,
                                      void     *src,
                                      uint32_t  word_len,
                                      boolean_t is_hw_result,
                                      boolean_t trace)
{
    pka_hw_res_desc_t *hw_result = NULL;
    boolean_t          trace_hw_result;
    uint64_t          *dst64_ptr, *src64_ptr;
    uint32_t           idx;

    // Current assume/require that the src address be eight byte aligned!
    Assert((((uintptr_t) dst) & 0x7) == 0);
    Assert((((uintptr_t) src) & 0x7) == 0);

    // *TBD* Make a more optimal loop, taking account of the fact that
    // there can only be one mmio read outstanding to a given cacheline.
    // So ideally (if the number of cachelines >=4) we should issue a read
    // to 4 different cachelines and store tham away in that same pattern.
    dst64_ptr = dst;
    src64_ptr = src;
    for (idx = 0;  idx < (word_len + 1) / 2;  idx++)
        *dst64_ptr++ = __gxio_mmio_read(src64_ptr++);

    trace_hw_result = FALSE;
    if (is_hw_result)
    {
        hw_result        = (pka_hw_res_desc_t *) dst;
        trace_hw_result  = (hw_result->tag & 1) != 0;
        trace           |= trace_hw_result;
    }

    if (! trace)
        return;

    if (trace_hw_result)
        DBG_LOG("\nProcessResult cmd=%u result_code=0x%X",
                hw_result->command, hw_result->result_code);

    DBG_LOG("\ngxcr_pka_copy_from_window src=%p dst=%p word_len=%u",
            src, dst, word_len);

    src64_ptr = src;
    for (idx = 0;  idx < (word_len + 1) / 2;  idx++)
    {
        DBG_LOG("gxcr_pka_copy_from_window idx=%u addr=%p data=0x%016" PRIX64,
                idx, src64_ptr, __gxio_mmio_read(src64_ptr));
        src64_ptr++;
    }

    DBG_LOGFLUSH();
}



static INLINE void update_cmd_stats(uint32_t mica_idx, uint32_t operands_size)
{
    mica_stats_t *mica_stat;

    mica_stat = &mica_stats[mica_idx];
    total_stats.reqs_started++;
    total_stats.operand_bytes += operands_size;
    mica_stat->reqs_started++;
    mica_stat->operand_bytes += operands_size;
}

static INLINE void update_result_stats(uint32_t mica_idx,
                                       uint32_t total_results_len,
                                       uint32_t cycles_taken)
{
    mica_stats_t *mica_stat;

    total_stats.replies++;
    total_stats.result_bytes += total_results_len;

    mica_stat = &mica_stats[mica_idx];
    mica_stat->replies++;
    mica_stat->result_bytes += total_results_len;
    mica_stat->min_cycles    = MIN(mica_stat->min_cycles, cycles_taken);
    mica_stat->max_cycles    = MAX(mica_stat->max_cycles, cycles_taken);
    mica_stat->total_cycles += (uint64_t) cycles_taken;
}

static int gxcr_pka_cmd_slots_avail(gxio_mica_pka_context_t *context,
                                    uint32_t                 ring)
{
    uint32_t num_elems, cmd_slots_in_use;

    num_elems        = context->ring[ring].num_elems;
    cmd_slots_in_use = context->ring[ring].cmd_slots_in_use;
    return num_elems - cmd_slots_in_use;
}

static uint32_t gxcr_pka_results_avail(gxio_mica_pka_context_t *context,
                                       uint32_t                 ring)
{
    uint32_t result_reg, result_cnt;

    result_reg = MICA_CRYPTO_ENG_RESULT_COUNT_0 + (ring * 8);
    result_cnt = __gxio_mmio_read(context->mmio_regs_base + result_reg);
    return result_cnt;
}



static int gxcr_pka_append_cmd(uint32_t               mica_idx,
                               uint32_t               ring,
                               pka_driver_cmd_desc_t *cmd_desc,
                               tbn_t                 *tbn)
{
    gxio_mica_pka_context_t *context;
    pka_hw_cmd_desc_t        hw_cmd_desc;
    mica_stats_t            *mica_stat;
    pka_ring_t              *hw_ring_desc;
    uint32_t                 num_elems, cmd_slots_in_use, odd_powers;
    uint32_t                 cmd_reg, tag;
    uint8_t                 *cmd_tail_ptr;

    context          = &contexts[mica_idx];
    hw_ring_desc     = &context->ring[ring];
    num_elems        = hw_ring_desc->num_elems;
    cmd_slots_in_use = hw_ring_desc->cmd_slots_in_use;
    memset(&hw_cmd_desc, 0, sizeof(hw_cmd_desc));
    if (num_elems == cmd_slots_in_use)
        return GXIO_MICA_ERR_PKA_CMD_QUEUE_FULL;

    switch (cmd_desc->opcode)
    {
    case PKA_SHIFT_LEFT:
    case PKA_SHIFT_RIGHT:
        odd_powers = cmd_desc->shift_cnt;
        break;

    case PKA_MOD_EXP:
        odd_powers = (tbn->lenA <= 1) ? 1 : 4;
        break;

    case PKA_EXP_WITH_CRT:
        odd_powers = 4;
        break;

    case PKA_DSA_GEN :
    case PKA_DSA_VERIFY :
        odd_powers = 4;  // *TBD* when should we use 4?  Depends on k length?
        break;

    default :
        odd_powers = 0;
    }

    tag = append_to_user_data_fifo(mica_idx,ring, cmd_desc->user_data,
                                   cmd_desc->client_idx, cmd_desc->trace_flag);

    hw_cmd_desc.command             = HW_CMD_CODE_TBL[cmd_desc->opcode];
    hw_cmd_desc.len_a               = tbn->lenA;
    hw_cmd_desc.len_b               = tbn->lenB;
    hw_cmd_desc.tag                 = tag;
    hw_cmd_desc.enc_vectors_bitmask = 0;
    hw_cmd_desc.kdr                 = 0;
    hw_cmd_desc.odd_powers          = odd_powers;
    hw_cmd_desc.ptr_a               = (uint32_t) tbn->ptrA;
    hw_cmd_desc.ptr_b               = (uint32_t) tbn->ptrB;
    hw_cmd_desc.ptr_c               = (uint32_t) tbn->ptrC;
    hw_cmd_desc.ptr_d               = (uint32_t) tbn->ptrD;
    hw_cmd_desc.ptr_e               = (uint32_t) tbn->ptrE;

    __insn_mf();

    hw_ring_desc->cmd_slots_in_use++;

    cmd_tail_ptr = (uint8_t *) context->mmio_data_base + hw_ring_desc->cmd_tail;
    if (cmd_desc->trace_flag)
    {
        DBG_LOG("\nAppendCmd cmd=%u", hw_cmd_desc.command);
        DBG_LOGFLUSH();
    }

    gxcr_pka_copy_to_window(cmd_tail_ptr, &hw_cmd_desc,
                            (sizeof(hw_cmd_desc) + 3)/4,
                            (sizeof(hw_cmd_desc) + 3)/4, cmd_desc->trace_flag);
    __insn_mf();

    // Increment command count
    cmd_reg = MICA_CRYPTO_ENG_COMMAND_COUNT_0 + (ring * 8);
    __gxio_mmio_write(context->mmio_regs_base + cmd_reg, 1);

    // Update some counters
    mica_sizes[mica_idx]++;
    pka_hw_size++;
    mica_stat               = &mica_stats[mica_idx];
    mica_stat->peak_cmd_cnt = MAX(mica_stat->peak_cmd_cnt,
                                  mica_sizes[mica_idx]);

    if (((hw_ring_desc->cmd_tail - hw_ring_desc->cmd_base) >=
         (sizeof(pka_hw_cmd_desc_t) * (num_elems - 1))))
        hw_ring_desc->cmd_tail = hw_ring_desc->cmd_base;
    else
        hw_ring_desc->cmd_tail += sizeof(pka_hw_cmd_desc_t);

    return 0;
}



static uint8_t operand_len(uint32_t byte_len, uint32_t max_len)
{
    return MIN((byte_len + 3)/4, max_len);
}

static uint8_t concat_len(uint32_t word_len,
                          uint32_t odd_skip,
                          uint32_t even_skip,
                          uint32_t pad_len)
{
    uint32_t skip_len, total_len;

    skip_len  = ((word_len & 0x1) == 1) ? odd_skip : even_skip;
    total_len = (2 * word_len) + skip_len;
    if (pad_len != 0)
    {
        total_len += pad_len;
        if ((total_len & 1) != 0)
            total_len++;
    }

    return ROUNDUP(total_len, 2);
}

static uint8_t concat3_len(uint32_t word_len1,
                           uint8_t  word_len2,
                           uint8_t  word_len3,
                           uint32_t odd_skip,
                           uint32_t even_skip)
{
    uint32_t skip_len1, skip_len2, len;

    skip_len1 = ((word_len1 & 0x1) == 1) ? odd_skip : even_skip;
    skip_len2 = ((word_len2 & 0x1) == 1) ? odd_skip : even_skip;
    len       = word_len1 + skip_len1 + word_len2 + skip_len2 + word_len3;
    return ROUNDUP(len, 2);
}

static uint8_t concat6_len(uint32_t word_len,
                           uint32_t odd_skip,
                           uint32_t even_skip)
{
    uint32_t skip_len = ((word_len & 0x1) == 1) ? odd_skip : even_skip;

    return ROUNDUP((6 * word_len) + (5 * skip_len), 2);
}

static uint8_t get_operand_byte(len_offset_t *operand, uint32_t index)
{
    return * (uint8_t *) (master_addr + operand->offset + index);
}

static void set_operand_byte(len_offset_t *operand,
                             uint32_t      index,
                             uint8_t       byte)
{
    * (uint8_t *) (master_addr + operand->offset + index) = byte;
}

static void even_modulus_chk(pka_opcode_t opcode, len_offset_t *operand)
{
    uint8_t lsbByte;

    // If modulus (operand) is even, print msg and jam it odd to prevent
    // lockup!
    lsbByte = get_operand_byte(operand, 0);
    if ((lsbByte & 0x1) == 0)
    {
        even_modulus_cnt++;
        set_operand_byte(operand, 0, lsbByte | 1);
        if (even_modulus_cnt < 10)
        {
            DBG_LOG("WARNING rcvd even modulus opcode=%u lsbytes=0x%X 0x%X",
                   opcode, lsbByte, get_operand_byte(operand, 1));
            DBG_LOGFLUSH();
        }
    }
}

static void copy_to_window(operands_alloc_t *alloc,
                           len_offset_t     *src_operand,
                           uint32_t          word_len,
                           uint32_t          pad_len)
{
    uint32_t copy_len, remain_len, src_word_len;
    uint8_t *src_ptr, *dst_ptr, src_operand_buffer[MAX_BYTE_LEN];

    // First see if the operand wraps.
    src_ptr = (uint8_t *) (master_addr + src_operand->offset);
    if (src_operand->wraps != 0)
    {
        // If so then first memcpy to a contiguous buffer on the stack and then
        // use gxio_mica_pka_copy_to_window.  This happens so rarely (< 1%),
        // that we don't bother optimizing it.
        if (alloc->trace_flag)
            DBG_LOG("copy_to_window src_operand wraps src_ptr=%p",
                    src_ptr);

        memset(&src_operand_buffer[0], 0, src_operand->byte_len + 7);
        copy_len = alloc->max_src_offset - src_operand->offset;
        memcpy(&src_operand_buffer[0], src_ptr, copy_len);

        src_ptr = (uint8_t *) (master_addr + alloc->base_src_offset);
        remain_len = src_operand->byte_len - copy_len;
        memcpy(&src_operand_buffer[copy_len], src_ptr, remain_len);
        src_ptr = &src_operand_buffer[0];
    }

    // Now load the operand into the 64KB window ram.
    Assert((alloc->dst_offset & 0x7) == 0);
    dst_ptr      = alloc->window_base_ptr + alloc->dst_offset;
    src_word_len = ROUNDUP(src_operand->byte_len, 4) / 4;
    gxcr_pka_copy_to_window(dst_ptr, src_ptr, src_word_len, word_len,
                            alloc->trace_flag);
    alloc->dst_offset += 4 * (word_len + pad_len);
    if ((alloc->dst_offset & 0x7) != 0)
        alloc->dst_offset = ROUNDUP(alloc->dst_offset, 8);

    Assert(alloc->dst_offset <= alloc->max_dst_offset);
}

static uint16_t copy_operand(operands_alloc_t *alloc,
                             len_offset_t     *operand,
                             uint8_t           word_len,
                             uint8_t           pad_len)
{
    uint32_t start_dst_offset;

    Assert((alloc->dst_offset & 0x7) == 0);
    start_dst_offset = alloc->dst_offset;

    copy_to_window(alloc, operand, word_len, pad_len);
    return start_dst_offset;
}

static uint16_t concat(operands_alloc_t *alloc,
                       len_offset_t     *operand1,
                       len_offset_t     *operand2,
                       uint8_t           word_len,
                       uint32_t          odd_skip,
                       uint32_t          even_skip,
                       uint32_t          pad_len)
{
    uint32_t start_dst_offset, skip_len;

    // Either skip 0, 1, 2 or 3 words
    Assert((alloc->dst_offset & 0x7) == 0);
    skip_len         = ((word_len & 0x1) == 1) ? odd_skip : even_skip;
    start_dst_offset = alloc->dst_offset;

    copy_to_window(alloc, operand1, word_len, skip_len);
    copy_to_window(alloc, operand2, word_len, pad_len);
    return start_dst_offset;
}

static uint16_t concat3(operands_alloc_t *alloc,
                        len_offset_t     *operand1,
                        len_offset_t     *operand2,
                        len_offset_t     *operand3,
                        uint8_t           word_len1,
                        uint8_t           word_len2,
                        uint8_t           word_len3,
                        uint32_t          odd_skip,
                        uint32_t          even_skip)
{
    uint32_t start_dst_offset, skip_len1, skip_len2;

    // Either skip 0, 1, 2 or 3 words
    Assert((alloc->dst_offset & 0x7) == 0);
    skip_len1        = ((word_len1 & 0x1) == 1) ? odd_skip : even_skip;
    skip_len2        = ((word_len2 & 0x1) == 1) ? odd_skip : even_skip;
    start_dst_offset = alloc->dst_offset;

    copy_to_window(alloc, operand1, word_len1, skip_len1);
    copy_to_window(alloc, operand2, word_len2, skip_len2);
    copy_to_window(alloc, operand3, word_len3, 0);
    return start_dst_offset;
}

static uint16_t concat6(operands_alloc_t *alloc,
                        len_offset_t     *operand1,
                        len_offset_t     *operand2,
                        len_offset_t     *operand3,
                        len_offset_t     *operand4,
                        len_offset_t     *operand5,
                        len_offset_t     *operand6,
                        uint8_t           word_len,
                        uint32_t          odd_skip,
                        uint32_t          even_skip)
{
    uint32_t start_dst_offset, skip_len;

    // Either skip 0, 1, 2 or 3 words
    Assert((alloc->dst_offset & 0x7) == 0);
    skip_len         = ((word_len & 0x1) == 1) ? odd_skip : even_skip;
    start_dst_offset = alloc->dst_offset;

    copy_to_window(alloc, operand1, word_len, skip_len);
    copy_to_window(alloc, operand2, word_len, skip_len);
    copy_to_window(alloc, operand3, word_len, skip_len);
    copy_to_window(alloc, operand4, word_len, skip_len);
    copy_to_window(alloc, operand5, word_len, skip_len);
    copy_to_window(alloc, operand6, word_len, 0);
    return start_dst_offset;
}

static uint16_t result_ptr(operands_alloc_t *alloc, uint8_t word_len)
{
    uint32_t start_dst_offset;

    Assert((alloc->dst_offset & 0x7) == 0);
    start_dst_offset  = alloc->dst_offset;
    alloc->dst_offset = ROUNDUP(start_dst_offset + (4 * word_len), 8);
    return start_dst_offset;
}



static void copy_operands(tbn_t            *cmd,
                          operands_alloc_t *alloc,
                          pka_opcode_t      opcode,
                          uint32_t          operand_cnt,
                          uint32_t          shift_cnt,
                          len_offset_t      operands[])
{
    uint32_t lenA, lenB, lenC, max_len, exp_len;
    uint32_t len_pointA, len_pointB, result_len;

    memset (cmd, 0, sizeof(tbn_t));

    switch (opcode)
    {
    case PKA_ADD :
        // operands[0] is value, operands[1] is addend.
        Assert(operand_cnt == 2);
        cmd->lenA = operand_len(operands[0].byte_len, 130);
        cmd->lenB = operand_len(operands[1].byte_len, 130);
        cmd->ptrA = copy_operand(alloc, &operands[0], cmd->lenA, 0);
        cmd->ptrB = copy_operand(alloc, &operands[1], cmd->lenB, 0);
        cmd->ptrC = result_ptr(alloc, MAX(cmd->lenA, cmd->lenB) + 1);
        break;

    case PKA_SUBTRACT:
        // operands[0] is value, operands[1] is subtrahend.
        Assert(operand_cnt == 2);
        cmd->lenA = operand_len(operands[0].byte_len, 130);
        cmd->lenB = operand_len(operands[1].byte_len, 130);
        cmd->ptrA = copy_operand(alloc, &operands[0], cmd->lenA, 0);
        cmd->ptrB = copy_operand(alloc, &operands[1], cmd->lenB, 0);
        cmd->ptrC = result_ptr(alloc, MAX(cmd->lenA, cmd->lenB));
        break;

    case PKA_MULTIPLY:
        // operands[0] is value, operands[1] is multiplier.
        Assert(operand_cnt == 2);
        cmd->lenA = operand_len(operands[0].byte_len, 130);
        cmd->lenB = operand_len(operands[1].byte_len, 130);
        cmd->ptrA = copy_operand(alloc, &operands[0], cmd->lenA, 0);
        cmd->ptrB = copy_operand(alloc, &operands[1], cmd->lenB, 0);
        cmd->ptrC = result_ptr(alloc, cmd->lenA + cmd->lenB + 6);
        break;

    case PKA_DIVIDE:
        // operands[0] is value, operands[1] is divisor.
        Assert(operand_cnt == 2);
        cmd->lenA = operand_len(operands[0].byte_len, 130);
        cmd->lenB = operand_len(operands[1].byte_len, 130);
        cmd->ptrA = copy_operand(alloc, &operands[0], cmd->lenA, 0);
        cmd->ptrB = copy_operand(alloc, &operands[1], cmd->lenB, 0);
        cmd->ptrC = result_ptr(alloc, cmd->lenB + 1);
        cmd->ptrD = result_ptr(alloc, (cmd->lenA - cmd->lenB) + 1);
        break;

    case PKA_MODULO:
        // operands[0] is value, operands[1] is modulus.
        Assert(operand_cnt == 2);
        Assert(5 <= operands[1].byte_len);
        cmd->lenA = operand_len(operands[0].byte_len, 130);
        cmd->lenB = operand_len(operands[1].byte_len, 130);
        cmd->ptrA = copy_operand(alloc, &operands[0], cmd->lenA, 0);
        cmd->ptrB = copy_operand(alloc, &operands[1], cmd->lenB, 0);
        cmd->ptrC = result_ptr(alloc, cmd->lenB + 1);
        break;

    case PKA_SHIFT_LEFT:
        // operands[0] is value to be shifted.
        Assert(operand_cnt == 1);
        cmd->lenA  = operand_len(operands[0].byte_len, 130);
        result_len = (shift_cnt == 0) ? cmd->lenA : (cmd->lenA + 1);
        cmd->ptrA  = copy_operand(alloc, &operands[0], cmd->lenA, 0);
        cmd->ptrC  = result_ptr(alloc, result_len);
        break;

    case PKA_SHIFT_RIGHT :
        // operands[0] is value to be shifted.
        Assert(operand_cnt == 1);
        cmd->lenA = operand_len(operands[0].byte_len, 130);
        cmd->ptrA = copy_operand(alloc, &operands[0], cmd->lenA, 0);
        cmd->ptrC = result_ptr(alloc, cmd->lenA);
        break;

    case PKA_COMPARE:
        // operands[0] is value, operands[1] is comparend.
        Assert(operand_cnt == 2);
        max_len = MAX(operands[0].byte_len, operands[1].byte_len);
        lenA    = operand_len(max_len, 130);

        cmd->lenA = lenA;
        cmd->lenB = 0;
        cmd->ptrA = copy_operand(alloc, &operands[0], lenA, 0);
        cmd->ptrB = copy_operand(alloc, &operands[1], lenA, 0);
        break;

    case PKA_MOD_EXP :
        // operands[0] is exponent, operands[1] is modulus,
        // operands[2] is message value.
        Assert(operand_cnt == 3);
        Assert(operands[2].byte_len <= operands[1].byte_len);
        even_modulus_chk(opcode, &operands[1]);
        lenA      = operand_len(operands[0].byte_len, 130);
        lenB      = operand_len(operands[1].byte_len, 130);
        lenC      = operand_len(operands[2].byte_len, 130);
        cmd->lenA = lenA;
        cmd->lenB = lenB;
        cmd->ptrA = copy_operand(alloc, &operands[0], lenA, 0);
        cmd->ptrB = copy_operand(alloc, &operands[1], lenB, 1);
        cmd->ptrC = copy_operand(alloc, &operands[2], lenC, (lenB - lenC) + 1);
        cmd->ptrD = result_ptr(alloc, cmd->lenB + 1);
        break;

    case PKA_EXP_WITH_CRT :
        // operands[0] is prime p, operands[1] is prime q,
        // operands[2] is input c, operands[3] is d_p,
        // operands[4] is d_q,     operands[5] is qinv.
        // Note that d_p = d mod (p-1) and d_q = d mod (q-1), where d is the
        // decrypt exponent/secret key, and ((q * qinv) mod p) = 1.
        // Also note that q MUST be less than p, and so lenB is based only on
        // on the length of operands[0]=p.
        Assert(operand_cnt == 6);
        Assert(operands[1].byte_len <= operands[0].byte_len);
        Assert(operands[5].byte_len <= operands[0].byte_len);
        even_modulus_chk(opcode, &operands[0]);
        even_modulus_chk(opcode, &operands[1]);
        exp_len   = MAX(operands[3].byte_len, operands[4].byte_len);
        cmd->lenA = operand_len(exp_len, 66);
        cmd->lenB = operand_len(operands[0].byte_len, 66);
        cmd->ptrA = concat(alloc, &operands[3], &operands[4], cmd->lenA,
                           1, 0, 0);
        cmd->ptrB = concat(alloc, &operands[0], &operands[1], cmd->lenB,
                           1, 2, 1);
        cmd->ptrC = copy_operand(alloc, &operands[5], cmd->lenB, 0);
        cmd->ptrE = copy_operand(alloc, &operands[2], 2 * cmd->lenB, 0);
        cmd->ptrD = result_ptr(alloc, 2 * cmd->lenB);
        break;

    case PKA_MOD_INVERT :
        // operands[0] is value to be inverted, operands[1] is modulus.
        Assert(operand_cnt == 2);
        even_modulus_chk(opcode, &operands[1]);
        cmd->lenA = operand_len(operands[0].byte_len, 130);
        cmd->lenB = operand_len(operands[1].byte_len, 130);
        cmd->ptrA = copy_operand(alloc, &operands[0], cmd->lenA, 0);
        cmd->ptrB = copy_operand(alloc, &operands[1], cmd->lenB, 0);
        cmd->ptrD = result_ptr(alloc, cmd->lenB);
        break;

    case PKA_ECC_ADD :
        // operands[0] is pointA x,      operands[1] is pointA y,
        // operands[2] is pointB x,      operands[3] is pointB y,
        // operands[4] is curve prime p, operands[5] is curve param a,
        // operands[6] is curve param b.
        Assert(operand_cnt == 7);
        even_modulus_chk(opcode, &operands[4]);

        // Note that operand[6] == b is currently not used!?!
        len_pointA = MAX(operands[0].byte_len, operands[1].byte_len);
        len_pointB = MAX(operands[2].byte_len, operands[3].byte_len);
        lenB       = operand_len(MAX(len_pointA, len_pointB), 24);

        cmd->lenA = 0;
        cmd->lenB = lenB;
        cmd->ptrA = concat(alloc, &operands[0], &operands[1], lenB, 3, 2, 0);
        cmd->ptrB = concat(alloc, &operands[4], &operands[5], lenB, 3, 2, 0);
        cmd->ptrC = concat(alloc, &operands[2], &operands[3], lenB, 3, 2, 0);
        cmd->ptrD = result_ptr(alloc, (2 * lenB) + 3);
        break;

    case PKA_ECC_MULTIPLY :
        // operands[0] is multiplier,    operands[1] is pointA x,
        // operands[2] is pointA y,      operands[3] is curve prime p,
        // operands[4] is curve param a, operands[5] is curve param b.
        Assert(operand_cnt == 6);
        even_modulus_chk(opcode, &operands[3]);
        len_pointA = MAX(operands[1].byte_len, operands[2].byte_len);
        lenA       = operand_len(operands[0].byte_len, 24);
        lenB       = operand_len(len_pointA, 24);

        cmd->lenA = lenA;
        cmd->lenB = lenB;
        cmd->ptrA = copy_operand(alloc, &operands[0], lenA, 0);
        cmd->ptrB = concat3(alloc, &operands[3], &operands[4], &operands[5],
                            lenB, lenB, lenB, 3, 2);
        cmd->ptrC = concat(alloc, &operands[1], &operands[2], lenB, 3, 2, 0);
        cmd->ptrD = result_ptr(alloc, (2 * lenB) + 3);
        break;

    case PKA_ECDSA_GEN :
        // operands[0] is base point x,        operands[1] is base point y,
        // operands[2] is secret k,            operands[3] is private key,
        // operands[4] is message digest hash, operands[5] is curve prime p,
        // operands[6] is curve param a,       operands[7] is curve param b,
        // operands[8] is base point order.
        Assert(operand_cnt == 9);
        Assert(operands[0].byte_len <= operands[5].byte_len);
        Assert(operands[1].byte_len <= operands[5].byte_len);
        Assert(operands[2].byte_len <= operands[8].byte_len);
        Assert(operands[3].byte_len <= operands[8].byte_len);
        Assert(operands[4].byte_len <= operands[8].byte_len);
        Assert(operands[6].byte_len <= operands[5].byte_len);
        Assert(operands[7].byte_len <= operands[5].byte_len);
        Assert(operands[8].byte_len <= operands[5].byte_len);
        even_modulus_chk(opcode, &operands[5]);
        lenB = operand_len(operands[5].byte_len, 24);

        cmd->lenA = 0;
        cmd->lenB = lenB;
        cmd->ptrA = copy_operand(alloc, &operands[3], lenB, 0);
        cmd->ptrB = concat6(alloc, &operands[5], &operands[6], &operands[7],
                            &operands[8], &operands[0], &operands[1],
                            lenB, 3, 2);
        cmd->ptrC = copy_operand(alloc, &operands[4], lenB, 0);
        cmd->ptrE = copy_operand(alloc, &operands[2], lenB, 0);
        cmd->ptrD = result_ptr(alloc, (2 * lenB) + 3);
        break;

    case PKA_ECDSA_VERIFY :
        // operands[0]  is base point x,        operands[1] is base point y,
        // operands[2]  is public key x,        operands[3] is public key y,
        // operands[4]  is message digest hash, operands[5] is curve prime p,
        // operands[6]  is curve param a,       operands[7] is curve param b,
        // operands[8]  is base point order,    operands[9] is signature r,
        // operands[10] is signature s.
        Assert(operand_cnt == 11);
        Assert(operands[0].byte_len  <= operands[5].byte_len);
        Assert(operands[1].byte_len  <= operands[5].byte_len);
        Assert(operands[2].byte_len  <= operands[5].byte_len);
        Assert(operands[3].byte_len  <= operands[5].byte_len);
        Assert(operands[4].byte_len  <= operands[8].byte_len);
        Assert(operands[6].byte_len  <= operands[5].byte_len);
        Assert(operands[7].byte_len  <= operands[5].byte_len);
        Assert(operands[8].byte_len  <= operands[5].byte_len);
        Assert(operands[9].byte_len  <= operands[8].byte_len);
        Assert(operands[10].byte_len <= operands[8].byte_len);
        even_modulus_chk(opcode, &operands[5]);
        lenB = operand_len(operands[5].byte_len, 24);

        cmd->lenA = 0;
        cmd->lenB = lenB;
        cmd->ptrA = concat(alloc, &operands[2], &operands[3], lenB, 3, 2, 0);
        cmd->ptrB = concat6(alloc, &operands[5], &operands[6], &operands[7],
                            &operands[8], &operands[0], &operands[1],
                            lenB, 3, 2);
        cmd->ptrC = copy_operand(alloc, &operands[4], lenB, 0);
        cmd->ptrE = concat(alloc, &operands[9], &operands[10], lenB, 3, 2, 0);
        break;

    case PKA_DSA_GEN :
        // operands[0] is prime p,     operands[1] is generator g,
        // operands[2] is sub-prime q, operands[3] is message digest hash,
        // operands[4] is secret k,    operands[5] is private key.
        Assert(operand_cnt == 6);
        Assert(operands[1].byte_len <= operands[0].byte_len);
        Assert(operands[3].byte_len <= operands[2].byte_len);
        Assert(operands[4].byte_len <= operands[2].byte_len);
        Assert(operands[5].byte_len <= operands[2].byte_len);
        even_modulus_chk(opcode, &operands[0]);
        lenA = operand_len(operands[0].byte_len, 130);
        lenB = operand_len(operands[2].byte_len, lenA - 1);

        cmd->lenA = lenA;
        cmd->lenB = lenB;
        cmd->ptrA = copy_operand(alloc, &operands[5], lenB, 0);
        cmd->ptrB = concat3(alloc, &operands[0], &operands[1], &operands[2],
                            lenA, lenA, lenB, 3, 2);
        cmd->ptrC = copy_operand(alloc, &operands[3], lenB, 0);
        cmd->ptrE = copy_operand(alloc, &operands[4], lenB, 0);
        cmd->ptrD = result_ptr(alloc, (2 * lenB) + 3);
        break;

    case PKA_DSA_VERIFY :
        // operands[0] is prime p,     operands[1] is generator g,
        // operands[2] is sub-prime q, operands[3] is message digest hash,
        // operands[4] is public key,  operands[5] is signature r,
        // operands[6] is signature s.
        Assert(operand_cnt == 7);
        Assert(operands[1].byte_len <= operands[0].byte_len);
        Assert(operands[4].byte_len <= operands[0].byte_len);
        Assert(operands[3].byte_len <= operands[2].byte_len);
        Assert(operands[5].byte_len <= operands[2].byte_len);
        Assert(operands[6].byte_len <= operands[2].byte_len);
        even_modulus_chk(opcode, &operands[0]);
        lenA = operand_len(operands[0].byte_len, 130);
        lenB = operand_len(operands[2].byte_len, lenA - 1);

        cmd->lenA = lenA;
        cmd->lenB = lenB;
        cmd->ptrA = copy_operand(alloc, &operands[4], lenA, 0);
        cmd->ptrB = concat3(alloc, &operands[0], &operands[1], &operands[2],
                            lenA, lenA, lenB, 3, 2);
        cmd->ptrC = copy_operand(alloc, &operands[3], lenB, 0);
        cmd->ptrE = concat(alloc, &operands[5], &operands[6], lenB, 3, 2, 0);
        break;

    default:
        Assert(FALSE);
    }
}



static uint32_t add_to_results_fifo(gxio_mica_pka_context_t *context,
                                    driver_results_desc_t   *results_fifo,
                                    uint32_t                 src_offset,
                                    uint32_t                 byte_len,
                                    boolean_t                trace_flag)
{
    uint32_t tail_offset, dst_offset, rounded_len, copy_len, extra_bytes;
    uint8_t *dst_ptr, *src_ptr;

    // Always preserve 8-byte alignment of the operands in the fifo, by making
    // sure tail_offset is a multiple of 8, and by doing a final memset of
    // "rounded_len - byte_len" bytes.
    src_ptr     = (uint8_t *) (context->mmio_data_base + src_offset);
    tail_offset = results_fifo->tail_offset;
    dst_offset  = results_fifo->base_offset + tail_offset;
    dst_ptr     = (uint8_t *) (master_addr + dst_offset);
    rounded_len = ROUNDUP(byte_len, 8);
    Assert((tail_offset & 0x7) == 0);
    Assert(tail_offset < results_fifo->max_size);

    // Do we need to split into two copy_from_window calls?
    if (results_fifo->max_size < (tail_offset + rounded_len))
    {
        // First gxcr_pka_copy_from_window call.  Note that copy_len MUST
        // be a multiple of 8 here!
        copy_len = results_fifo->max_size - tail_offset;
        if (trace_flag)
            DBG_LOG("add_to_results_fifo First len=%u copy_len=%u",
                    rounded_len, copy_len);
        gxcr_pka_copy_from_window(dst_ptr, src_ptr, copy_len / 4,
                                  FALSE, trace_flag);
        tail_offset = 0;
        dst_ptr     = (uint8_t *) (master_addr + results_fifo->base_offset);
        src_ptr    += copy_len;
        copy_len    = rounded_len - copy_len;
    }
    else
        copy_len = rounded_len;

    // Note that copy_len MUST be a multiple of 8 here!
    gxcr_pka_copy_from_window(dst_ptr, src_ptr, copy_len / 4,
                              FALSE, trace_flag);
    dst_ptr     += copy_len;
    tail_offset += copy_len;

    // If we copied too much (i.e. if rounded_len != byte_len), then memset
    // the extra bytes to zero, just in case.
    if (rounded_len != byte_len)
    {
        extra_bytes = rounded_len - byte_len;
        dst_ptr    -= extra_bytes;
        memset(dst_ptr, 0, extra_bytes);
    }

    if (tail_offset == results_fifo->max_size)
        tail_offset = 0;

    results_fifo->tail_offset = tail_offset;
    if (trace_flag)
        DBG_LOGFLUSH();
    return dst_offset;
}



static uint32_t copy_results(gxio_mica_pka_context_t  *context,
                             pka_hw_res_desc_t        *hw_result,
                             pka_opcode_t              opcode,
                             driver_results_desc_t    *results_fifo,
                             pka_driver_result_desc_t *results_desc,
                             boolean_t                 trace_flag)
{
    uint32_t result_bit_len, result_byte_len, result_off, offset;
    uint32_t total_len, s_offset, skip_byte_len;

    results_desc->opcode         = opcode;
    results_desc->result_cnt     = 1;  // default value. Sometimes overriden.
    results_desc->status         = hw_result->result_code;
    results_desc->compare_result = hw_result->cmp_res;

    result_bit_len  = (hw_result->main_result_msw_offset * 32) +
                     ((hw_result->ms_offset & 0x1F) + 1);
    result_byte_len = (result_bit_len + 7) / 8;
    result_off      = hw_result->ptr_c;  // default value. Often overriden.

    switch (opcode)
    {
    case PKA_ADD:
    case PKA_SUBTRACT:
    case PKA_MULTIPLY:
    case PKA_SHIFT_LEFT:
    case PKA_SHIFT_RIGHT:
        break;  // result is pointed to by ptrC

    case PKA_DIVIDE:
        result_byte_len = 4 * hw_result->len_b;
        offset          = add_to_results_fifo(context, results_fifo,
                                              hw_result->ptr_c,
                                              result_byte_len, trace_flag);
        results_desc->result_cnt         = 2;
        results_desc->result[0].offset   = offset;
        results_desc->result[0].byte_len = result_byte_len;
        total_len                        = ROUNDUP(result_byte_len, 8);

        result_byte_len = 4 * ((hw_result->len_a - hw_result->len_b) + 1);
        offset          = add_to_results_fifo(context, results_fifo,
                                              hw_result->ptr_d,
                                              result_byte_len, trace_flag);
        results_desc->result[1].offset   = offset;
        results_desc->result[1].byte_len = result_byte_len;
        total_len                       += result_byte_len;
        return ROUNDUP(total_len, 8);

    case PKA_MODULO:
        result_bit_len  = (hw_result->ms_offset + 1) * 32;
        result_byte_len = (result_bit_len + 7) / 8;
        break;

    case PKA_COMPARE:
    case PKA_ECDSA_VERIFY:
    case PKA_DSA_VERIFY:
        result_byte_len          = 0;
        results_desc->result_cnt = 0;
        return 0;

    case PKA_MOD_EXP:
    case PKA_EXP_WITH_CRT:
    case PKA_MOD_INVERT:
        result_off = hw_result->ptr_d;
        break;

    case PKA_ECC_ADD:
    case PKA_ECC_MULTIPLY:
    case PKA_ECDSA_GEN:
    case PKA_DSA_GEN:
        skip_byte_len   = ((hw_result->len_b & 0x1) != 0) ? 12 : 8;
        result_byte_len = 4 * hw_result->len_b;
        offset          = add_to_results_fifo(context, results_fifo,
                                              hw_result->ptr_d,
                                              result_byte_len, trace_flag);
        results_desc->result_cnt         = 2;
        results_desc->result[0].offset   = offset;
        results_desc->result[0].byte_len = result_byte_len;
        total_len                        = result_byte_len;

        s_offset = hw_result->ptr_d + result_byte_len + skip_byte_len;
        offset   = add_to_results_fifo(context, results_fifo,
                                       s_offset, result_byte_len, trace_flag);

        results_desc->result[1].offset   = offset;
        results_desc->result[1].byte_len = result_byte_len;
        total_len                       += skip_byte_len + result_byte_len;
        return ROUNDUP(total_len, 8);

    default:
        Assert(FALSE);
    }

    offset = add_to_results_fifo(context, results_fifo,
                                 result_off, result_byte_len, trace_flag);

    results_desc->result[0].offset   = offset;
    results_desc->result[0].byte_len = result_byte_len;
    return ROUNDUP(result_byte_len, 8);
}



static uint32_t total_operands_len(pka_opcode_t  opcode,
                                   uint32_t      shiftCnt,
                                   len_offset_t  operands[])
{
    uint32_t lenA, lenB, max_len, input_len, result_len, p_len, exp_len;
    uint32_t len_pointA, len_pointB;

    switch (opcode)
    {
    case PKA_ADD :
        lenA       = operand_len(operands[0].byte_len, 130);
        lenB       = operand_len(operands[1].byte_len, 130);
        input_len  = ROUNDUP(lenA, 2) + ROUNDUP(lenB, 2);
        result_len = ROUNDUP(MAX(lenA, lenB) + 1, 2);
        return input_len + result_len;

    case PKA_SUBTRACT :
        lenA       = operand_len(operands[0].byte_len, 130);
        lenB       = operand_len(operands[1].byte_len, 130);
        input_len  = ROUNDUP(lenA, 2) + ROUNDUP(lenB, 2);
        result_len = ROUNDUP(MAX(lenA, lenB), 2);
        return input_len + result_len;

    case PKA_MULTIPLY :
        lenA       = operand_len(operands[0].byte_len, 130);
        lenB       = operand_len(operands[1].byte_len, 130);
        input_len  = ROUNDUP(lenA, 2) + ROUNDUP(lenB, 2);
        result_len = ROUNDUP(lenA + lenB + 6, 2);
        return input_len + result_len;

    case PKA_DIVIDE :
        lenA       = operand_len(operands[0].byte_len, 130);
        lenB       = operand_len(operands[1].byte_len, 130);
        input_len  = ROUNDUP(lenA, 2) + ROUNDUP(lenB, 2);
        result_len = ROUNDUP(lenB + 1, 2) + ROUNDUP((lenA - lenB) + 1, 2);
        return input_len + result_len;

    case PKA_MODULO :
        lenA       = operand_len(operands[0].byte_len, 130);
        lenB       = operand_len(operands[1].byte_len, 130);
        input_len  = ROUNDUP(lenA, 2) + ROUNDUP(lenB, 2);
        result_len = ROUNDUP(lenB + 1, 2);
        return input_len + result_len;

    case PKA_SHIFT_LEFT :
        lenA       = operand_len(operands[0].byte_len, 130);
        input_len  = ROUNDUP(lenA, 2);
        result_len = ROUNDUP((shiftCnt == 0) ? lenA : (lenA + 1), 2);
        return input_len + result_len;

    case PKA_SHIFT_RIGHT :
        lenA       = operand_len(operands[0].byte_len, 130);
        input_len  = ROUNDUP(lenA, 2);
        result_len = ROUNDUP(lenA, 2);
        return input_len + result_len;

    case PKA_COMPARE :
        lenA       = operand_len(operands[0].byte_len, 130);
        lenB       = operand_len(operands[1].byte_len, 130);
        max_len    = MAX(lenA, lenB);
        input_len  = 2 * ROUNDUP(max_len, 2);
        result_len = 0;
        return input_len + result_len;

    case PKA_MOD_EXP :
        lenA       = operand_len(operands[0].byte_len, 130);
        lenB       = operand_len(operands[1].byte_len, 130);
        input_len  = ROUNDUP(lenA, 2) + 2 * ROUNDUP(lenB + 1, 2);
        result_len = ROUNDUP(lenB + 1, 2);
        return input_len + result_len;

    case PKA_EXP_WITH_CRT :
        p_len      = operands[0].byte_len;
        exp_len    = MAX(operands[3].byte_len, operands[4].byte_len);
        lenA       = operand_len(exp_len, 66);
        lenB       = operand_len(p_len,   66);
        input_len  = concat_len(lenA, 1, 0, 0) + concat_len(lenB, 1, 2, 1) +
                     ROUNDUP(lenB, 2) + (2 * lenB);
        result_len = 2 * lenB;
        return input_len + result_len;

    case PKA_MOD_INVERT :
        lenA       = operand_len(operands[0].byte_len, 130);
        lenB       = operand_len(operands[1].byte_len, 130);
        input_len  = ROUNDUP(lenA, 2) + ROUNDUP(lenB, 2);
        result_len = ROUNDUP(lenB, 2);
        return input_len + result_len;

    case PKA_ECC_ADD :
        len_pointA = MAX(operands[0].byte_len, operands[1].byte_len);
        len_pointB = MAX(operands[2].byte_len, operands[3].byte_len);
        lenA       = 0;
        lenB       = operand_len(MAX(len_pointA, len_pointB), 24);
        input_len  = 3 * concat_len(lenB, 3, 2, 0);
        result_len = ROUNDUP((2 * lenB) + 3, 2);
        return input_len + result_len;

    case PKA_ECC_MULTIPLY :
        len_pointA = MAX(operands[1].byte_len, operands[2].byte_len);
        lenA       = operand_len(operands[0].byte_len, 24);
        lenB       = operand_len(len_pointA, 24);
        input_len  = ROUNDUP(lenA, 2) + concat3_len(lenB, lenB, lenB, 3, 2) +
                     concat_len(lenB, 3, 2, 0);
        result_len = ROUNDUP((2 * lenB) + 3, 2);
        return input_len + result_len;

    case PKA_ECDSA_GEN :
        lenB       = operand_len(operands[5].byte_len, 24);
        input_len  = ROUNDUP(lenB, 2) + concat6_len(lenB, 3, 2) +
                     (2 * lenB);
        result_len = ROUNDUP((2 * lenB) + 3, 2);
        return input_len + result_len;

    case PKA_ECDSA_VERIFY :
        lenB       = operand_len(operands[5].byte_len, 24);
        input_len  = concat_len(lenB, 3, 2, 0) + concat6_len(lenB, 3, 2) +
                     ROUNDUP(lenB, 2) + concat_len(lenB, 3, 2, 0);
        result_len = 0;
        return input_len + result_len;

    case PKA_DSA_GEN :
        lenA       = operand_len(operands[0].byte_len, 130);
        lenB       = operand_len(operands[2].byte_len, lenA - 1);
        input_len  = ROUNDUP(lenB, 2) + concat3_len(lenA, lenA, lenB, 3, 2) +
                     (2 * lenB);
        result_len = ROUNDUP((2 * lenB) + 3, 2);
        return input_len + result_len;

    case PKA_DSA_VERIFY :
        lenA       = operand_len(operands[0].byte_len, 130);
        lenB       = operand_len(operands[2].byte_len, lenA - 1);
        input_len  = ROUNDUP(lenA, 2) + concat3_len(lenA, lenA, lenB, 3, 2) +
                     ROUNDUP(lenB, 2) + concat_len(lenB, 3, 2, 0);
        result_len = 0;
        return input_len + result_len;

    default :
        Assert(FALSE);
    }
}



static uint32_t copy_len_offs(len_offset_t            len_offs[MAX_OPERAND_CNT],
                              driver_operands_desc_t *operand_fifo_desc,
                              uint32_t                len_offs_offset,
                              uint32_t                operand_cnt)
{
    len_offset_t *operand_len_offs;
    uintptr_t     base_addr;
    uint32_t      max_size, base_offset, operand_idx;

    max_size    = operand_fifo_desc->max_size;
    base_offset = operand_fifo_desc->base_offset;
    base_addr   = master_addr + (uintptr_t) base_offset;

    // Copy out the variable length array len_offs from the ring
    // (where it may be non-contiguous because of ring wrapping in the
    // middle of this array) into a contiguous local variable.
    for (operand_idx = 0;  operand_idx < operand_cnt;  operand_idx++)
    {
        operand_len_offs      = (len_offset_t *) (base_addr + len_offs_offset);
        len_offs[operand_idx] = *operand_len_offs;
        len_offs_offset      += sizeof(len_offset_t);
        if (max_size <= len_offs_offset)
            len_offs_offset = 0;
    }

    return len_offs_offset;
}

static void trim_result_size(driver_results_desc_t *fifo_desc,
                             len_offset_t          *len_offset)
{
  uint32_t offset, byte_len, msb_offset, trunc_cnt = 0;;
    uint8_t *byte_ptr;

    offset   = len_offset->offset;
    byte_len = len_offset->byte_len;
    if ((offset == 0) || (byte_len == 0))
        return;

    if (fifo_desc->max_size < (offset + byte_len))
        return;

    // First find the most significant byte based upon the word_len, and then
    // move backwards over all zero bytes.
    msb_offset = offset + byte_len - 1;
    byte_ptr   = (uint8_t *) (master_addr + msb_offset);
    if (byte_ptr[0] != 0)
        return;

    trunc_cnt = 0;
    while ((byte_ptr[0] == 0) && (2 <= byte_len))
    {
        byte_ptr--;
        byte_len--;
        trunc_cnt++;
    }

    len_offset->byte_len = byte_len;
}



static int gxcr_pka_get_result_from_ring(uint32_t                 mica_idx,
                                         gxio_mica_pka_context_t *context,
                                         uint32_t                 ring,
                                         pka_hw_res_desc_t       *res)
{
    pka_ring_t *hw_ring_desc;
    uint32_t    result_reg, result_cnt, num_elems, cmd_base;
    void       *src;

    result_reg = MICA_CRYPTO_ENG_RESULT_COUNT_0 + (ring * 8);
    result_cnt = __gxio_mmio_read(context->mmio_regs_base + result_reg);
    if (result_cnt == 0)
        return 0;

    hw_ring_desc = &context->ring[ring];
    src          = (void *) (context->mmio_data_base + hw_ring_desc->cmd_head);
    gxcr_pka_copy_from_window(res, src, (sizeof(*res) + 3)/4, TRUE, FALSE);

    // Update some counters.
    if (pka_hw_size != 0)
        pka_hw_size--;

    if (mica_sizes[mica_idx] != 0)
        mica_sizes[mica_idx]--;

    if (pka_state == PKA_HW_FULL)
        pka_state = PKA_HW_BUSY;
    else if ((pka_state == PKA_HW_BUSY) && (pka_hw_size == 0))
        pka_state = PKA_HW_EMPTY;

    // Decrement HW result_count csr by 1 for the result we are now processing.
    __gxio_mmio_write(context->mmio_regs_base + result_reg, 1);

    num_elems = hw_ring_desc->num_elems;
    cmd_base  = hw_ring_desc->cmd_base;
//  cmd_end   = cmd_base + ((num_elems - 1) * sizeof(pka_hw_cmd_desc_t));

    if ((hw_ring_desc->cmd_head - hw_ring_desc->cmd_base) >=
        (sizeof(pka_hw_cmd_desc_t) * (num_elems - 1)))
        hw_ring_desc->cmd_head = hw_ring_desc->cmd_base;
    else
        hw_ring_desc->cmd_head += sizeof(pka_hw_cmd_desc_t);

    hw_ring_desc->cmd_slots_in_use--;
    return 1;
}



static INLINE uint32_t available_cmd_slots(uint32_t mica_idx,
                                           uint32_t ring,
                                           uint32_t operands_size)
{
    gxio_mica_pka_context_t *context;
    uint32_t                 slots_avail;

    context     = &contexts[mica_idx];
    slots_avail = gxcr_pka_cmd_slots_avail(context, ring);
    if (slots_avail == 0)
        return 0;

    if (operand_mem_avail(mica_idx, operands_size))
        return slots_avail;
    else
        return 0;
}

static NOINLINE uint32_t pick_mica(uint32_t  ring,
                                   uint32_t  operands_size,
                                   uint32_t *best_slots_avail_ptr)
{
    uint32_t best_slots_avail, best_mica_idx, mica_idx, slots_avail;

    Assert(last_mica_picked < num_of_micas);
    best_slots_avail = 0;
    best_mica_idx    = 0;

    for (mica_idx = last_mica_picked + 1; mica_idx < num_of_micas; mica_idx++)
    {
        slots_avail = available_cmd_slots(mica_idx, ring, operands_size);
        if (best_slots_avail < slots_avail)
        {
            best_slots_avail = slots_avail;
            best_mica_idx    = mica_idx;
        }
    }

    for (mica_idx = 0; mica_idx <= last_mica_picked; mica_idx++)
    {
        slots_avail = available_cmd_slots(mica_idx, ring, operands_size);
        if (best_slots_avail < slots_avail)
        {
            best_slots_avail = slots_avail;
            best_mica_idx    = mica_idx;
        }
    }

    if (best_slots_avail != 0)
        last_mica_picked = best_mica_idx;

    if (best_slots_avail_ptr != NULL)
        *best_slots_avail_ptr = best_slots_avail;

    return best_mica_idx;
}

static status_t process_cmd(pka_driver_cmd_desc_t *cmd_desc,
                            len_offset_t           len_offs[MAX_OPERAND_CNT],
                            uint32_t               new_head_offset)
{
    gxio_mica_pka_context_t *context;
    driver_operands_desc_t  *operand_fifo_desc;
    operands_alloc_t         alloc;
    boolean_t                isFastCmd;
    uint64_t                 base_addr;
    uint32_t                 operands_size, ring, best_slots_avail, operand_cnt;
    uint32_t                 best_mica_idx, base_offset;
    uint32_t                 len_offs_offset, client_idx;
    uint32_t                 head_offset, bytes_consumed, max_size;
    tbn_t                    cmd;
    int                      rc;

    // First determine isSmallCmd.
    isFastCmd     = FALSE;  // ???
    ring          = isFastCmd ? FAST_CMD_RING : NORMAL_CMD_RING;
    operands_size = cmd_desc->operands_size;
    Assert(operands_size < MAX_ALLOC_SIZE);

    // Find MICA engine with the most slots available.  In the event of a
    // tie, round robin amongst the shims.
    best_mica_idx = pick_mica(ring, operands_size, &best_slots_avail);
    if (best_slots_avail == 0)
    {
        pka_state = PKA_HW_FULL;
        return FAILURE;
    }

    client_idx            = cmd_desc->client_idx;
    operand_cnt           = cmd_desc->operand_cnt;
    operand_fifo_desc     = &master_record->driver_operand_descs[client_idx];
    base_offset           = operand_fifo_desc->base_offset;
    head_offset           = operand_fifo_desc->head_offset;
    max_size              = operand_fifo_desc->max_size;
    alloc.base_src_offset = base_offset;
    alloc.max_src_offset  = base_offset + max_size;
    alloc.trace_flag      = cmd_desc->trace_flag;

    // Found (at least one) a MICA that still has room for this request.
    context               = &contexts[best_mica_idx];
    base_offset           = alloc_operand_mem(best_mica_idx, operands_size);
    alloc.window_base_ptr = (uint8_t *) context->mmio_data_base;
    alloc.dst_offset      = base_offset;
    alloc.max_dst_offset  = base_offset + operands_size;
    gxcr_pka_clear_window_ram(&alloc, operands_size);

    len_offs_offset = cmd_desc->offset_of_operand_len_offs;
    base_addr       = master_addr + (uint64_t) base_offset;

    copy_operands(&cmd, &alloc, cmd_desc->opcode, operand_cnt,
                  cmd_desc->shift_cnt, &len_offs[0]);

    // Determine new head_offset and number of bytes consumed.
    if (head_offset < new_head_offset)
        bytes_consumed = new_head_offset - head_offset;
    else
        bytes_consumed = new_head_offset + (max_size - head_offset);

    // Now actually append a cmd entry to the HW ring fifos.
    operand_fifo_desc->head_offset = new_head_offset;
    rc = gxcr_pka_append_cmd(best_mica_idx, ring, cmd_desc, &cmd);
    Assert(rc == 0);

    update_cmd_stats(best_mica_idx, operands_size);

    // Update the client operand ring to indicate that the operands have been
    // freed!
    arch_atomic_sub(&master_record->client_operand_descs[client_idx].curr_size,
                    bytes_consumed);
    operand_fifo_desc->bytes_removed += bytes_consumed;
    pka_state = PKA_HW_BUSY;
    return SUCCESS;
}



static void add_to_overflow_queue(pka_driver_cmd_desc_t *cmd_desc)
{
    int rc;

    if (overflow_queue_size == 0)
    {
        // The head element of this queue is held in the variable,
        // overflow_queue_head, and so if the queue_size is 0 going to 1, we
        // just copy the cmd descriptor into this variable, increment the
        // queue_size and return.
        overflow_queue_head = *cmd_desc;
        overflow_queue_size++;
        return;
    }

    rc = overflow_queue_enqueue(&overflow_queue, *cmd_desc);
    if (rc != 0)
    {
        total_stats.overflow_enqueue_errs++;
        return;
    }

    total_stats.overflow_enqueue_cnt++;
    overflow_queue_size++;
    total_stats.overflow_queue_peak_size = MAX(total_stats.overflow_queue_peak_size,
                                               overflow_queue_size);
    if (OVERFLOW_QUEUE_MAX_SIZE <= overflow_queue_size)
    {
        if (master_record->dont_accept_new_reqs == FALSE)
        {
            master_record->dont_accept_new_reqs = TRUE;
            total_stats.num_no_new_reqs_set++;
        }
    }
}

static void process_overflow_queue(void)
{
    driver_operands_desc_t *operand_fifo_desc;
    pka_driver_cmd_desc_t  *cmd_desc;
    len_offset_t            len_offs[MAX_OPERAND_CNT];
    uint32_t                client_idx, new_head_offset;
    int                     rc;

    Assert(overflow_queue_size != 0);
    cmd_desc          = &overflow_queue_head;
    client_idx        = cmd_desc->client_idx;
    operand_fifo_desc = &master_record->driver_operand_descs[client_idx];
    new_head_offset   = copy_len_offs(len_offs, operand_fifo_desc,
                                      cmd_desc->offset_of_operand_len_offs,
                                      cmd_desc->operand_cnt);

    if (SUCCESS != process_cmd(cmd_desc, &len_offs[0], new_head_offset))
        return;

    overflow_queue_size--;
    if (overflow_queue_size != 0)
    {
        rc = overflow_queue_dequeue(&overflow_queue, &overflow_queue_head);
        Assert(rc == 0);
        total_stats.overflow_dequeue_cnt++;
    }

    if (overflow_queue_size < (OVERFLOW_QUEUE_MAX_SIZE - 512))
    {
        if (master_record->dont_accept_new_reqs)
        {
            master_record->dont_accept_new_reqs = FALSE;
            total_stats.num_no_new_reqs_cleared++;
        }
    }
}

static boolean_t process_input_queue(void)
{
    client_operands_desc_t *client_operand_ring;
    driver_operands_desc_t *operand_fifo_desc;
    pka_driver_cmd_desc_t   cmd_desc;
    len_offset_t            len_offs[MAX_OPERAND_CNT];
    uint32_t                client_idx, new_head_offset, word_len;
    stats_t                *client_stats;
    int                     rc;

    rc = pka_cmd_dequeue(&master_record->cmd_queue, &cmd_desc);
    if (rc != 0)
        return FALSE;

    client_idx        = cmd_desc.client_idx;
    operand_fifo_desc = &master_record->driver_operand_descs[client_idx];
    new_head_offset   = copy_len_offs(len_offs, operand_fifo_desc,
                                      cmd_desc.offset_of_operand_len_offs,
                                      cmd_desc.operand_cnt);

    word_len = total_operands_len(cmd_desc.opcode, cmd_desc.shift_cnt,
                                  &len_offs[0]);
    cmd_desc.operands_size = 4 * word_len;
    Assert(cmd_desc.operands_size < MAX_ALLOC_SIZE);

    client_operand_ring = &master_record->client_operand_descs[client_idx];

    client_stats = &master_record->client_idx_stats[client_idx];
    client_stats->requests++;
    client_stats->peak_request_queue_size = client_operand_ring->peak_size;
    total_stats.requests++;

    // If the overflow queue is not empty, we must append everything to this
    // queue until it becomes empty again in order to preserve request
    // ordering.
    if (overflow_queue_size != 0)
    {
        add_to_overflow_queue(&cmd_desc);
        return FALSE;
    }

    // Try to copy into pka window and queue request.   If fail, put it on the
    // overflow queue.
    if (SUCCESS == process_cmd(&cmd_desc, &len_offs[0], new_head_offset))
        return TRUE;

    add_to_overflow_queue(&cmd_desc);
    return FALSE;
}



static void process_result(uint32_t                 mica_idx,
                           gxio_mica_pka_context_t *context,
                           uint32_t                 ring)
{
    pka_driver_result_desc_t results_desc;
    driver_results_desc_t   *results_fifo_desc;
    pka_hw_res_desc_t        hw_result;
    pka_opcode_t             opcode;
    queue_size_t            *queue_size_ptr;
    boolean_t                trace_flag;
    uint32_t                 result_cnt, client_idx, result_idx, res_len;
    uint32_t                 cycles_taken, total_results_len, prev_reply_size;
    uint32_t                 reply_queue_size, peak_reply_size;
    stats_t                 *client_stats;
    void                    *user_data;
    int                      rc;

    result_cnt = gxcr_pka_get_result_from_ring(mica_idx, context, ring,
                                               &hw_result);
    Assert(result_cnt != 0);
    cycles_taken = remove_from_user_data_fifo(mica_idx, ring, hw_result.tag,
                                              &user_data, &client_idx,
                                              &trace_flag);

    // Convert from HW command code to our more abstract opcode.
    opcode = CMD_CODE_TO_OPCODE[hw_result.command];

    // Copy the result vectors from the window ram to the results ring.
    results_fifo_desc = &master_record->driver_result_descs[client_idx];
    memset(&results_desc, 0, sizeof(results_desc));
    total_results_len = copy_results(context, &hw_result, opcode,
                                     results_fifo_desc, &results_desc,
                                     trace_flag);

    // Increment the size of the results ring.
    arch_atomic_add(&results_fifo_desc->curr_size, total_results_len);
    results_fifo_desc->bytes_added += total_results_len;
    if (results_fifo_desc->peak_size < results_fifo_desc->curr_size)
        results_fifo_desc->peak_size = results_fifo_desc->curr_size;

    results_desc.user_data = user_data;
    for (result_idx = 0;  result_idx < result_cnt;  result_idx++)
        trim_result_size(results_fifo_desc, &results_desc.result[result_idx]);

    if (trace_flag)
    {
        res_len = results_desc.result[0].byte_len;
        if (((res_len + 7) < total_results_len) && (8 <= res_len))
            DBG_LOG("process_result total_results_len=%u byte_len=%u",
                    total_results_len, results_desc.result[0].byte_len);
    }

    if (trace_flag)
        DBG_LOG("process_result opcode=%u result_cnt=%u result1 len=%u",
                results_desc.opcode, results_desc.result_cnt,
                results_desc.result[0].byte_len);

    // Add results_desc to the reply ring.
    __insn_mf();
    rc = pka_reply_enqueue(&master_record->reply_queues[client_idx],
                           results_desc);
    __insn_mf();

    // Free up the operand+result block.
    free_operand_mem(mica_idx, hw_result.ptr_a);

    client_stats = &master_record->client_idx_stats[client_idx];
    if (rc != 0)
    {
        total_stats.reply_errs++;
        mica_stats[mica_idx].reply_errs++;
        return;
    }

    // Increment the reply_queue_size.
    queue_size_ptr   = &master_record->reply_queue_sizes[client_idx];
    prev_reply_size  = arch_atomic_increment(&queue_size_ptr->queue_cnt);
    reply_queue_size = prev_reply_size + 1;
    peak_reply_size  = MAX(client_stats->peak_reply_queue_size,
                           reply_queue_size);

    // Update some stats.
    update_result_stats(mica_idx, total_results_len, cycles_taken);
    client_stats->replies++;
    client_stats->peak_reply_queue_size  = peak_reply_size;

    if (trace_flag)
        DBG_LOGFLUSH();
}

static INLINE void process_results(void)
{
    gxio_mica_pka_context_t *context;
    uint32_t                 mica_idx, ring;
    int                      result_cnt;

    for (mica_idx = 0; mica_idx < num_of_micas; mica_idx++)
    {
        context = &contexts[mica_idx];
        for (ring = 0; ring < NUM_MICA_RINGS; ring++)
        {
            result_cnt = gxcr_pka_results_avail(context, ring);
            if (1 <= result_cnt)
                process_result(mica_idx, context, ring);
        }
    }
}

static void close_client_idx(uint32_t client_idx)
{
    client_desc_t *client_desc;
    stats_t       *client_stats;

    client_stats = &master_record->client_idx_stats[client_idx];
    client_desc  = &master_record->client_idxs.client_descs[client_idx];
    total_stats.closed_client_idxs++;

    if (client_stats->requests == client_stats->replies)
        return;

    DBG_LOG("On close client_idx=%u epoch=%u had requests=%" PRIu64 " and "
            "replies=%" PRIu64 "\n", client_idx, client_desc->epoch,
            client_stats->requests, client_stats->replies);
    DBG_LOGFLUSH();
}



static void fill_random_buf(void)
{
    cacheline_t *dst_ptr, *next_dst_ptr;
    uint64_t     start_cycles, elapsed_cycles, avg_cycles, cycles_used;

    if (NUM_RAND_CACHELINES <= random_bytes_buf.size)
        return;

    // Just get ONE cacheline's worth of random numbers per fill_random_buf
    // call.
    dst_ptr      = &random_bytes_buf.cachelines[random_bytes_buf.tail_idx];
    start_cycles = get_cycle_count();
    get_random_bytes(0, (uint32_t *) dst_ptr, sizeof(cacheline_t));
    elapsed_cycles = get_cycle_count() - start_cycles;

    random_bytes_buf.size++;
    random_bytes_buf.tail_idx++;
    if (NUM_RAND_CACHELINES <= random_bytes_buf.tail_idx)
        random_bytes_buf.tail_idx = 0;

    next_dst_ptr = &random_bytes_buf.cachelines[random_bytes_buf.tail_idx];
    __insn_prefetch_l2(next_dst_ptr);

    // Now update some random_bytes_buf stats.
    avg_cycles  = random_bytes_buf.avg_cycles;
    cycles_used = (uint32_t) MIN(elapsed_cycles, MAX_CLOCK_CYCLES);
    avg_cycles  = ((avg_cycles << 4) + cycles_used + 8 - avg_cycles) >> 4;

    random_bytes_buf.num_fills++;
    random_bytes_buf.min_cycles = MIN(random_bytes_buf.min_cycles, cycles_used);
    random_bytes_buf.max_cycles = MAX(random_bytes_buf.max_cycles, cycles_used);
    random_bytes_buf.avg_cycles = avg_cycles;
}

static void init_random_buf(void)
{
    uint32_t idx;

    memset(&random_bytes_buf, 0, sizeof(random_bytes_buf));
    random_bytes_buf.min_cycles = MAX_CLOCK_CYCLES;

    // Bring the random_bytes buf into our L2 cache.
    for (idx = 0; idx < NUM_RAND_CACHELINES; idx++)
        __insn_prefetch_l2(&random_bytes_buf.cachelines[idx]);

    // Now preload the random_bytes_buf.
    while (random_bytes_buf.size < NUM_RAND_CACHELINES)
        fill_random_buf();
}

static void reset_random_buf_stats(void)
{
    random_bytes_buf.num_fills  = 0;
    random_bytes_buf.min_cycles = MAX_CLOCK_CYCLES;
    random_bytes_buf.avg_cycles = 0;
    random_bytes_buf.max_cycles = 0;
}



static void fill_client_random_fifo(uint32_t client_idx, uint32_t max_bytes)
{
    rand_fifo_desc_t *fifo_desc;
    cacheline_t      *src_ptr;
    uint32_t          ring_size, tail_offset, rem_src_len, rem_dst_len;
    uint32_t          src_bytes_avail, copy_len, num_cachelines;
    uint8_t          *dst_ptr;

    fifo_desc = &master_record->rand_fifos[client_idx];
    ring_size = 64 * 1024;
    __insn_mf();

    tail_offset = fifo_desc->tail_offset;
    Assert(tail_offset < ring_size);

    src_ptr = &random_bytes_buf.cachelines[random_bytes_buf.head_idx];
    dst_ptr = (uint8_t *) (master_addr + fifo_desc->base_offset + tail_offset);

    src_bytes_avail = sizeof(cacheline_t) * random_bytes_buf.size;
    rem_src_len     = sizeof(cacheline_t) *
                            (NUM_RAND_CACHELINES - random_bytes_buf.tail_idx);
    rem_dst_len     = ring_size - tail_offset;
    copy_len        = MIN(MIN(rem_dst_len, max_bytes),
                          MIN(src_bytes_avail, rem_src_len));

    // Make sure copy_len is a multiple of the cache line size.
    num_cachelines = copy_len / sizeof(cacheline_t);
    copy_len       = sizeof(cacheline_t) * num_cachelines;
    Assert(copy_len != 0);

    // Now do the copy.
    tmc_mem_write_hint(dst_ptr, copy_len);
    memcpy(dst_ptr, src_ptr, copy_len);
    client_rand_ctl[client_idx].bytes_provided += copy_len;

    random_bytes_buf.size     -= num_cachelines;
    random_bytes_buf.head_idx += num_cachelines;
    if (NUM_RAND_CACHELINES <= random_bytes_buf.head_idx)
        random_bytes_buf.head_idx = 0;

    tail_offset += copy_len;
    if (ring_size <= tail_offset)
        tail_offset = 0;

    fifo_desc->tail_offset = tail_offset;
    atomic_add(&fifo_desc->curr_size, copy_len);
    tmc_mem_fence();
}

static void update_client_rand_ctl(uint32_t client_idx,
                                   uint32_t current_size,
                                   uint32_t ring_size)
{
    client_rand_ctl_t *rand_ctl;
    uint64_t           rand_size64;
    uint32_t           rand_size32, bytes_consumed, total_consumed, goal;

    rand_ctl    = &client_rand_ctl[client_idx];
    rand_size64 = rand_ctl->bytes_provided - rand_ctl->bytes_consumed;
    if (0xFFFFFFFF < rand_size64)
        return;  // Error

    rand_size32 = (uint32_t) rand_size64;
    if (rand_size32 < current_size)
        return;  // Error

    if (current_size == rand_size32)
        return;  // Normal case.

    bytes_consumed = rand_size32 - current_size;
    if (ring_size <= bytes_consumed)
        return;  // Error

    goal           = rand_ctl->fifo_size_goal;
    total_consumed = rand_ctl->bytes_consumed + bytes_consumed;
    goal           = MIN(goal + total_consumed, ring_size / 2);

    rand_ctl->bytes_consumed = total_consumed;
    rand_ctl->fifo_size_goal = goal;
}

static void fill_client_random_fifos(void)
{
    uint32_t client_idx, ring_size, current_size;

    ring_size = 64 * 1024;
    for (client_idx = 0; client_idx < MAX_NUM_OF_CLIENTS; client_idx++)
    {
        if (in_use[client_idx] == 0)
            continue;

        // Only add to one client's random fifo per call.
        current_size = master_record->rand_fifos[client_idx].curr_size;
        update_client_rand_ctl(client_idx, current_size, ring_size);

        if (current_size < client_rand_ctl[client_idx].fifo_size_goal)
        {
            fill_client_random_fifo(client_idx, sizeof(cacheline_t));
            return;
        }
    }
}



static void init_driver_stats(void)
{
    uint32_t mica_idx;

    memset(&total_stats,   0, sizeof(total_stats));
    memset(&mica_stats[0], 0, sizeof(mica_stats));

    for (mica_idx = 0; mica_idx < num_of_micas; mica_idx++)
        mica_stats[mica_idx].min_cycles = MAX_CLOCK_CYCLES;
}

static void dump_and_clear_stats(void)
{
    mica_stats_t *mica_stat;
    uint64_t      avg_cycles64;
    uint32_t      mica_idx, avg_cycles;

    // First dump the total_stats.
    fprintf(driver_log, "\nTotal stats:");
    fprintf(driver_log, "  reqs=%" PRIu64 " started=%" PRIu64 " reply_errs=%"
            PRIu64 " replies=%" PRIu64 "\n",
            total_stats.requests, total_stats.reqs_started,
            total_stats.reply_errs, total_stats.replies);
    fprintf(driver_log, "  operand_bytes=%" PRIu64 " result_bytes=%" PRIu64
            "\n", total_stats.operand_bytes, total_stats.result_bytes);
    fprintf(driver_log, "  opened_clients=%u closed_clients=%u close_reqs=%u\n",
            total_stats.opened_client_idxs, total_stats.closed_client_idxs,
            total_stats.closed_by_request);

    // Next dump the overflow queue stats.
    fprintf(driver_log, "\nOverflow queue stats:\n");
    fprintf(driver_log, "  enqueue_cnt=%" PRIu64 " dequeue_cnt=%" PRIu64
            " peak_size=%u enqueue_errs=%u\n",
            total_stats.overflow_enqueue_cnt, total_stats.overflow_dequeue_cnt,
            total_stats.overflow_queue_peak_size,
            total_stats.overflow_enqueue_errs);
    fprintf(driver_log, "  num_no_new_reqs_set=%u num_no_new_reqs_cleared=%u\n",
            total_stats.num_no_new_reqs_set,
            total_stats.num_no_new_reqs_cleared);

    // Next dump the random_bytes_buf stats.
    fprintf(driver_log, "\nRandom bytes buf stats:\n");
    fprintf(driver_log, "num_fills=%" PRIu64 " size=%u max_size=%u cycles_used "
            "min=%u avg=%u max=%u\n", random_bytes_buf.num_fills,
            random_bytes_buf.size * (uint32_t) sizeof(cacheline_t),
            NUM_RAND_BYTES, random_bytes_buf.min_cycles,
            random_bytes_buf.avg_cycles, random_bytes_buf.max_cycles);

    // Next dump the per mica shim stats.
    for (mica_idx = 0; mica_idx < num_of_micas; mica_idx++)
    {
        mica_stat = &mica_stats[mica_idx];
        avg_cycles = 0;
        if (mica_stat->replies != 0)
        {
            avg_cycles64 = mica_stat->total_cycles / mica_stat->replies;
            avg_cycles   = (uint32_t) MIN(avg_cycles64, MAX_CLOCK_CYCLES);
        }

        fprintf(driver_log, "\nMica %u stats:\n", mica_idx);
        fprintf(driver_log, "  reqs_started=%" PRIu64 " reply_errs=%" PRIu64
                " replies=%" PRIu64 " peak_cmd_cnt=%u operand_bytes=%" PRIu64
                " result_bytes=%" PRIu64 "\n",
                mica_stat->reqs_started, mica_stat->reply_errs,
                mica_stat->replies, mica_stat->peak_cmd_cnt,
                mica_stat->operand_bytes, mica_stat->result_bytes);
        fprintf(driver_log, "  min_cycles=%u avg_cycles=%u max_cycles=%u\n",
                mica_stat->min_cycles, avg_cycles, mica_stat->max_cycles);
    }

    DBG_LOGFLUSH();
    init_driver_stats();
    reset_random_buf_stats();
}



static void close_clients_matching_pid(pid_t pid)
{
    client_desc_t *client_desc;
    uint32_t       client_idx;

    for (client_idx = 0;  client_idx < MAX_NUM_OF_CLIENTS;  client_idx++)
    {
        client_desc = &master_record->client_idxs.client_descs[client_idx];
        if ((client_desc->in_use != 0) && (client_desc->pid == pid))
        {
            DBG_LOG("Closing client_idx=%u epoch=%u pid=%u since the process "
                    "no longer exists",
                    client_idx, client_desc->epoch, client_desc->pid);
            close_client_idx(client_idx);
            init_client_idx(client_idx);
        }
    }

    DBG_NEWLINE;
    DBG_LOGFLUSH();
}

static void poll_for_closed_client_idxs(void)
{
    client_desc_t *client_desc;
    uint32_t       client_idx;
    pid_t          pid, last_pid;
    char           proc_path[128];

    last_pid = 0;
    tmc_spin_queued_mutex_lock(&master_record->client_idxs.client_idx_lock);

    for (client_idx = 0;  client_idx < MAX_NUM_OF_CLIENTS;  client_idx++)
    {
        client_desc = &master_record->client_idxs.client_descs[client_idx];
        if (client_desc->in_use == 0)
            continue;

        if (client_desc->req_close)
        {
            DBG_LOG("Closed client_idx=%u epoch=%u pid=%u",
                    client_idx, client_desc->epoch, client_desc->pid);
            DBG_LOGFLUSH();
            total_stats.closed_by_request++;
            close_client_idx(client_idx);
            init_client_idx(client_idx);
        }
    }

    for (client_idx = 0;  client_idx < MAX_NUM_OF_CLIENTS;  client_idx++)
    {
        client_desc = &master_record->client_idxs.client_descs[client_idx];
        if (client_desc->in_use == 0)
            continue;

        // Check whether the process with the given pid still exists, by trying
        // to check if the directory "/proc/pid" exists.  Optimize the common
        //  casde that this client_idx pid matches a previous one.
        pid = client_desc->pid;
        if (pid != last_pid)
        {
            sprintf(proc_path, "/proc/%u", client_desc->pid);
            if (access(proc_path, F_OK) != 0)
            {
                DBG_LOG("Found process that no longer exists pid=%u",
                        client_desc->pid);
                DBG_LOGFLUSH();
                close_clients_matching_pid(pid);
            }

            break;
        }
    }

    tmc_spin_queued_mutex_unlock(&master_record->client_idxs.client_idx_lock);
}

// Note that this fcn skips using the lock because if is polled at high
// rate and besides the in_use flag can only go one way?
static void poll_for_new_client_idxs(void)
{
    client_desc_t *client_desc;
    uint32_t       client_idx;

    for (client_idx = 0;  client_idx < MAX_NUM_OF_CLIENTS;  client_idx++)
    {
        client_desc = &master_record->client_idxs.client_descs[client_idx];
        if ((client_desc->in_use != 0) && (in_use[client_idx] == 0))
        {
            in_use[client_idx] = 1;
            num_in_use++;
            total_stats.opened_client_idxs++;
            DBG_LOG("New client_idx=%u epoch=%u pid=%u",
                    client_idx, client_desc->epoch, client_desc->pid);
            DBG_LOGFLUSH();
            fill_client_random_fifo(client_idx, 4 * sizeof(cacheline_t));
        }
    }
}

static uint64_t update_uptime(uint64_t current_cycles,
                              uint64_t next_millisec_cycles)
{
    uint32_t cnt;

    for (cnt = 1;  cnt <= 1000;  cnt++)
    {
        if (current_cycles < next_millisec_cycles)
            break;

        next_millisec_cycles += cycles_per_millisec;
        msecs_in_second++;
        if (msecs_in_second == 1000)
        {
            msecs_in_second = 0;
            uptime_in_secs++;
        }
    }

    return next_millisec_cycles;
}

static void main_loop(void)
{
    uint64_t last_poll_cycles, poll_elapsed_cycles, next_millisec_cycles;
    uint64_t current_cycles;
    uint32_t errors;

    errors               = 0;
    cycles_per_millisec  = tmc_perf_get_cpu_speed() / 1000;
    last_poll_cycles     = get_cycle_count();
    next_millisec_cycles = get_cycle_count() + cycles_per_millisec;

    while (errors == 0)
    {
        __insn_mf();

        // First look for any new requests.  We try to do two input requests
        // here to reduce the risk of input_queue drop's.
        process_results();
        if (process_input_queue())
            process_results();

        if (process_input_queue())
            process_results();

        // Next process the overflow queue, if not empty.
        if ((overflow_queue_size != 0) && (pka_state != PKA_HW_FULL))
            process_overflow_queue();

        // Next look for new results available.
        process_results();
        __insn_mf();

#if ENABLE_TRNG
        // Next look for random fifos that are getting low
        if (random_bytes_buf.size < NUM_RAND_CACHELINES)
            fill_random_buf();
        else
            fill_client_random_fifos();
#endif

        if (dump_stats != 0)
        {
            dump_stats = 0;
            dump_and_clear_stats();
            dump_stats = 0;  // Just to be sure.
        }

        poll_for_new_client_idxs();

        // Update the uptime_in_secs and msecs_in_second;
        current_cycles = get_cycle_count();
        if (next_millisec_cycles <= current_cycles)
            next_millisec_cycles = update_uptime(current_cycles,
                                                 next_millisec_cycles);

        poll_elapsed_cycles = current_cycles - last_poll_cycles;
        if (1000000 < poll_elapsed_cycles)
        {
            // Poll the client_idxs record occasionally looking for
            // (a) context_idxs that have requested a close and
            // (b) context_idxs whose processes (as identified by the
            // client_desc pid) no longer exist.
            poll_for_closed_client_idxs();
            last_poll_cycles = get_cycle_count();
        }
    }
}



static status_t open_shared_mem(char *shared_mem_name)
{
    uint32_t perms, mprotect;

    perms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
    shared_mem_fd = shm_open(shared_mem_name, O_RDWR | O_CREAT | O_EXCL, perms);
    if (shared_mem_fd < 0)
    {
        DBG_STRING("shared_mem already exists");

        if (errno == EEXIST)
        {
            shm_unlink(PKA_DRIVER_MMAP_NAME);

            shared_mem_fd = shm_open(shared_mem_name, O_RDWR | O_CREAT | O_EXCL,
                                     perms);
        }

        if (shared_mem_fd < 0)
        {
            printf("shared_mem_fd=%d errno=%d exists=%d\n", shared_mem_fd,
                   errno, EEXIST);
            perror("Error opening shared memory object using shm_open");
            DBG_STRING("Error opening shared memory object using shm_open");
            DBG_LOGFLUSH();
            return FAILURE;
        }
    }

    ftruncate(shared_mem_fd, PKA_DRIVER_MMAP_SIZE);
    mprotect       = PROT_READ | PROT_WRITE;
    shared_mem_ptr = mmap(NULL, PKA_DRIVER_MMAP_SIZE, mprotect, MAP_SHARED,
                          shared_mem_fd, 0);
    if (shared_mem_ptr == MAP_FAILED)
    {
        perror("Error calling mmap of shared memory object");
        DBG_STRING("Error calling mmap of shared memory object");
        return FAILURE;
    }

    memset(shared_mem_ptr, 0, PKA_DRIVER_MMAP_SIZE);
    DBG_STRING("open_shared_mem succeeded");
    fflush(NULL);
    return SUCCESS;
}

static void init_client_idxs(void)
{
    client_desc_t *client_desc;
    uint32_t       client_idx;

    tmc_spin_queued_mutex_lock(&master_record->client_idxs.client_idx_lock);

    for (client_idx = 0;  client_idx < MAX_NUM_OF_CLIENTS;  client_idx++)
    {
        client_desc = &master_record->client_idxs.client_descs[client_idx];
        memset(client_desc, 0, sizeof(client_desc_t));
    }

    master_record->client_idxs.max_client_idx  = MAX_NUM_OF_CLIENTS - 1;
    master_record->client_idxs.last_idx_opened = 0;
    master_record->client_idxs.magic_num       = 0xBABEBABE;
    tmc_spin_queued_mutex_unlock(&master_record->client_idxs.client_idx_lock);
}

static void init_operand_fifo(uint32_t client_idx)
{
    client_operands_desc_t *client_operands_desc;
    driver_operands_desc_t *driver_operands_desc;
    uint32_t                ring_offset, base_offset;
    void                   *base_ptr;

    client_operands_desc = &master_record->client_operand_descs[client_idx];
    driver_operands_desc = &master_record->driver_operand_descs[client_idx];
    memset(client_operands_desc, 0, sizeof(client_operands_desc_t));
    memset(driver_operands_desc, 0, sizeof(driver_operands_desc_t));

    // *TBD* Check that operandMemPerClient is a multiple of 8???
    ring_offset = client_idx * operand_mem_per_client;
    base_offset = master_record->first_operand_ring_offset + ring_offset;

    client_operands_desc->base_offset = base_offset;
    driver_operands_desc->base_offset = base_offset;
    client_operands_desc->max_size    = operand_mem_per_client;
    driver_operands_desc->max_size    = operand_mem_per_client;

    base_ptr = (void *) (master_addr + base_offset);
    memset(base_ptr, 0, operand_mem_per_client);
}

static void init_result_fifo(uint32_t client_idx)
{
    driver_results_desc_t *driver_results_desc;
    client_results_desc_t *client_results_desc;
    queue_size_t          *reply_queue_size;
    pka_reply_t           *reply_queue;
    uint32_t               ring_offset, base_offset;
    void                   *base_ptr;

    driver_results_desc = &master_record->driver_result_descs[client_idx];
    client_results_desc = &master_record->client_result_descs[client_idx];
    reply_queue_size    = &master_record->reply_queue_sizes[client_idx];
    reply_queue         = &master_record->reply_queues[client_idx];

    memset(driver_results_desc, 0, sizeof(driver_results_desc_t));
    memset(client_results_desc, 0, sizeof(client_results_desc_t));
    memset(reply_queue_size,    0, sizeof(queue_size_t));

    // *TBD* Check that resultMemPerClient is a multiple of 8???
    ring_offset = client_idx * result_mem_per_client;
    base_offset = master_record->first_result_ring_offset + ring_offset;

    driver_results_desc->base_offset = base_offset;
    client_results_desc->base_offset = base_offset;
    driver_results_desc->max_size    = result_mem_per_client;
    client_results_desc->max_size    = result_mem_per_client;

    base_ptr = (void *) master_addr + base_offset;
    memset(base_ptr, 0, result_mem_per_client);

    pka_reply_init(reply_queue);
}

static void init_rand_fifo(uint32_t client_idx)
{
    client_rand_ctl_t *rand_ctl;
    rand_fifo_desc_t  *rand_fifo_desc;
    uint32_t           first_ring_offset, ring_size;
    void              *base_ptr;

    rand_fifo_desc = &master_record->rand_fifos[client_idx];
    memset(rand_fifo_desc, 0, sizeof(rand_fifo_desc_t));

    first_ring_offset           = master_record->first_rand_ring_offset;
    ring_size                   = 64 * 1024;
    rand_fifo_desc->base_offset = first_ring_offset + (client_idx * ring_size);
    rand_fifo_desc->max_offset  = rand_fifo_desc->base_offset + ring_size;

    base_ptr = (void *) (master_addr + rand_fifo_desc->base_offset);
    memset(base_ptr, 0, ring_size);

    rand_ctl = &client_rand_ctl[client_idx];
    memset(rand_ctl, 0, sizeof(client_rand_ctl_t));
    rand_ctl->fifo_size_goal = 4 * sizeof(cacheline_t);
}

static void init_client_idx(uint32_t client_idx)
{
    client_desc_t *client_desc;
    stats_t       *client_idx_stats;

    client_desc      = &master_record->client_idxs.client_descs[client_idx];
    client_idx_stats = &master_record->client_idx_stats[client_idx];

    client_desc->available = FALSE;

    init_operand_fifo(client_idx);
    init_result_fifo(client_idx);
    init_rand_fifo(client_idx);
    memset(client_idx_stats, 0, sizeof(stats_t));

    if (client_desc->initialized == 0)
    {
        client_desc->initialized = 1;
        client_desc->client_idx  = client_idx;
        client_desc->epoch       = 1;
    }
    else
    {
        client_desc->epoch++;
        if (client_desc->epoch == 0)
            client_desc->epoch = 1;

        if ((in_use[client_idx] != 0) && (num_in_use != 0))
            num_in_use--;

        in_use[client_idx] = 0;
    }

    client_desc->req_open  = FALSE;
    client_desc->req_close = FALSE;
    client_desc->in_use    = FALSE;
    client_desc->available = TRUE;
}



void signal_handler(int signal)
{
    size_t num_stack_frames;
    char  *signal_name;
    void  *bt_array[128];

    if (signal == SIGHUP)
    {
        // Set a global bit telling the main loop to dump its stats.
        dump_stats = 1;
        __insn_mf();
        return;
    }

    switch (signal)
    {
    case SIGSEGV: signal_name = "SIGSEGV";  break;
    case SIGTERM: signal_name = "SIGTERM";  break;
    case SIGINT:  signal_name = "SIGINT";   break;
    default:      signal_name = "UNKNOWN";  break;
    }

    num_stack_frames = backtrace(bt_array, 100);
    DBG_LOG("Received signal=%u (%s) exiting.",   signal, signal_name);
    backtrace_symbols_fd(bt_array, num_stack_frames, fileno(driver_log));
    DBG_LOGFLUSH();
    fclose(driver_log);

    printf ("Received signal=%u (%s) exiting.\n", signal, signal_name);
    _exit(1);
}

void usage(void)
{
}

int main(int argc, char *argv[])
{
    gxio_mica_pka_ring_config_t ring_config;
    struct sigaction            signal_action;
    struct rlimit               rlimit;
    uint32_t                    client_idx, mica_idx, memory_avail, cache_lines;
    uint32_t                    first_ring_offset, total_operand_ring_size;
    uint32_t                    rand_mem_per_client, total_result_ring_size;
    char                        driver_log_filename[128];
    int                         rc;

    num_of_micas = 1;
    if (2 <= argc)
        num_of_micas = atoi(argv[1]);

    uptime_in_secs  = 0;
    msecs_in_second = 0;

    sprintf(driver_log_filename, "pka-driver-%u.log", getpid());
    driver_log = fopen(driver_log_filename, "w");

    // Don't register the signal handler until AFTER the driver_log has been
    // opened.  The sigfillset call causes all signals to be blocked while
    // the handler is running.  SIGHUP can be used to cause the pka_driver
    // to append various internal statistics to the log file.  All other
    // signals will cause a backtrace to be printed and logged and a core
    // file to be generated.
    memset(&signal_action, 0, sizeof(signal_action));
    signal_action.sa_handler = signal_handler;
    sigfillset(&signal_action.sa_mask);
    rc = sigaction(SIGSEGV, &signal_action, NULL);
    rc = sigaction(SIGTERM, &signal_action, NULL);
    rc = sigaction(SIGINT,  &signal_action, NULL);
    rc = sigaction(SIGHUP,  &signal_action, NULL);

    getrlimit(RLIMIT_CORE, &rlimit);
    rlimit.rlim_cur = rlimit.rlim_max;
    setrlimit(RLIMIT_CORE, &rlimit);

    DBG_STRING("PKA driver starting up");
    if (SUCCESS != open_shared_mem(PKA_DRIVER_MMAP_NAME))
    {
        printf("open_shared_mem failed.  Exiting\n");
        DBG_STRING("open_shared_mem failed.  Exiting");
        DBG_LOGFLUSH();
        return -1;
    }

    memset(in_use, 0, sizeof(in_use));
    num_in_use = 0;
    DBG_LOGFLUSH();

    // Initialize the SW overflow queue.
    overflow_queue_init(&overflow_queue);

    // Initialize the master record and the cmd queue.
    master_addr   = (uintptr_t) shared_mem_ptr;
    master_record = (master_record_t *) master_addr;
    pka_cmd_init(&master_record->cmd_queue);

    memory_avail        = PKA_DRIVER_MMAP_SIZE - offsetof(master_record_t, mem);
    memory_per_client   = memory_avail / MAX_NUM_OF_CLIENTS;
    cache_lines         = memory_per_client / sizeof(cacheline_t);
    rand_mem_per_client = 64 * 1024;
    memory_per_client   = (cache_lines * sizeof(cacheline_t)) -
                              rand_mem_per_client;

    result_mem_per_client  = memory_per_client / 3;
    cache_lines            = result_mem_per_client / sizeof(cacheline_t);
    result_mem_per_client  = cache_lines * sizeof(cacheline_t);
    operand_mem_per_client = memory_per_client - result_mem_per_client;

    // *TBD* Round to next cache line.
    total_operand_ring_size = operand_mem_per_client * MAX_NUM_OF_CLIENTS;
    total_result_ring_size  = result_mem_per_client  * MAX_NUM_OF_CLIENTS;
    first_ring_offset       = offsetof(master_record_t, mem);
    master_record->first_operand_ring_offset = first_ring_offset;
    master_record->first_result_ring_offset  = first_ring_offset +
                                                   total_operand_ring_size;
    master_record->first_rand_ring_offset    = first_ring_offset +
                                                   total_operand_ring_size +
                                                   total_result_ring_size;

    memset(&master_record->client_operand_descs[0], 0,
           sizeof(master_record->client_operand_descs));
    memset(&master_record->driver_operand_descs[0], 0,
           sizeof(master_record->driver_operand_descs));
    memset(&master_record->driver_result_descs[0], 0,
           sizeof(master_record->driver_result_descs));
    memset(&master_record->client_result_descs[0], 0,
           sizeof(master_record->client_result_descs));

    // Initialize the user_data_fifos.
    memset(&user_data_fifos[0][0], 0, sizeof(user_data_fifos));

    // Open contexts to each mica.
    ring_config.num_rings               = 2;
    ring_config.ring_0_is_high_priority = FALSE;
    ring_config.ring_elem_size[0]       = 0;
    ring_config.ring_elem_size[1]       = 0;

    for (mica_idx = 0; mica_idx < num_of_micas; mica_idx++)
    {
        rc = gxcr_pka_init(&contexts[mica_idx], mica_idx, &ring_config);
        if (rc < 0)
            DBG_LOG("gxio_mica_pka_init failed on shim idx=%u with rc=%d",
                    mica_idx, rc);
        else
            DBG_LOG("gxio_mica_pka_init succeeded on mica shim idx=%u",
                    mica_idx);

        init_pka_mem_allocator(mica_idx);
    }

    // Initialize the client_idx_lock.
    tmc_spin_queued_mutex_init(&master_record->client_idxs.client_idx_lock);

    // Test True Random number generator.
    DBG_LOGFLUSH();
    init_random_buf();
    get_random_bytes(0, random_test_block, 100);

    init_client_idxs();
    for (client_idx = 0;  client_idx < MAX_NUM_OF_CLIENTS;  client_idx++)
        init_client_idx(client_idx);

    init_driver_stats();
    last_mica_picked = 0;

    // Main Loop
    main_loop();

    munmap(shared_mem_ptr, PKA_DRIVER_MMAP_SIZE);
    close(shared_mem_fd);
    shm_unlink(PKA_DRIVER_MMAP_NAME);

    DBG_STRING("PKA driver shutting down normally");
    DBG_LOGFLUSH();
    return 0;
}
