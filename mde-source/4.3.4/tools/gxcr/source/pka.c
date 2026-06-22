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


#include <stdint.h>
#include <ctype.h>
#include <sys/types.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <arch/cycle.h>
#include <gxcr/pka.h>
#include "pka_driver.h"


// This file implements an interface from User Space programs to the Mica
// PKA driver.  The Mica PKA hardware makes available a large number of
// arithmetic operations, both basic (like add and multiply) and complex
// (like modular exponentiation and DSA).


#define MAGIC_NUM  0x1234BABE

#define ENABLE_ASSERT 1


#define MAX(a, b)  (((a) <= (b)) ? (b) : (a))
#define MIN(a, b)  (((a) <= (b)) ? (a) : (b))
#define ROUNDUP(val, align)  (((val) + ((align) - 1)) & (~ ((align) - 1)))

#define NOINLINE __attribute__((noinline))


#ifdef ENABLE_ASSERT
#define Assert(cond)                                                   \
    ({                                                                 \
        if (! (cond))                                                  \
        {                                                              \
            printf("Assertion failed @ %s:%u\n", __FILE__, __LINE__);  \
            abort();                                                   \
        }                                                              \
    })
#else
#define Assert(cond)
#endif

struct gxcr_pka_handle_s
{
    uint8_t  *master_ptr;
    void     *cmd_ring;
    void     *operand_ring_desc;
    void     *reply_ring;
    void     *reply_queue_size;
    void     *driver_results;
    void     *result_ring_desc;
    uint32_t *results_size_ptr;
    void     *rand_fifo_desc;
    uint32_t  head_reply_desc[8];
    uint32_t  prev_reply_desc[8];
    uint32_t  head_reply_avail;
    uint32_t  rand_fifo_head_offset;
    uint32_t  client_idx;
    uint32_t  epoch;
    uint32_t  magic_num;
    uint32_t  trace_cnt;  // when 0, tracing is disabled.
    int       fd;
    uint8_t   big_endian;
};

typedef struct
{
    uint8_t *first_part_ptr;
    uint8_t *second_part_ptr;
    uint32_t first_part_len;
    uint32_t second_part_len;
    uint32_t total_len;
} result_operand_parts_t;

typedef struct
{
    result_operand_parts_t result_parts[2];
} result_ptrs_t;


static const uint8_t OPERAND_CNT_TBL[] =
{
    [PKA_ADD]          = 2,
    [PKA_SUBTRACT]     = 2,
    [PKA_MULTIPLY]     = 2,
    [PKA_DIVIDE]       = 2,
    [PKA_MODULO]       = 2,
    [PKA_SHIFT_LEFT]   = 1,
    [PKA_SHIFT_RIGHT]  = 1,
    [PKA_COMPARE]      = 2,
    [PKA_MOD_EXP]      = 3,
    [PKA_EXP_WITH_CRT] = 6,
    [PKA_MOD_INVERT]   = 2,
    [PKA_ECC_ADD]      = 7,
    [PKA_ECC_MULTIPLY] = 6,
    [PKA_ECDSA_GEN]    = 9,
    [PKA_ECDSA_VERIFY] = 11,
    [PKA_DSA_GEN]      = 6,
    [PKA_DSA_VERIFY]   = 7
};

static uint8_t HEX_CHARS[] = "0123456789ABCDEF";
static uint8_t FROM_HEX[256] =
{
    ['0'] = 0,  ['1'] = 1,  ['2'] = 2,  ['3'] = 3,  ['4'] = 4,
    ['5'] = 5,  ['6'] = 6,  ['7'] = 7,  ['8'] = 8,  ['9'] = 9,
    ['a'] = 10, ['b'] = 11, ['c'] = 12, ['d']  = 13, ['e'] = 14, ['f'] = 15,
    ['A'] = 10, ['B'] = 11, ['C'] = 12, ['D'] = 13, ['E'] = 14, ['F'] = 15
};



static NOINLINE void busy_wait_delay(void)
{
    uint32_t cnt;

    // Wait for ~300 cycles.
    for (cnt = 1;  cnt < 50;  cnt++)
        cycle_relax();
}

static uint32_t process_operand(pka_operand_t *value, uint8_t big_endian)
{
    uint32_t byte_len;
    uint8_t *byte_ptr;

    byte_len          = value->actual_len;
    value->big_endian = big_endian;
    if (byte_len == 0)
        return 0;

    if (big_endian != 0)
    {
        byte_ptr = &value->buf_ptr[0];
        if (byte_ptr[0] != 0)
            return byte_len;

        // Move forwards over all zero bytes.
        while ((byte_ptr[0] == 0) && (1 <= byte_len))
        {
            byte_ptr++;
            byte_len--;
        }

        value->buf_ptr    = byte_ptr;
        value->actual_len = byte_len;
        return byte_len;
    }
    else  // little-endian.
    {
        // First find the most significant byte based upon the actual_len, and
        // then move backwards over all zero bytes, in order to skip leading
        // zeros and find the real byte_len.
        byte_ptr = &value->buf_ptr[byte_len - 1];
        if (byte_ptr[0] != 0)
            return byte_len;

        while ((byte_ptr[0] == 0) && (1 <= byte_len))
        {
            byte_ptr--;
            byte_len--;
        }

        value->actual_len = byte_len;
        return byte_len;
    }
}

static pka_comparison_t internal_compare(uint8_t *value_buf_ptr,
                                         uint8_t *comparend_buf_ptr,
                                         uint32_t operand_len,
                                         uint8_t  is_big_endian)
{
    uint32_t idx, value_len, comparend_len;

    if (is_big_endian)
    {
        // Start the comparison at the most significant end which is at the
        // lowest idx.  But first we need to skip any leading zeros!
        value_len = operand_len;
        while ((value_buf_ptr[0] == 0) && (2 <= value_len))
        {
            value_buf_ptr++;
            value_len--;
        }

        comparend_len = operand_len;
        while ((comparend_buf_ptr[0] == 0) && (2 <= comparend_len))
        {
            comparend_buf_ptr++;
            comparend_len--;
        }

        if (value_len < comparend_len)
            return PKA_LESS_THAN;
        else if (comparend_len < value_len)
            return PKA_GREATER_THAN;

        operand_len = value_len;
        for (idx = 1;  idx <= operand_len;  idx++)
        {
            if (value_buf_ptr[0] < comparend_buf_ptr[0])
                return PKA_LESS_THAN;
            else if (value_buf_ptr[0] > comparend_buf_ptr[0])
                return PKA_GREATER_THAN;

            value_buf_ptr++;
            comparend_buf_ptr++;
        }
    }
    else
    {
        // Start the comparison at the most significant end which is at the
        // highest idx.  But first we need to skip any leading zeros!
        value_buf_ptr = &value_buf_ptr[operand_len - 1];
        value_len     = operand_len;
        while ((value_buf_ptr[0] == 0) && (2 <= value_len))
        {
            value_buf_ptr--;
            value_len--;
        }

        comparend_buf_ptr = &comparend_buf_ptr[operand_len - 1];
        comparend_len     = operand_len;
        while ((comparend_buf_ptr[0] == 0) && (2 <= comparend_len))
        {
            comparend_buf_ptr--;
            comparend_len--;
        }

        if (value_len < comparend_len)
            return PKA_LESS_THAN;
        else if (comparend_len < value_len)
            return PKA_GREATER_THAN;

        operand_len = value_len;
        for (idx = 1;  idx <= operand_len;  idx++)
        {
            if (value_buf_ptr[0] < comparend_buf_ptr[0])
                return PKA_LESS_THAN;
            else if (value_buf_ptr[0] > comparend_buf_ptr[0])
                return PKA_GREATER_THAN;

            value_buf_ptr--;
            comparend_buf_ptr--;
        }
    }

    return PKA_EQUAL;
}



static boolean_t gxcr_get_client_idx (master_record_t *master_record,
                                      uint32_t        *client_idx_ptr,
                                      uint32_t        *epoch_ptr)
{
    client_desc_t *client_desc;
    boolean_t      found;
    uint32_t       max_client_idx, num_client_idxs, last_idx_opened;
    uint32_t       client_idx, cnt;
    pid_t          pid;

    tmc_spin_queued_mutex_lock(&master_record->client_idxs.client_idx_lock);

    pid             = getpid();
    max_client_idx  = master_record->client_idxs.max_client_idx;
    num_client_idxs = max_client_idx + 1;
    last_idx_opened = master_record->client_idxs.last_idx_opened;
    client_idx      = last_idx_opened;
    found           = FALSE;

    for (cnt = 1;  cnt <= num_client_idxs;  cnt++)
    {
        client_idx++;
        if (max_client_idx < client_idx)
            client_idx = 0;

        client_desc = &master_record->client_idxs.client_descs[client_idx];
        if ((client_desc->available  == TRUE)  &&
            (client_desc->in_use     == FALSE) &&
            (client_desc->client_idx == client_idx))
        {
            client_desc->pid       = pid;
            client_desc->available = FALSE;
            client_desc->in_use    = TRUE;
            found                  = TRUE;
            *client_idx_ptr        = client_idx;
            *epoch_ptr             = client_desc->epoch;

            master_record->client_idxs.last_idx_opened = client_idx;
            __insn_mf();
            break;
        }
    }

    tmc_spin_queued_mutex_unlock(&master_record->client_idxs.client_idx_lock);

    return found;
}

// Initialize a PKA handle.
gxcr_pka_handle_t* gxcr_pka_open(uint8_t use_big_endian)
{
    gxcr_pka_handle_t *handle;
    master_record_t   *master_record;
    boolean_t          found;
    uint32_t           protect, client_idx, epoch;
    void              *shared_mem_ptr;
    int                shared_mem_fd;

    shared_mem_fd = shm_open(PKA_DRIVER_MMAP_NAME, O_RDWR, 0);
    if (shared_mem_fd < 0)
    {
        // *TBD* Try to contact the Linux driver, instead!!!
        printf("gxcr_pka_open - shm_open call failed\n");
        return NULL;
    }

    protect        = PROT_READ | PROT_WRITE;
    shared_mem_ptr = mmap(NULL, PKA_DRIVER_MMAP_SIZE, protect,
                          MAP_SHARED, shared_mem_fd, 0);
    if (shared_mem_ptr == MAP_FAILED)
    {
        printf("gxcr_pka_open - mmap call failed\n");
        return NULL;
    }

    handle = malloc(sizeof(gxcr_pka_handle_t));
    memset(handle, 0, sizeof(gxcr_pka_handle_t));
    handle->fd         = shared_mem_fd;
    handle->big_endian = use_big_endian;
    handle->master_ptr = shared_mem_ptr;
    master_record      = (master_record_t *) shared_mem_ptr;

    if (master_record->client_idxs.magic_num != 0xBABEBABE)
    {
        printf("gxcr_pka_open - master record magic number error\n");
        return NULL;
    }

    // Get a unique client_idx for our exclusive use.
    found = gxcr_get_client_idx(master_record, &client_idx, &epoch);
    if (found == FALSE)
    {
        printf("gxcr_pka_open - no available client_idxs\n");
        return NULL;
    }

    // Now initialize our client_idx based data structures.
    handle->magic_num        = MAGIC_NUM;
    handle->client_idx       = client_idx;
    handle->epoch            = epoch;
    handle->cmd_ring         = &master_record->cmd_queue;
    handle->operand_ring_desc= &master_record->client_operand_descs[client_idx];
    handle->reply_ring       = &master_record->reply_queues[client_idx];
    handle->reply_queue_size = &master_record->reply_queue_sizes[client_idx];
    handle->driver_results   = &master_record->driver_result_descs[client_idx];
    handle->result_ring_desc = &master_record->client_result_descs[client_idx];

    handle->results_size_ptr = &master_record->driver_result_descs[client_idx].curr_size;

    handle->rand_fifo_desc        = &master_record->rand_fifos[client_idx];
    handle->rand_fifo_head_offset = 0;
    printf("gxcr_pka_open succeeded on pid=%u client_idx=%u epoch=%u\n",
           getpid(), client_idx, epoch);
    return handle;
}

int gxcr_pka_close(gxcr_pka_handle_t *handle)
{
    master_record_t *master_record;
    client_desc_t   *client_desc;
    uint32_t         client_idx;

    if (handle->magic_num != MAGIC_NUM)
        return 0;

    client_idx = handle->client_idx;
    close(handle->fd);

    // Note that we mark the client_idx descriptor with a req_close, but
    // leave the client_idx descriptor in_use alone.  The driver, when it
    // notices the req_close, will then clear both.
    handle->magic_num = 0;
    handle->fd        = 0;

    master_record = (master_record_t *) handle->master_ptr;
    client_desc   = &master_record->client_idxs.client_descs[client_idx];

    client_desc->req_close = TRUE;
    return 0;
}

uint8_t gxcr_pka_get_endian(gxcr_pka_handle_t *handle)
{
    return handle->big_endian;
}

void enable_driver_tracing(gxcr_pka_handle_t *handle,
                           uint32_t           max_num_requests)
{
    handle->trace_cnt = max_num_requests;
}

void disable_driver_tracing(gxcr_pka_handle_t *handle)
{
    handle->trace_cnt = 0;
}

void byte_swap_cpy(void *dst_ptr, void *src_ptr, uint32_t byte_len)
{
    uintptr_t dst_addr, src_addr;
    uint64_t *dst64_ptr, *src64_ptr, src1_aligned, src2_aligned, src_word;
    uint32_t  misaligned, cnt;
    uint8_t  *dst8_ptr, *src8_ptr;

    dst8_ptr = (uint8_t *) dst_ptr;
    dst_addr = (uintptr_t) dst8_ptr;

    // Src8 points one byte past end of src
    src8_ptr = ((uint8_t *) src_ptr) + byte_len;

    if (byte_len >= 16)
    {
        // Align dst to 8-bytes
        misaligned = dst_addr & 7;
        if (misaligned != 0)
        {
            // To align dst to an 8-byte boundary, we need to copy
            // "8 - misaligned" bytes from the src.
            for (cnt = 1; cnt <= (8 - misaligned); cnt++)
            {
                *dst8_ptr++ = *(--src8_ptr);
                byte_len--;
            }
        }

        // Dst pointer now aligned to 8-bytes.
        dst64_ptr = (uint64_t *) dst8_ptr;
        // Src pointer might be aligned or not aligned.
        src64_ptr = (uint64_t *) src8_ptr;

        // Check for end of src aligned to 8-bytes.
        // Byte_len is >= (16 - 7) (max misaligned is 7)
        src_addr = (uintptr_t) src8_ptr;
        if ((src_addr & 7) == 0)
        {
            // Src pointer is also aligned.
            while (byte_len >= 8)
            {
                *dst64_ptr++ = __insn_revbytes(*(--src64_ptr));
                byte_len    -= 8;
            }
        }
        else
        {
            // src pointer is not aligned, read the two 8-byte aligned words
            // containing the unaligned src word and then the use double-align
            // instruction to create the aligned word.

            // Second aligned word containing the src_word.
            src2_aligned = __insn_ldna(src64_ptr);
            while (byte_len >= 8)
            {
                // First aligned word containing src_word
                src1_aligned = __insn_ldna(--src64_ptr);

                // Extract an 8-byte word using an unaligned ptr (src64_ptr)
                // from two aligned words.
                src_word = __insn_dblalign(src1_aligned, src2_aligned,
                                           src64_ptr);
                *dst64_ptr++ = __insn_revbytes(src_word);
                byte_len -= 8;

                // Shifted the aligned word over 8-bytes.
                src2_aligned = src1_aligned;
            }
        }

        // Handle any left over bytes (must be less than eight) with a
        // byte loop, at the end of this function.
        dst8_ptr = (uint8_t *) dst64_ptr;
        src8_ptr = (uint8_t *) src64_ptr;
    }

    // Handle any remaining bytes, or short lengths.
    while (byte_len--)
        *dst8_ptr++ = *(--src8_ptr);
}

int gxcr_pka_from_hex_string(char *hex_string, pka_operand_t *value)
{
    uint32_t string_len, hexDigitCnt, char_idx, byte_len, hexDigitState;
    uint32_t hex_value, hex_value1 = 0;
    uint8_t *big_num_ptr, ch, big_endian, byte_value;

    // Skip the initial 0x, if present
    string_len = strlen(hex_string);
    if ((3 <= string_len) && (hex_string[0] == '0') &&
        (hex_string[1] == 'x'))
    {
        hex_string += 2;
        string_len -= 2;
    }

    // Next count the number of hexadecimal characters in the string (i.e.
    // ignoring things like spaces and underscores).
    hexDigitCnt = 0;
    for (char_idx = 0;  char_idx < string_len;  char_idx++)
    {
        ch = hex_string[char_idx];
        if (isxdigit(ch))
            hexDigitCnt++;
        else if ((! isspace(ch)) && (ch != '_'))
            return -1;
    }

    byte_len = (hexDigitCnt + 1) / 2;
    if (value->buf_ptr == NULL)
    {
        value->buf_ptr = malloc(byte_len);
        value->buf_len = byte_len;
    }
    else if (value->buf_len < byte_len)
        return -1;

    value->actual_len   = byte_len;
    value->is_encrypted = 0;
    big_endian          = value->big_endian;
    if (big_endian)
        big_num_ptr = &value->buf_ptr[0];
    else
        big_num_ptr = &value->buf_ptr[byte_len - 1];

    hexDigitState = 0;
    hex_value1    = 0;
    if ((hexDigitCnt & 0x1) != 0)
        hexDigitState = 1;

    for (char_idx = 0;  char_idx < string_len;  char_idx++)
    {
        ch = hex_string[char_idx];
        if (isxdigit(ch))
        {
            hex_value = FROM_HEX[ch];
            if (hexDigitState == 0)
            {
                hex_value1    = hex_value;
                hexDigitState = 1;
            }
            else
            {
                byte_value    = (hex_value1 << 4) | hex_value;
                hexDigitState = 0;
                if (big_endian)
                    *big_num_ptr++ = byte_value;
                else
                    *big_num_ptr-- = byte_value;
            }
        }
    }

    return 0;
}

int gxcr_pka_to_hex_string(pka_operand_t *value,
                           char          *string_buf,
                           uint32_t       buf_len)
{
    uint32_t byte_len, byte_cnt, byte_value;
    uint8_t *byte_ptr;
    char    *char_ptr;

    byte_len = value->actual_len;
    if (buf_len <= byte_len)
        return -1;

    memset(string_buf, 0, buf_len);

    if (value->big_endian)
    {
        byte_ptr = &value->buf_ptr[0];
        char_ptr = &string_buf[0];
        for (byte_cnt = 0;  byte_cnt < byte_len;  byte_cnt++)
        {
            byte_value  = *byte_ptr++;
            *char_ptr++ = HEX_CHARS[byte_value >> 4];
            *char_ptr++ = HEX_CHARS[byte_value & 0x0F];
        }
    }
    else
    {
        byte_ptr = &value->buf_ptr[byte_len - 1];
        char_ptr = &string_buf[0];
        for (byte_cnt = 0;  byte_cnt < byte_len;  byte_cnt++)
        {
            byte_value  = *byte_ptr--;
            *char_ptr++ = HEX_CHARS[byte_value >> 4];
            *char_ptr++ = HEX_CHARS[byte_value & 0x0F];
        }
    }

    return 0;
}

pka_comparison_t pka_compare(pka_operand_t *value, pka_operand_t *comparend)
{
    pka_comparison_t result;
    pka_operands_t   operands;
    uint32_t         value_len, comparend_len;
    uint8_t          big_endian;

    if ((value == NULL) || (comparend == NULL))
        return PKA_NO_COMPARE;

    if ((value->buf_ptr == NULL) || (comparend->buf_ptr == NULL))
        return PKA_NO_COMPARE;

    // Fail comparison if the operands are not of the same endian.
    big_endian = value->big_endian;
    if (big_endian != comparend->big_endian)
    {
        printf("pka_compare mismatch endian\n");
        return PKA_NO_COMPARE;
    }

    operands.operands[0] = *value;
    operands.operands[1] = *comparend;

    big_endian    = value->big_endian;
    value_len     = process_operand(&operands.operands[0], big_endian);
    comparend_len = process_operand(&operands.operands[1], big_endian);

    if (value_len < comparend_len)
        result = PKA_LESS_THAN;
    else if (comparend_len < value_len)
        result = PKA_GREATER_THAN;
    else
        result = internal_compare(operands.operands[0].buf_ptr,
                                  operands.operands[1].buf_ptr,
                                  value_len, value->big_endian);

    return result;
}

static uint32_t pka_bits_in_byte(uint8_t byte)
{
    int32_t bit_num;

    if (byte == 0)
        return 0;

    // Assumes byte != 0;
    for (bit_num = 7;  bit_num >= 0;  bit_num--)
        if ((byte & (1 << bit_num)) != 0)
            return bit_num + 1;

    // Should never reach here
    return 0;
}

uint32_t pka_operand_byte_len(pka_operand_t *value)
{
    uint32_t byte_len;
    uint8_t *byte_ptr;

    byte_len = value->actual_len;
    if (byte_len == 0)
        return 0;

    if (value->big_endian != 0)
    {
        byte_ptr = &value->buf_ptr[0];
        if (byte_ptr[0] != 0)
            return byte_len;

        // Move forwards over all zero bytes.
        while ((byte_ptr[0] == 0) && (1 <= byte_len))
        {
            byte_ptr++;
            byte_len--;
        }
    }
    else
    {
        // First find the most significant byte based upon the actual_len, and
        // then move backwards over all zero bytes.
        byte_ptr = &value->buf_ptr[byte_len - 1];
        if (byte_ptr[0] != 0)
            return byte_len;

        while ((byte_ptr[0] == 0) && (1 <= byte_len))
        {
            byte_ptr--;
            byte_len--;
        }
    }

    return byte_len;
}

uint32_t pka_operand_bit_len(pka_operand_t *value)
{
    uint32_t byte_len;
    uint8_t *byte_ptr;

    byte_len = value->actual_len;
    if (byte_len == 0)
        return 0;

    if (value->big_endian != 0)
    {
        // Move forwards over all zero bytes.
        byte_ptr = &value->buf_ptr[0];
        if (byte_ptr[0] != 0)
            return (8 * (byte_len - 1)) + pka_bits_in_byte(byte_ptr[0]);

        while ((byte_ptr[0] == 0) && (1 <= byte_len))
        {
            byte_ptr++;
            byte_len--;
        }
    }
    else
    {
        // First find the most significant byte based upon the actual_len, and
        // then move backwards over all zero bytes.
        byte_ptr = &value->buf_ptr[byte_len - 1];
        if (byte_ptr[0] != 0)
            return (8 * (byte_len - 1)) + pka_bits_in_byte(byte_ptr[0]);

        while ((byte_ptr[0] == 0) && (1 <= byte_len))
        {
            byte_ptr--;
            byte_len--;
        }
    }

    if (byte_len == 0)
        return 0;
    else
        return (8 * (byte_len - 1)) + pka_bits_in_byte(byte_ptr[0]);
}



// Implements value + addend -> result.
gxcr_pka_err_t gxcr_pka_add(gxcr_pka_handle_t *handle,
                            void              *user_data,
                            pka_operand_t     *value,
                            pka_operand_t     *addend)
{
    pka_operands_t operands;
    uint32_t       value_len, addend_len;
    uint8_t        big_endian;

    if ((value == NULL) || (addend == NULL))
        return PKA_OPERAND_MISSING;

    if ((value->buf_ptr == NULL) || (addend->buf_ptr == NULL))
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0] = *value;
    operands.operands[1] = *addend;

    big_endian = handle->big_endian;
    value_len  = process_operand(&operands.operands[0], big_endian);
    addend_len = process_operand(&operands.operands[1], big_endian);

    if ((value_len == 0) || (addend_len == 0))
        return PKA_OPERAND_LEN_ZERO;

    if ((MAX_BYTE_LEN < value_len) || (MAX_BYTE_LEN < addend_len))
        return PKA_OPERAND_LEN_TOO_LONG;

    operands.operand_cnt        = 2;
    operands.shift_amount       = 0;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_ADD, &operands);
}

// Implements value - subtrahend -> result.
gxcr_pka_err_t gxcr_pka_subtract(gxcr_pka_handle_t *handle,
                                 void              *user_data,
                                 pka_operand_t     *value,
                                 pka_operand_t     *subtrahend)
{
    pka_comparison_t comparison;
    pka_operands_t   operands;
    uint32_t         value_len, subtrahend_len;
    uint8_t          big_endian;

    if ((value == NULL) || (subtrahend == NULL))
        return PKA_OPERAND_MISSING;

    if ((value->buf_ptr == NULL) || (subtrahend->buf_ptr == NULL))
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0] = *value;
    operands.operands[1] = *subtrahend;

    big_endian     = handle->big_endian;
    value_len      = process_operand(&operands.operands[0], big_endian);
    subtrahend_len = process_operand(&operands.operands[1], big_endian);

    if ((value_len == 0) || (subtrahend_len == 0))
        return PKA_OPERAND_LEN_ZERO;

    if ((MAX_BYTE_LEN < value_len) || (MAX_BYTE_LEN < subtrahend_len))
        return PKA_OPERAND_LEN_TOO_LONG;

    // Check that subtrahend <= value (i.e.
    // operands.operands[1] <= operands.operands[0])
    comparison = pka_compare(&operands.operands[1], &operands.operands[0]);
    if ((comparison != PKA_LESS_THAN) && (comparison != PKA_EQUAL))
        return PKA_RESULT_MUST_BE_POSITIVE;

    operands.operand_cnt        = 2;
    operands.shift_amount       = 0;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_SUBTRACT, &operands);
}



// Implements value * multiplier ->result.
gxcr_pka_err_t gxcr_pka_multiply(gxcr_pka_handle_t *handle,
                                 void              *user_data,
                                 pka_operand_t     *value,
                                 pka_operand_t     *multiplier)
{
    pka_operands_t operands;
    uint32_t       value_len, multiplier_len;
    uint8_t        big_endian;

    if ((value == NULL) || (multiplier == NULL))
        return PKA_OPERAND_MISSING;

    if ((value->buf_ptr == NULL) || (multiplier->buf_ptr == NULL))
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0] = *value;
    operands.operands[1] = *multiplier;

    big_endian     = handle->big_endian;
    value_len      = process_operand(&operands.operands[0], big_endian);
    multiplier_len = process_operand(&operands.operands[1], big_endian);

    if ((value_len == 0) || (multiplier_len == 0))
        return PKA_OPERAND_LEN_ZERO;

    if ((MAX_BYTE_LEN < value_len) || (MAX_BYTE_LEN < multiplier_len))
        return PKA_OPERAND_LEN_TOO_LONG;

    operands.operand_cnt        = 2;
    operands.shift_amount       = 0;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_MULTIPLY, &operands);
}

// Implements value / divisor -> modulus, quotient.  divisor must not be 0.
gxcr_pka_err_t gxcr_pka_divide(gxcr_pka_handle_t *handle,
                               void              *user_data,
                               pka_operand_t     *value,
                               pka_operand_t     *divisor)
{
    pka_operands_t operands;
    uint32_t       value_len, divisor_len;
    uint8_t        big_endian;

    if ((value == NULL) || (divisor == NULL))
        return PKA_OPERAND_MISSING;

    if ((value->buf_ptr == NULL) || (divisor->buf_ptr == NULL))
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0] = *value;
    operands.operands[1] = *divisor;

    big_endian  = handle->big_endian;
    value_len   = process_operand(&operands.operands[0], big_endian);
    divisor_len = process_operand(&operands.operands[1], big_endian);

    if ((value_len == 0) || (divisor_len == 0))
        return PKA_OPERAND_LEN_ZERO;

    if ((value_len < 5) || (divisor_len < 5))
        return PKA_OPERAND_LEN_TOO_SHORT;

    if ((MAX_BYTE_LEN < value_len) || (MAX_BYTE_LEN < divisor_len))
        return PKA_OPERAND_LEN_TOO_LONG;

    if (value_len < divisor_len)
        return PKA_OPERAND_LEN_A_LT_LEN_B;

    operands.operand_cnt        = 2;
    operands.shift_amount       = 0;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_DIVIDE, &operands);
}

// Implements value mod modulus ->result.  modulus must not be 0.
gxcr_pka_err_t gxcr_pka_modulo(gxcr_pka_handle_t *handle,
                               void              *user_data,
                               pka_operand_t     *value,
                               pka_operand_t     *modulus)
{
    pka_operands_t operands;
    uint32_t       value_len, modulus_len, value_word_len, modulus_word_len;;
    uint8_t        big_endian;

    if ((value == NULL) || (modulus == NULL))
        return PKA_OPERAND_MISSING;

    if ((value->buf_ptr == NULL) || (modulus->buf_ptr == NULL))
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0] = *value;
    operands.operands[1] = *modulus;

    big_endian  = handle->big_endian;
    value_len   = process_operand(&operands.operands[0], big_endian);
    modulus_len = process_operand(&operands.operands[1], big_endian);

    if ((value_len == 0) || (modulus_len == 0))
        return PKA_OPERAND_LEN_ZERO;

    if ((value_len < 5) || (modulus_len < 5))
        return PKA_OPERAND_LEN_TOO_SHORT;

    if ((MAX_BYTE_LEN < value_len) || (MAX_BYTE_LEN < modulus_len))
        return PKA_OPERAND_LEN_TOO_LONG;

    value_word_len   = (value_len   + 3) / 4;
    modulus_word_len = (modulus_len + 3) / 4;
    if (value_word_len < modulus_word_len)
        return PKA_OPERAND_LEN_A_LT_LEN_B;

    operands.operand_cnt        = 2;
    operands.shift_amount       = 0;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_MODULO, &operands);
}

// Implements value << shift_amount -> result.
gxcr_pka_err_t gxcr_pka_shift_left(gxcr_pka_handle_t *handle,
                                   void              *user_data,
                                   pka_operand_t     *value,
                                   uint32_t           shift_amount)
{
    pka_operands_t operands;
    uint32_t       value_len;
    uint8_t        big_endian;

    if (value == NULL)
        return PKA_OPERAND_MISSING;

    if (value->buf_ptr == NULL)
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0] = *value;

    big_endian = handle->big_endian;
    value_len  = process_operand(&operands.operands[0], big_endian);
    if (value_len == 0)
        return PKA_OPERAND_LEN_ZERO;

    if (MAX_BYTE_LEN < value_len)
        return PKA_OPERAND_LEN_TOO_LONG;

    // Check that shift_amount is <= 32?

    operands.operand_cnt        = 1;
    operands.shift_amount       = shift_amount;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_SHIFT_LEFT, &operands);
}

// Implements value >> shift_amount -> result.
gxcr_pka_err_t gxcr_pka_shift_right(gxcr_pka_handle_t *handle,
                                    void              *user_data,
                                    pka_operand_t     *value,
                                    uint32_t           shift_amount)
{
    pka_operands_t operands;
    uint32_t       value_len;
    uint8_t        big_endian;

    if (value == NULL)
        return PKA_OPERAND_MISSING;

    if (value->buf_ptr == NULL)
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0] = *value;

    big_endian = handle->big_endian;
    value_len  = process_operand(&operands.operands[0], big_endian);
    if (value_len == 0)
        return PKA_OPERAND_LEN_ZERO;

    if (MAX_BYTE_LEN < value_len)
        return PKA_OPERAND_LEN_TOO_LONG;

    // Check that shift_amount is <= 32?

    operands.operand_cnt        = 1;
    operands.shift_amount       = shift_amount;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_SHIFT_RIGHT, &operands);
}

// Implements value <> comparend -> compare_result.
gxcr_pka_err_t gxcr_pka_compare(gxcr_pka_handle_t *handle,
                                void              *user_data,
                                pka_operand_t     *value,
                                pka_operand_t     *comparend)
{
    pka_operands_t operands;
    uint32_t       value_len, comparend_len;
    uint8_t        big_endian;

    if ((value == NULL) || (comparend == NULL))
        return PKA_OPERAND_MISSING;

    if ((value->buf_ptr == NULL) || (comparend->buf_ptr == NULL))
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0] = *value;
    operands.operands[1] = *comparend;

    big_endian    = handle->big_endian;
    value_len     = process_operand(&operands.operands[0], big_endian);
    comparend_len = process_operand(&operands.operands[1], big_endian);
    if ((value_len == 0) || (comparend_len == 0))
        return PKA_OPERAND_LEN_ZERO;

    if ((MAX_BYTE_LEN < value_len) || (MAX_BYTE_LEN < comparend_len))
        return PKA_OPERAND_LEN_TOO_LONG;

    operands.operand_cnt        = 2;
    operands.shift_amount       = 0;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_COMPARE, &operands);
}

// Implements value^exponent mod modulus -> result.
gxcr_pka_err_t gxcr_pka_mod_exp(gxcr_pka_handle_t *handle,
                                void              *user_data,
                                pka_operand_t     *exponent,
                                pka_operand_t     *modulus,
                                pka_operand_t     *value)
{
    pka_operands_t operands;
    uint32_t       exponent_len, modulus_len, value_len;
    uint8_t        big_endian;

    if ((exponent == NULL) || (modulus == NULL) || (value == NULL))
        return PKA_OPERAND_MISSING;

    if ((exponent->buf_ptr == NULL) || (modulus->buf_ptr == NULL) ||
        (value->buf_ptr    == NULL))
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0] = *exponent;
    operands.operands[1] = *modulus;
    operands.operands[2] = *value;

    big_endian   = handle->big_endian;
    exponent_len = process_operand(&operands.operands[0], big_endian);
    modulus_len  = process_operand(&operands.operands[1], big_endian);
    value_len    = process_operand(&operands.operands[2], big_endian);

    if ((exponent_len == 0) || (modulus_len == 0) || (value_len == 0))
        return PKA_OPERAND_LEN_ZERO;

    if ((MAX_BYTE_LEN < exponent_len) || (MAX_BYTE_LEN < modulus_len) ||
        (MAX_BYTE_LEN < value_len))
        return PKA_OPERAND_LEN_TOO_LONG;

    if (modulus_len < 5)
        return PKA_OPERAND_LEN_TOO_SHORT;

    // Make sure that value (aka msg) < modulus.
    if (modulus_len < value_len)
        return PKA_OPERAND_VAL_GE_MODULUS;
    else if (modulus_len == value_len)
    {
        if (internal_compare(operands.operands[2].buf_ptr,
                             operands.operands[1].buf_ptr, value_len,
                             big_endian) != PKA_LESS_THAN)
            return PKA_OPERAND_VAL_GE_MODULUS;
    }

    // Check for odd modulus
    if (big_endian)
    {
        if ((operands.operands[1].buf_ptr[modulus_len - 1] & 0x01) == 0)
            return PKA_OPERAND_MODULUS_IS_EVEN;
    }
    else
    {
        if ((operands.operands[1].buf_ptr[0] & 0x01) == 0)
            return PKA_OPERAND_MODULUS_IS_EVEN;
    }

    operands.operand_cnt        = 3;
    operands.shift_amount       = 0;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_MOD_EXP, &operands);
}

// Implements c^d mod p*q -> result.  Instead of d, requires d_p=d mod p-1,
// and d_q=d mod q-1.  Also requires inverse of q mod p, i.e. qinv=q^(-1) mod p.
// Note that q MUST be smaller than p.
gxcr_pka_err_t gxcr_pka_exp_with_crt(gxcr_pka_handle_t *handle,
                                     void              *user_data,
                                     pka_operand_t     *p,
                                     pka_operand_t     *q,
                                     pka_operand_t     *c,
                                     pka_operand_t     *d_p,
                                     pka_operand_t     *d_q,
                                     pka_operand_t     *qinv)
{
    pka_comparison_t comparison;
    pka_operands_t   operands;
    uint32_t         p_len, q_len, c_len, dp_len, dq_len, qinv_len;
    uint8_t          big_endian;

    if ((p == NULL) || (q == NULL) || (c == NULL) || (d_p == NULL) ||
        (d_q == NULL) || (qinv == NULL))
        return PKA_OPERAND_MISSING;

    if ((p->buf_ptr   == NULL) || (q->buf_ptr    == NULL) ||
        (c->buf_ptr   == NULL) || (d_p->buf_ptr  == NULL) ||
        (d_q->buf_ptr == NULL) || (qinv->buf_ptr == NULL))
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0] = *p;
    operands.operands[1] = *q;
    operands.operands[2] = *c;
    operands.operands[3] = *d_p;
    operands.operands[4] = *d_q;
    operands.operands[5] = *qinv;

    big_endian = handle->big_endian;
    p_len      = process_operand(&operands.operands[0], big_endian);
    q_len      = process_operand(&operands.operands[1], big_endian);
    c_len      = process_operand(&operands.operands[2], big_endian);
    dp_len     = process_operand(&operands.operands[3], big_endian);
    dq_len     = process_operand(&operands.operands[4], big_endian);
    qinv_len   = process_operand(&operands.operands[5], big_endian);

    if ((p_len  == 0) || (q_len  == 0) || (c_len    == 0) ||
        (dp_len == 0) || (dq_len == 0) || (qinv_len == 0))
        return PKA_OPERAND_LEN_ZERO;

    if ((OTHER_MAX_BYTE_LEN < p_len)  || (OTHER_MAX_BYTE_LEN < q_len)  ||
        (MAX_BYTE_LEN       < c_len)  || (OTHER_MAX_BYTE_LEN < dp_len) ||
        (OTHER_MAX_BYTE_LEN < dq_len) || (OTHER_MAX_BYTE_LEN < qinv_len))
        return PKA_OPERAND_LEN_TOO_LONG;

    // Check that p_len and q_len > 32 bits
    if ((p_len < 5) || (q_len < 5))
        return PKA_OPERAND_LEN_TOO_SHORT;

    // Check that q < p (i.e. operands.operands[1] < operands.operands[0])
    comparison = pka_compare(&operands.operands[1], &operands.operands[0]);
    if (comparison != PKA_LESS_THAN)
        return PKA_OPERAND_Q_GE_OPERAND_P;

    // Check that c_len <= p_len + q_len? Probably not worth it, so don't
    // bother for now.

    // We could also check that d_p < p, d_q < q and qinv < p, but it is
    // probably not worth it, so don't bother for now.

    // Check for odd modulus (i.e. p and q must be odd, but in common usage
    // p and q will usually be a large prime number and hence odd).
    if (big_endian)
    {
        if (((operands.operands[0].buf_ptr[p_len - 1] & 0x01) == 0) ||
            ((operands.operands[1].buf_ptr[q_len - 1] & 0x01) == 0))
            return PKA_OPERAND_MODULUS_IS_EVEN;
    }
    else
    {
        if (((operands.operands[0].buf_ptr[0] & 0x01) == 0) ||
            ((operands.operands[1].buf_ptr[0] & 0x01) == 0))
            return PKA_OPERAND_MODULUS_IS_EVEN;
    }

    // Don't check that p and q are co-prime (i.e. gcd(p,q) == 1), since it
    // is too expensive.  Also don't check that "((qinv * q) mod p) == 1",
    // since that too, is fairly expensive.

    operands.operand_cnt        = 6;
    operands.shift_amount       = 0;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_EXP_WITH_CRT, &operands);
}

// Implements value^(-1) mod modulus ->result.  modulus must not be 0.
gxcr_pka_err_t gxcr_pka_mod_invert(gxcr_pka_handle_t *handle,
                                   void              *user_data,
                                   pka_operand_t     *value,
                                   pka_operand_t     *modulus)
{
    pka_operands_t operands;
    uint32_t       value_len, modulus_len;
    uint8_t        big_endian;

    if ((value == NULL) || (modulus == NULL))
        return PKA_OPERAND_MISSING;

    if ((value->buf_ptr == NULL) || (modulus->buf_ptr == NULL))
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0] = *value;
    operands.operands[1] = *modulus;

    big_endian  = handle->big_endian;
    value_len   = process_operand(&operands.operands[0], big_endian);
    modulus_len = process_operand(&operands.operands[1], big_endian);

    if ((value_len == 0) || (modulus_len == 0))
        return PKA_OPERAND_LEN_ZERO;

    if ((MAX_BYTE_LEN < value_len) || (MAX_BYTE_LEN < modulus_len))
        return PKA_OPERAND_LEN_TOO_LONG;

    // Check for odd modulus.
    if (big_endian)
    {
        if ((operands.operands[1].buf_ptr[modulus_len - 1] & 0x01) == 0)
            return PKA_OPERAND_MODULUS_IS_EVEN;
    }
    else
    {
        if ((operands.operands[1].buf_ptr[0] & 0x01) == 0)
            return PKA_OPERAND_MODULUS_IS_EVEN;
    }

    operands.operand_cnt        = 2;
    operands.shift_amount       = 0;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_MOD_INVERT, &operands);
}

// Implements pointA + pointB -> pointC on elliptic curve
// "y^2 = x^3 + a*x + b mod p".
gxcr_pka_err_t gxcr_pka_ecc_add(gxcr_pka_handle_t *handle,
                                void              *user_data,
                                ecc_curve_t       *curve,
                                ecc_point_t       *pointA,
                                ecc_point_t       *pointB)
{
    pka_operands_t operands;
    uint32_t       pointA_x_len, pointA_y_len, pointB_x_len, pointB_y_len;
    uint32_t       p_len, a_len, b_len;
    uint8_t        big_endian;

    if ((curve     == NULL) || (pointA    == NULL) || (pointB   == NULL) ||
        (curve->p  == NULL) || (curve->a  == NULL) || (curve->b == NULL) ||
        (pointA->x == NULL) || (pointA->y == NULL) ||
        (pointB->x == NULL) || (pointB->y == NULL))
        return PKA_OPERAND_MISSING;

    if ((curve->p->buf_ptr  == NULL) || (curve->a->buf_ptr  == NULL) ||
        (curve->b->buf_ptr  == NULL) ||
        (pointA->x->buf_ptr == NULL) || (pointA->y->buf_ptr == NULL) ||
        (pointB->x->buf_ptr == NULL) || (pointB->y->buf_ptr == NULL))
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0] = *pointA->x;
    operands.operands[1] = *pointA->y;
    operands.operands[2] = *pointB->x;
    operands.operands[3] = *pointB->y;
    operands.operands[4] = *curve->p;
    operands.operands[5] = *curve->a;
    operands.operands[6] = *curve->b;

    big_endian   = handle->big_endian;
    pointA_x_len = process_operand(&operands.operands[0], big_endian);
    pointA_y_len = process_operand(&operands.operands[1], big_endian);
    pointB_x_len = process_operand(&operands.operands[2], big_endian);
    pointB_y_len = process_operand(&operands.operands[3], big_endian);
    p_len        = process_operand(&operands.operands[4], big_endian);
    a_len        = process_operand(&operands.operands[5], big_endian);
    b_len        = process_operand(&operands.operands[6], big_endian);

    if ((pointA_x_len == 0) || (pointA_y_len == 0) ||
        (pointB_x_len == 0) || (pointB_y_len == 0) ||
        (p_len        == 0) || (a_len        == 0) ||
        (b_len        == 0))
        return PKA_OPERAND_LEN_ZERO;

    // We could check that pointA and pointB are "on" the given curve, but
    // that is deemed too expensive for now.

    operands.operand_cnt        = 7;
    operands.shift_amount       = 0;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_ECC_ADD, &operands);
}

// Implements multiplier * pointA -> pointC on elliptic curve
// "y^2 = x^3 + a*x + b mod p".
gxcr_pka_err_t gxcr_pka_ecc_multiply(gxcr_pka_handle_t *handle,
                                     void              *user_data,
                                     ecc_curve_t       *curve,
                                     ecc_point_t       *pointA,
                                     pka_operand_t     *multiplier)
{
    pka_operands_t operands;
    uint32_t       pointA_x_len, pointA_y_len, p_len, a_len, b_len, k_len;
    uint8_t        big_endian;

    if ((curve     == NULL) || (pointA    == NULL) ||
        (curve->p  == NULL) || (curve->a  == NULL) || (curve->b   == NULL) ||
        (pointA->x == NULL) || (pointA->y == NULL) || (multiplier == NULL))
        return PKA_OPERAND_MISSING;

    if ((curve->p->buf_ptr  == NULL) || (curve->a->buf_ptr   == NULL) ||
        (curve->b->buf_ptr  == NULL) || (multiplier->buf_ptr == NULL) ||
        (pointA->x->buf_ptr == NULL) || (pointA->y->buf_ptr  == NULL))
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0] = *multiplier;
    operands.operands[1] = *pointA->x;
    operands.operands[2] = *pointA->y;
    operands.operands[3] = *curve->p;
    operands.operands[4] = *curve->a;
    operands.operands[5] = *curve->b;

    big_endian   = handle->big_endian;
    k_len        = process_operand(&operands.operands[0], big_endian);
    pointA_x_len = process_operand(&operands.operands[1], big_endian);
    pointA_y_len = process_operand(&operands.operands[2], big_endian);
    p_len        = process_operand(&operands.operands[3], big_endian);
    a_len        = process_operand(&operands.operands[4], big_endian);
    b_len        = process_operand(&operands.operands[5], big_endian);

    if ((pointA_x_len == 0) || (pointA_y_len == 0) ||
        (p_len        == 0) || (a_len        == 0) ||
        (b_len        == 0) || (k_len        == 0))
        return PKA_OPERAND_LEN_ZERO;

    // We could check that pointA is "on" the given curve, but that is deemed
    // too expensive for now.

    operands.operand_cnt        = 6;
    operands.shift_amount       = 0;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_ECC_MULTIPLY, &operands);
}



// Implements the Elliptic Curve DSA Signature Generation algorithm.
// p = curve modulus, a large prime
// a, b are the curve parameters < p
// curve is y^2 = x^3 + a*x + b mod p
// base_pt = (base_point_x, base_point_y), a valid point on the curve
// base_pt_order = order of base_pt, i.e. base_pt_order * base_pt =
//                                                            point at infinity.
// hash= SHA hash of the signed document, where hash < base_pt_order
// k = random parameter < base_pt_order
// private_key < base_pt_order, where public_key = private_key * base_pt
// define (x,y) = k * base_pt
// outputs r < base_pt_order and s < base_pt_order
// where r = x mod base_pt_order and
// s = kinv * (hash + r*private_key) mod base_pt_order

gxcr_pka_err_t gxcr_pka_ecdsa_gen(gxcr_pka_handle_t *handle,
                                  void              *user_data,
                                  ecc_curve_t       *curve,
                                  ecc_point_t       *base_pt,
                                  pka_operand_t     *base_pt_order,
                                  pka_operand_t     *private_key,
                                  pka_operand_t     *hash,
                                  pka_operand_t     *k)
{
    pka_operands_t operands;
    uint32_t       base_point_x_len, base_point_y_len, k_len, alpha_len;
    uint32_t       h_len, p_len, a_len, b_len, n_len;
    uint8_t        big_endian;

    if ((curve         == NULL) || (base_pt     == NULL) ||
        (curve->p      == NULL) || (curve->a    == NULL) ||
        (curve->b      == NULL) ||
        (base_pt->x    == NULL) || (base_pt->y  == NULL) ||
        (base_pt_order == NULL) || (private_key == NULL) ||
        (hash          == NULL) || (k           == NULL))
        return PKA_OPERAND_MISSING;

    if ((curve->p->buf_ptr    == NULL) || (curve->a->buf_ptr      == NULL) ||
        (curve->b->buf_ptr    == NULL) || (base_pt_order->buf_ptr == NULL) ||
        (base_pt->x->buf_ptr  == NULL) || (base_pt->y->buf_ptr    == NULL) ||
        (private_key->buf_ptr == NULL) || (hash->buf_ptr          == NULL) ||
        (k->buf_ptr           == NULL))
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0] = *base_pt->x;
    operands.operands[1] = *base_pt->y;
    operands.operands[2] = *k;
    operands.operands[3] = *private_key;
    operands.operands[4] = *hash;
    operands.operands[5] = *curve->p;
    operands.operands[6] = *curve->a;
    operands.operands[7] = *curve->b;
    operands.operands[8] = *base_pt_order;

    big_endian       = handle->big_endian;
    base_point_x_len = process_operand(&operands.operands[0], big_endian);
    base_point_y_len = process_operand(&operands.operands[1], big_endian);
    k_len            = process_operand(&operands.operands[2], big_endian);
    alpha_len        = process_operand(&operands.operands[3], big_endian);
    h_len            = process_operand(&operands.operands[4], big_endian);
    p_len            = process_operand(&operands.operands[5], big_endian);
    a_len            = process_operand(&operands.operands[6], big_endian);
    b_len            = process_operand(&operands.operands[7], big_endian);
    n_len            = process_operand(&operands.operands[8], big_endian);

    if ((base_point_x_len == 0) || (base_point_y_len == 0) ||
        (k_len            == 0) || (alpha_len        == 0) ||
        (h_len            == 0) || (p_len            == 0) ||
        (a_len            == 0) || (b_len            == 0) ||
        (n_len            == 0))
        return PKA_OPERAND_LEN_ZERO;

    // We could check that base_point_x < p, base_point_y < p, a < p, b < p,
    // base_pt_order < p, k < base_pt_order, and hash < base_pt_order, but
    // these are all too expensive for now.

    operands.operand_cnt        = 9;
    operands.shift_amount       = 0;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_ECDSA_GEN, &operands);
}



// Implements the Elliptic Curve DSA Signature Verification algorithm.
// p = curve modulus, a large prime.
// a, b are the curve parameters < p
// curve is y^2 = x^3 + a*x + b mod p
// base_pt = (base_point_x, base_point_y), a valid point on the curve
// base_pt_order = order of base_pt, i.e. base_pt_order * base_pt =
//                                                            point at infinity.
// hash= SHA hash of the signed document, where hash < base_pt_order
// k = random parameter < base_pt_order
// public_key < base_pt_order, where public_key = private_key * base_pt
// define (x,y) = k * base_pt.
// outputs r < base_pt_order and s < base_pt_order
// where r = x mod base_pt_order and
// s = kinv * (hash + r*private_key) mod base_pt_order

gxcr_pka_err_t gxcr_pka_ecdsa_verify(gxcr_pka_handle_t *handle,
                                     void              *user_data,
                                     ecc_curve_t       *curve,
                                     ecc_point_t       *base_pt,
                                     pka_operand_t     *base_pt_order,
                                     ecc_point_t       *public_key,
                                     pka_operand_t     *hash,
                                     dsa_signature_t   *signature)
{
    pka_operands_t operands;
    uint32_t       base_point_x_len, base_point_y_len;
    uint32_t       public_point_x_len, public_point_y_len, h_len, p_len;
    uint32_t       a_len, b_len, n_len, r_len, s_len;
    uint8_t        big_endian;

    if ((curve         == NULL) || (base_pt       == NULL) ||
        (public_key    == NULL) || (signature     == NULL) ||
        (curve->p      == NULL) || (curve->a      == NULL) ||
        (curve->b      == NULL) || (base_pt_order == NULL) ||
        (base_pt->x    == NULL) || (base_pt->y    == NULL) ||
        (public_key->x == NULL) || (public_key->y == NULL) ||
        (hash          == NULL) || (signature->r  == NULL) ||
        (signature->s  == NULL))
        return PKA_OPERAND_MISSING;

    if ((curve->p->buf_ptr      == NULL) || (curve->a->buf_ptr      == NULL) ||
        (curve->b->buf_ptr      == NULL) || (base_pt_order->buf_ptr == NULL) ||
        (base_pt->x->buf_ptr    == NULL) || (base_pt->y->buf_ptr    == NULL) ||
        (public_key->x->buf_ptr == NULL) || (public_key->y->buf_ptr == NULL) ||
        (hash->buf_ptr          == NULL) || (signature->r->buf_ptr  == NULL) ||
        (signature->s->buf_ptr  == NULL))
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0]  = *base_pt->x;
    operands.operands[1]  = *base_pt->y;
    operands.operands[2]  = *public_key->x;
    operands.operands[3]  = *public_key->y;
    operands.operands[4]  = *hash;
    operands.operands[5]  = *curve->p;
    operands.operands[6]  = *curve->a;
    operands.operands[7]  = *curve->b;
    operands.operands[8]  = *base_pt_order;
    operands.operands[9]  = *signature->r;
    operands.operands[10] = *signature->s;

    big_endian         = handle->big_endian;
    base_point_x_len   = process_operand(&operands.operands[0], big_endian);
    base_point_y_len   = process_operand(&operands.operands[1], big_endian);
    public_point_x_len = process_operand(&operands.operands[2], big_endian);
    public_point_y_len = process_operand(&operands.operands[3], big_endian);
    h_len              = process_operand(&operands.operands[4], big_endian);
    p_len              = process_operand(&operands.operands[5], big_endian);
    a_len              = process_operand(&operands.operands[6], big_endian);
    b_len              = process_operand(&operands.operands[7], big_endian);
    n_len              = process_operand(&operands.operands[8], big_endian);
    r_len              = process_operand(&operands.operands[9], big_endian);
    s_len              = process_operand(&operands.operands[10],big_endian);

    if ((base_point_x_len   == 0) || (base_point_y_len   == 0) ||
        (public_point_x_len == 0) || (public_point_y_len == 0) ||
        (h_len              == 0) || (p_len              == 0) ||
        (a_len              == 0) || (b_len              == 0) ||
        (n_len              == 0) || (r_len              == 0) ||
        (s_len              == 0))
        return PKA_OPERAND_LEN_ZERO;

    // We could check that base_point_x < p, base_point_y < p, a < p, b < p,
    // base_pt_order < p, public_point_x < p, public_point_y < p,
    // hash < base_pt_order, r < base_pt_order and s < base_pt_order but
    // these are all too expensive for now.

    operands.operand_cnt        = 11;
    operands.shift_amount       = 0;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_ECDSA_VERIFY, &operands);
}



// Implements the Digital Signature Generation Algorithm (FIPS-186).
// Requirements.
// (1) p is a large prime (usually >= 1024 bits long)
// (2) q < p, (in fact q is MUCH smaller than p and < 2^256)
// (3) q must divide (p-1)
// (4) 1 < g < p
// (5) g^q mod p = 1
// (6) 0 < private_key < q
// (7) hash < q
// (8) 0 < k < q
// (9) outputs r < q and s < q

gxcr_pka_err_t gxcr_pka_dsa_gen(gxcr_pka_handle_t *handle,
                                void              *user_data,
                                pka_operand_t     *p,
                                pka_operand_t     *q,
                                pka_operand_t     *g,
                                pka_operand_t     *private_key,
                                pka_operand_t     *hash,
                                pka_operand_t     *k)
{
    pka_operands_t operands;
    uint32_t       p_len, g_len, q_len, hash_len, k_len, private_key_len;
    uint8_t        big_endian;

    if ((p           == NULL) || (q    == NULL) || (g == NULL) ||
        (private_key == NULL) || (hash == NULL) || (k == NULL))
        return PKA_OPERAND_MISSING;

    if ((p->buf_ptr     == NULL) || (q->buf_ptr           == NULL) ||
        (g->buf_ptr     == NULL) || (private_key->buf_ptr == NULL) ||
        (hash->buf_ptr  == NULL) || (k->buf_ptr           == NULL))
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0] = *p;
    operands.operands[1] = *g;
    operands.operands[2] = *q;
    operands.operands[3] = *hash;
    operands.operands[4] = *k;
    operands.operands[5] = *private_key;

    big_endian      = handle->big_endian;
    p_len           = process_operand(&operands.operands[0], big_endian);
    g_len           = process_operand(&operands.operands[1], big_endian);
    q_len           = process_operand(&operands.operands[2], big_endian);
    hash_len        = process_operand(&operands.operands[3], big_endian);
    k_len           = process_operand(&operands.operands[4], big_endian);
    private_key_len = process_operand(&operands.operands[5], big_endian);

    if ((p_len == 0) || (g_len           == 0) ||
        (q_len == 0) || (hash_len        == 0) ||
        (k_len == 0) || (private_key_len == 0))
        return PKA_OPERAND_LEN_ZERO;

    // We could check that q < p, g < p, k < q, and hash < q, but
    // these are all too expensive for now.

    operands.operand_cnt        = 6;
    operands.shift_amount       = 0;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_DSA_GEN, &operands);
}



// Implements the Digital Signature Verification Algorithm (FIPS-186).
// Requirements.
// (1) p is a large prime (usually >= 1024 bits long)
// (2) q < p, (in fact q is MUCH smaller than p and < 2^256)
// (3) q must divide (p-1)
// (4) 1 < g < p
// (5) g^q mod p = 1
// (6) 0 < public_key < p
// (7) hash < q
// (8) public_key = g^private_key mod p, therefore public_key < p
// (9) r < q and s < q

gxcr_pka_err_t gxcr_pka_dsa_verify(gxcr_pka_handle_t *handle,
                                   void              *user_data,
                                   pka_operand_t     *p,
                                   pka_operand_t     *q,
                                   pka_operand_t     *g,
                                   pka_operand_t     *public_key,
                                   pka_operand_t     *hash,
                                   dsa_signature_t   *signature)
{
    pka_operands_t operands;
    uint32_t       p_len, g_len, q_len, hash_len, public_key_len;
    uint32_t       r_len, s_len;
    uint8_t        big_endian;

    if ((p == NULL) || (q == NULL) || (g == NULL) || (hash == NULL) ||
        (public_key == NULL) || (signature->r == NULL) ||
        (signature->s == NULL))
        return PKA_OPERAND_MISSING;

    if ((p->buf_ptr            == NULL) || (q->buf_ptr            == NULL) ||
        (g->buf_ptr            == NULL) || (hash->buf_ptr         == NULL) ||
        (public_key->buf_ptr   == NULL) || (signature->r->buf_ptr == NULL) ||
        (signature->s->buf_ptr == NULL))
        return PKA_OPERAND_BUF_MISSING;

    operands.operands[0] = *p;
    operands.operands[1] = *g;
    operands.operands[2] = *q;
    operands.operands[3] = *hash;
    operands.operands[4] = *public_key;
    operands.operands[5] = *signature->r;
    operands.operands[6] = *signature->s;

    big_endian     = handle->big_endian;
    p_len          = process_operand(&operands.operands[0], big_endian);
    g_len          = process_operand(&operands.operands[1], big_endian);
    q_len          = process_operand(&operands.operands[2], big_endian);
    hash_len       = process_operand(&operands.operands[3], big_endian);
    public_key_len = process_operand(&operands.operands[4], big_endian);
    r_len          = process_operand(&operands.operands[5], big_endian);
    s_len          = process_operand(&operands.operands[6], big_endian);

    if ((p_len          == 0) || (g_len    == 0) ||
        (q_len          == 0) || (hash_len == 0) ||
        (public_key_len == 0) || (r_len    == 0) ||
        (s_len          == 0))
        return PKA_OPERAND_LEN_ZERO;

    // We could check that q < p, g < p, public_key < p, hash < q, r < q, and
    // s < q but these are all too expensive for now.

    operands.operand_cnt        = 7;
    operands.shift_amount       = 0;
    operands.encrypt_results[0] = 0;
    operands.encrypt_results[1] = 0;
    return gxcr_pka_submit_op(handle, user_data, PKA_DSA_VERIFY, &operands);
}



static uint32_t append_to_fifo(gxcr_pka_handle_t      *handle,
                               client_operands_desc_t *fifo_desc,
                               void                   *data_ptr,
                               uint32_t                byte_len,
                               uint8_t                 big_endian,
                               uint8_t                *wrapsPtr)
{
    uint32_t rounded_len, tail_offset, dst_offset, copy_len;
    uint8_t *dst_ptr, *src_ptr, wraps;

    // Always preserve 8-byte alignment of the operands in the fifo, by making
    // sure tail_offset is a multiple of 8, and by doing a final memset of
    // "rounded_len - byte_len" bytes.
    src_ptr     = data_ptr;
    rounded_len = ROUNDUP(byte_len, 8);
    tail_offset = fifo_desc->tail_offset;
    Assert((tail_offset & 0x7) == 0);
    Assert(tail_offset < fifo_desc->max_size);

    dst_offset = fifo_desc->base_offset + tail_offset;
    dst_ptr    = handle->master_ptr + dst_offset;

    // Do we need to split into two memcpy's?
    if (fifo_desc->max_size < (tail_offset + byte_len))
    {
        // First memcpy.  Note that copy_len MUST be a multiple of 8 here!
        copy_len = fifo_desc->max_size - tail_offset;
        if (! big_endian)
        {
            memcpy(dst_ptr, src_ptr, copy_len);
            src_ptr += copy_len;
        }
        else
            byte_swap_cpy(dst_ptr, src_ptr + (byte_len - copy_len), copy_len);

        tail_offset = 0;
        dst_ptr     = handle->master_ptr + fifo_desc->base_offset;
        copy_len    = byte_len - copy_len;
        wraps       = 1;
    }
    else
    {
        copy_len = byte_len;
        wraps    = 0;
    }

    if (! big_endian)
        memcpy(dst_ptr, src_ptr, copy_len);
    else
        byte_swap_cpy(dst_ptr, src_ptr, copy_len);

    dst_ptr     += copy_len;
    tail_offset += copy_len;
    if (rounded_len != byte_len)
    {
        memset(dst_ptr, 0, rounded_len - byte_len);
        tail_offset += rounded_len - byte_len;
    }

    if (tail_offset == fifo_desc->max_size)
        tail_offset = 0;

    *wrapsPtr              = wraps;
    fifo_desc->tail_offset = tail_offset;
    return dst_offset;
}

void copy_operand_parts(uint8_t                *dst_ptr,
                        result_operand_parts_t *operand_parts,
                        uint8_t                 big_endian)
{
    uint32_t copy_len, total_len, rounded_len, second_part_copy_len;
    uint8_t *first_part_ptr, *second_part_ptr;

    // First (and possibly only) memcpy.
    copy_len        = operand_parts->first_part_len;
    first_part_ptr  = operand_parts->first_part_ptr;
    second_part_ptr = operand_parts->second_part_ptr;
    total_len       = operand_parts->total_len;
    rounded_len     = ROUNDUP(total_len, 8);

    if (total_len < rounded_len)
        // Zero out the entire dst area - more than we need, but safe for now.
        memset(dst_ptr, 0, rounded_len);

    if (second_part_ptr != NULL)
        // Note that copy_len MUST be a multiple of 8 here!
        Assert((copy_len & 7) == 0);

    if (! big_endian)
    {
        memcpy(dst_ptr, first_part_ptr, copy_len);
        dst_ptr += copy_len;
    }
    else
        byte_swap_cpy(dst_ptr + (total_len - copy_len), first_part_ptr,
                      copy_len);

    if (second_part_ptr == NULL)
        return;

    second_part_copy_len = operand_parts->second_part_len;
    if (! big_endian)
        memcpy(dst_ptr, second_part_ptr, second_part_copy_len);
    else
        byte_swap_cpy(dst_ptr, second_part_ptr, second_part_copy_len);
}

void get_result_src_ptrs(gxcr_pka_handle_t        *handle,
                         client_results_desc_t    *fifo_desc,
                         pka_driver_result_desc_t *result_desc,
                         result_ptrs_t            *result_ptrs)
{
    result_operand_parts_t *operand_parts;
    uint32_t                result_cnt, base_offset, head_offset, max_size;
    uint32_t                result_idx, src_offset, byte_len;
    uint32_t                first_part_len, second_part_len;
    uint8_t                *master_ptr;

    result_cnt  = result_desc->result_cnt;
    base_offset = fifo_desc->base_offset;
    head_offset = fifo_desc->head_offset;
    max_size    = fifo_desc->max_size;
    master_ptr  = handle->master_ptr;
    Assert((head_offset & 0x7) == 0);
    Assert(head_offset < fifo_desc->max_size);

    // Null out the first_part_ptr and second_part_ptr for the second result,
    // so that they won't have stale values when result_cnt is 1.
    result_ptrs->result_parts[1].first_part_ptr  = NULL;
    result_ptrs->result_parts[1].second_part_ptr = NULL;

    for (result_idx = 0;  result_idx < result_cnt;  result_idx++)
    {
        src_offset  = result_desc->result[result_idx].offset;
        byte_len    = result_desc->result[result_idx].byte_len;
        if (src_offset != (base_offset + head_offset))
            printf("get_result_src_ptrs src_offset=0x%X base=0x%X "
                   "head=0x%X sum=0x%X\n",
                   src_offset, base_offset, head_offset,
                   base_offset + head_offset);

        operand_parts = &result_ptrs->result_parts[result_idx];
        operand_parts->first_part_ptr = master_ptr + src_offset;
        operand_parts->total_len      = byte_len;

        if (max_size < (head_offset + byte_len))
        {
            first_part_len                 = max_size - head_offset;
            second_part_len                = byte_len - first_part_len;
            operand_parts->first_part_len  = first_part_len;
            operand_parts->second_part_ptr = master_ptr + base_offset;
            operand_parts->second_part_len = second_part_len;
            head_offset = ROUNDUP(second_part_len, 8);
        }
        else
        {
            operand_parts->first_part_len  = byte_len;
            operand_parts->second_part_ptr = NULL;
            operand_parts->second_part_len = 0;
            head_offset                   += byte_len;
            head_offset                    = ROUNDUP(head_offset, 8);
        }
    }
}

#if 0

void copy_from_result_fifo(gxcr_pka_handle_t     *handle,
                           client_results_desc_t *fifo_desc,
                           void                  *result_buf_ptr,
                           uint32_t               byte_len,
                           uint8_t                big_endian)
{
    uint32_t rounded_len, head_offset, copy_len;
    uint8_t *dst_ptr, *src_ptr, wraps;

    // Always preserve 8-byte alignment of the operands in the fifo.
    dst_ptr     = result_buf_ptr;
    rounded_len = ROUNDUP(byte_len, 8);
    head_offset = fifo_desc->head_offset;
    src_ptr     = handle->master_ptr + (fifo_desc->base_offset + head_offset);
    Assert((head_offset & 0x7) == 0);
    Assert(head_offset < fifo_desc->max_size);

    // Zero out bytes at the end.
    if (byte_len < rounded_len)
        memset(dst_ptr + byte_len, 0, rounded_len - byte_len);

    // Do we need to split into two memcpy's?
    if (fifo_desc->max_size < (head_offset + byte_len))
    {
        // First memcpy.  Note that copy_len MUST be a multiple of 8 here!
        copy_len = fifo_desc->max_size - head_offset;
        if (! big_endian)
        {
            memcpy(dst_ptr, src_ptr, copy_len);
            dst_ptr += copy_len;
        }
        else
            byte_swap_cpy(dst_ptr + (byte_len - copy_len), src_ptr,
                          copy_len);

        head_offset = 0;
        src_ptr     = handle->master_ptr + fifo_desc->base_offset;
        copy_len    = byte_len - copy_len;
        wraps       = 1;
    }
    else
    {
        copy_len = byte_len;
        wraps    = 0;
    }

    if (! big_endian)
        memcpy(dst_ptr, src_ptr, copy_len);
    else
        byte_swap_cpy(dst_ptr, src_ptr, copy_len);

    head_offset += copy_len;
    head_offset  = ROUNDUP(head_offset, 8);
    if (head_offset == fifo_desc->max_size)
        head_offset = 0;

    fifo_desc->head_offset = head_offset;
}

#endif



gxcr_pka_err_t gxcr_pka_submit_op(gxcr_pka_handle_t *handle,
                                  void              *user_data,
                                  pka_opcode_t       opcode,
                                  pka_operands_t    *operands)
{
    client_operands_desc_t *operand_ring_desc;
    pka_driver_cmd_desc_t   cmd_desc;
    master_record_t        *master_record;
    len_offset_t            len_offs[MAX_OPERAND_CNT];
    pka_operand_t          *operand;
    uint32_t                operand_cnt, operand_sizes, byte_len, operand_off;
    uint32_t                operand_idx, orig_tail_offset, client_idx, epoch;
    uint32_t                len_offs_offset, total_operand_ring_used;
    uint32_t                tail_delta;
    uint8_t                *buf_ptr, encrypted, big_endian, wraps;
    int                     rc;

    // First see if the driver is too full to accept any more requests.
    master_record = (master_record_t *) handle->master_ptr;
    if (master_record->dont_accept_new_reqs)
        return PKA_DRIVER_TOO_BUSY;

    // Next make sure that at least the operand_cnt makes sense.
    operand_cnt = OPERAND_CNT_TBL[opcode];
    if (operands->operand_cnt != operand_cnt)
        return PKA_BAD_OPERAND_CNT;

    // *TBD* Check our epoch to the one in the master record.
    client_idx = handle->client_idx;
    epoch      = handle->epoch;
#if 0
    if (epoch != master_record->client_idxs.client_descs[client_idx].epoch)
        return -1;  // *TBD internal error?
#endif

    // Now add up the operand sizes
    operand_sizes = 0;
    operand_cnt   = operands->operand_cnt;
    for (operand_idx = 0;  operand_idx < operand_cnt;  operand_idx++)
    {
        byte_len       = operands->operands[operand_idx].actual_len;
        operand_sizes += ROUNDUP(byte_len, 8);
    }

    // Include the variable length array len_offs in the operand_sizes.
    total_operand_ring_used = operand_sizes +
                                  (operand_cnt * sizeof(len_offset_t));

    // Next see if there is any room in the operand ring.  If not then bail.
    operand_ring_desc = (client_operands_desc_t *) handle->operand_ring_desc;
    if (operand_ring_desc->max_size <=
          (operand_ring_desc->curr_size + total_operand_ring_used))
        return PKA_OPERAND_FIFO_FULL;

    // Remember the tail_offset here, in case we have to undo our appends if
    // the enqueue of the cmd itself fails later.
    orig_tail_offset = operand_ring_desc->tail_offset;

    // Next append the operands to the operand ring.
    operand_cnt = operands->operand_cnt;
    for (operand_idx = 0;  operand_idx < operand_cnt;  operand_idx++)
    {
        operand     = &operands->operands[operand_idx];
        big_endian  = operand->big_endian;
        buf_ptr     = operand->buf_ptr;
        byte_len    = operand->actual_len;
        encrypted   = operand->is_encrypted;
        operand_off = append_to_fifo(handle, operand_ring_desc, buf_ptr,
                                     byte_len, big_endian, &wraps);
        len_offs[operand_idx].offset    = operand_off;
        len_offs[operand_idx].byte_len  = byte_len;
        len_offs[operand_idx].encrypted = encrypted;
        len_offs[operand_idx].wraps     = wraps;
    }

    // Next append the len_offsets structure to the operand ring.
    len_offs_offset = append_to_fifo(handle, operand_ring_desc, &len_offs[0],
                                     operand_cnt * sizeof(len_offset_t),
                                     0, &wraps);

    len_offs_offset -= operand_ring_desc->base_offset;

    // Now append the cmd using TMC_QUEUE.  Note that this enqueue could also
    // fail, though rarely, since it means requests are coming to the driver
    // faster than the driver can do its minimal processing and requeue it to
    // the internal cmd queue.  Also, initially the tag field just holds the
    // client_idx.  This is updated with more information later.
    memset(&cmd_desc, 0, sizeof (cmd_desc));
    cmd_desc.user_data                  = user_data;
    cmd_desc.offset_of_operand_len_offs = len_offs_offset;
    cmd_desc.operands_size              = operand_sizes;
    cmd_desc.tag                        = client_idx;
    cmd_desc.epoch                      = epoch;
    cmd_desc.opcode                     = opcode;
    cmd_desc.shift_cnt                  = operands->shift_amount;
    cmd_desc.operand_cnt                = operand_cnt;
    cmd_desc.client_idx                 = client_idx;
    if (handle->trace_cnt != 0)
    {
        cmd_desc.trace_flag = TRUE;
        handle->trace_cnt--;
    }

    Assert((cmd_desc.trace_flag == 0) || (cmd_desc.trace_flag == 1));

    __insn_mf();

    rc = pka_cmd_enqueue((pka_cmd_t *) handle->cmd_ring, cmd_desc);
    if (rc < 0)
    {
        // We failed on our latest step.  Need to undo the changes to the
        // operand ring.
        operand_ring_desc->tail_offset = orig_tail_offset;
        return PKA_CMD_RING_FULL;
    }

    if (orig_tail_offset < operand_ring_desc->tail_offset)
    {
        tail_delta = operand_ring_desc->tail_offset - orig_tail_offset;
        if (tail_delta != total_operand_ring_used)
        {
            printf("tail_offset 0x%X -> 0x%X delta=0x%X ring_used=0x%X\n",
                   orig_tail_offset, operand_ring_desc->tail_offset,
                   tail_delta, total_operand_ring_used);
            Assert(FALSE);
        }
    }

    arch_atomic_add(&operand_ring_desc->curr_size, total_operand_ring_used);
    if (operand_ring_desc->peak_size < operand_ring_desc->curr_size)
        operand_ring_desc->peak_size = operand_ring_desc->curr_size;

    operand_ring_desc->bytes_added += total_operand_ring_used;

    // Finally signal the driver that they should look at my cmd Ring.
    // arch_atomic_bit_set(handle->notifyWord, handle->client_idx);
    __insn_mf();
    return PKA_NO_ERROR;
}



// Returns TRUE if gxcr_get_reply_head was successful in refilling the
// head_reply.  When it returns TRUE handle->head_reply_avail is also set.
// Returns FALSE if pka_reply_dequeue failed, potentially indicating a
// problem with reply_queue_size->queue_cnt.

static boolean_t gxcr_get_reply_head(gxcr_pka_handle_t *handle)
{
    pka_driver_result_desc_t *result_desc, *prev_result_desc;
    driver_results_desc_t    *driver_desc;
    client_results_desc_t    *client_desc;
    queue_size_t             *reply_queue_size;
    pka_opcode_t              opcode;
    pka_reply_t              *reply_queue;
    uint32_t                  bytesAdded, bytesRemoved, queueCnt;
    uint8_t                   compare_result;
    int                       rc;

    prev_result_desc = (pka_driver_result_desc_t *) &handle->prev_reply_desc;
    result_desc      = (pka_driver_result_desc_t *) &handle->head_reply_desc;
    reply_queue_size = (queue_size_t *) handle->reply_queue_size;
    reply_queue      = (pka_reply_t *)  handle->reply_ring;
    Assert(handle->head_reply_avail == 0);
    Assert(reply_queue_size->queue_cnt != 0);

    *prev_result_desc = *result_desc;
    rc = pka_reply_dequeue(reply_queue, result_desc);
    if (rc != 0)
    {
        // Somethings wrong.  Try to resynch by reducing queue_cnt by 1.
        driver_desc = (driver_results_desc_t *) handle->driver_results;
        client_desc = (client_results_desc_t *) handle->result_ring_desc;
        __insn_mf();
        bytesAdded   = (uint32_t) driver_desc->bytes_added;
        bytesRemoved = (uint32_t) client_desc->bytes_removed;
        queueCnt     = (uint32_t) reply_queue_size->queue_cnt;
        printf("gxcr_get_reply_head resynch cnt=%u added=%u removed=%u\n",
               queueCnt, bytesAdded, bytesRemoved);
        if (reply_queue_size->queue_cnt != 0)
        {
            arch_atomic_decrement(&reply_queue_size->queue_cnt);
            __insn_mf();
        }
        return FALSE;
    }

    // Set result_head is avail.
    handle->head_reply_avail = 1;
    arch_atomic_decrement(&reply_queue_size->queue_cnt);
    __insn_mf();

    opcode         = result_desc->opcode;
    compare_result = result_desc->compare_result;

    if ((opcode != PKA_COMPARE)      &&
        (opcode != PKA_ECDSA_VERIFY) &&
        (opcode != PKA_DSA_VERIFY))
        result_desc->compare_result = PKA_NO_COMPARE;
    else if (compare_result == 2)
        result_desc->compare_result = PKA_LESS_THAN;
    else if (compare_result == 1)
        result_desc->compare_result = PKA_EQUAL;
    else if (compare_result == 4)
        result_desc->compare_result = PKA_GREATER_THAN;
    else
        result_desc->compare_result = PKA_NO_COMPARE;

    return TRUE;
}

void gxcr_pka_wait_for_results(gxcr_pka_handle_t *handle)
{
    queue_size_t *reply_queue_size;

    if (handle->head_reply_avail != 0)
        return;

    reply_queue_size = (queue_size_t *) handle->reply_queue_size;
    if (reply_queue_size->queue_cnt != 0)
        if (gxcr_get_reply_head(handle))
            return;

    while (TRUE)
    {
        // Wait for a short while (~300 cycles) between attempts to read the
        // curr_size.
        busy_wait_delay();

        if (reply_queue_size->queue_cnt != 0)
            if (gxcr_get_reply_head(handle))
                return;
    }
}

int gxcr_pka_has_results(gxcr_pka_handle_t *handle)
{
    queue_size_t *reply_queue_size;

    if (handle->head_reply_avail != 0)
        return 1;

    __insn_mf();
    reply_queue_size = (queue_size_t *) handle->reply_queue_size;
    if (reply_queue_size->queue_cnt == 0)
        return 0;

    if (gxcr_get_reply_head(handle))
        return 1;
    else
        return 0;
}

gxcr_pka_err_t gxcr_pka_pop_result_fifo(gxcr_pka_handle_t *handle)
{
    pka_driver_result_desc_t *result_desc;
    client_results_desc_t    *result_ring_desc;
    queue_size_t             *reply_queue_size;
    uint32_t                  result_cnt, base_offset, max_size, head_offset;
    uint32_t                  final_src_offset, final_actual_len;
    uint32_t                  new_head_offset, bytes_consumed;

    if (gxcr_pka_has_results(handle) == 0)
        return PKA_RESULT_FIFO_EMPTY;

    result_desc      = (pka_driver_result_desc_t *) &handle->head_reply_desc;
    result_ring_desc = (client_results_desc_t    *) handle->result_ring_desc;
    result_cnt       = result_desc->result_cnt;
    reply_queue_size = (queue_size_t *) handle->reply_queue_size;

    if (result_cnt != 0)
    {
        base_offset      = result_ring_desc->base_offset;
        max_size         = result_ring_desc->max_size;
        head_offset      = result_ring_desc->head_offset;
        final_src_offset = result_desc->result[result_cnt - 1].offset;
        final_actual_len = result_desc->result[result_cnt - 1].byte_len;

        // Determine new head_offset and number of bytes consumed.
        new_head_offset = (final_src_offset - base_offset) +
                              ROUNDUP(final_actual_len, 8);
        if (max_size <= new_head_offset)
            new_head_offset -= max_size;

        if (head_offset < new_head_offset)
            bytes_consumed = new_head_offset - head_offset;
        else
            bytes_consumed = new_head_offset + (max_size - head_offset);

        arch_atomic_sub(handle->results_size_ptr, bytes_consumed);
        result_ring_desc->bytes_removed += bytes_consumed;
        result_ring_desc->head_offset    = new_head_offset;
    }

    // Clear head_reply_avail, then see if the reply_queue_size != 0 so that
    // we can potential refill the head_reply.
    handle->head_reply_avail = 0;
    if (reply_queue_size->queue_cnt != 0)
        gxcr_get_reply_head(handle);

    return 0;
}

gxcr_pka_err_t gxcr_pka_get_results(gxcr_pka_handle_t *handle,
                                    pka_results_t     *results)
{
    pka_driver_result_desc_t *result_desc;
    client_results_desc_t    *result_ring_desc;
    result_operand_parts_t   *operand_parts;
    result_ptrs_t             result_ptrs;
    uint32_t                  result_cnt, result_idx, buf_len, actual_len;
    uint8_t                  *buf_ptr;

    if (gxcr_pka_has_results(handle) == 0)
        gxcr_pka_wait_for_results(handle);

    result_desc      = (pka_driver_result_desc_t *) &handle->head_reply_desc;
    result_ring_desc = (client_results_desc_t    *) handle->result_ring_desc;
    result_cnt       = result_desc->result_cnt;

    results->user_data      = result_desc->user_data;
    results->opcode         = result_desc->opcode;
    results->result_cnt     = result_cnt;
    results->status         = result_desc->status;
    results->compare_result = result_desc->compare_result;

    get_result_src_ptrs(handle, result_ring_desc, result_desc, &result_ptrs);
    for (result_idx = 0;  result_idx < result_cnt;  result_idx++)
    {
        operand_parts = &result_ptrs.result_parts[result_idx];
        actual_len    = operand_parts->total_len;
        buf_ptr       = results->results[result_idx].buf_ptr;
        buf_len       = results->results[result_idx].buf_len;

        if (buf_ptr == NULL)
        {
            printf("gxcr_pka_get_results buf_ptr==NULL\n");
            return PKA_RESULT_BUF_NULL;
        }
        else if (buf_len < actual_len)
        {
            printf("gxcr_pka_get_results buf_len<actual_len\n");
            return PKA_RESULT_BUF_TOO_SMALL;
        }

        copy_operand_parts(buf_ptr, operand_parts, handle->big_endian);

        results->results[result_idx].actual_len = actual_len;
        results->results[result_idx].big_endian = handle->big_endian;
    }

    gxcr_pka_pop_result_fifo(handle);
    return PKA_NO_ERROR;
}

gxcr_pka_err_t gxcr_pka_try_get_results(gxcr_pka_handle_t *handle,
                                        pka_results_t     *results)
{
    if (gxcr_pka_has_results(handle) == 0)
        return PKA_TRY_GET_RESULTS_FAILED;

    return gxcr_pka_get_results(handle, results);
}

gxcr_pka_err_t gxcr_pka_result_info(gxcr_pka_handle_t *handle,
                                    uint8_t           *opcode,
                                    uint32_t          *status,
                                    void             **user_data)
{
    pka_driver_result_desc_t *result_desc;

    if (gxcr_pka_has_results(handle) == 0)
        return PKA_TRY_GET_RESULTS_FAILED;

    result_desc = (pka_driver_result_desc_t *) &handle->head_reply_desc;
    if (opcode != NULL)
        *opcode = result_desc->opcode;

    if (status != NULL)
        *status = result_desc->status;

    if (user_data != NULL)
        *user_data = result_desc->user_data;

    return result_desc->result_cnt;
}

int gxcr_pka_result_lengths(gxcr_pka_handle_t *handle,
                            uint32_t          *result_operand0_len,
                            uint32_t          *result_operand1_len)
{
    pka_driver_result_desc_t *result_desc;

    if (gxcr_pka_has_results(handle) == 0)
        return PKA_TRY_GET_RESULTS_FAILED;

    result_desc = (pka_driver_result_desc_t *) &handle->head_reply_desc;
    if (result_operand0_len != NULL)
        *result_operand0_len = result_desc->result[0].byte_len;

    if (result_operand1_len != NULL)
        *result_operand1_len = result_desc->result[1].byte_len;

    return result_desc->result_cnt;
}

gxcr_pka_err_t gxcr_pka_copy_result_operand(gxcr_pka_handle_t *handle,
                                            uint32_t           result_idx,
                                            uint8_t           *result_buf,
                                            uint32_t           result_buf_len)
{
    pka_driver_result_desc_t *result_desc;
    client_results_desc_t    *result_ring_desc;
    result_operand_parts_t   *operand_parts;
    result_ptrs_t             result_ptrs;
    uint32_t                  result_cnt, actual_len;

    if (gxcr_pka_has_results(handle) == 0)
        return PKA_TRY_GET_RESULTS_FAILED;

    result_desc      = (pka_driver_result_desc_t *) &handle->head_reply_desc;
    result_ring_desc = (client_results_desc_t    *) handle->result_ring_desc;
    result_cnt       = result_desc->result_cnt;
    if (result_cnt <= result_idx)
        return PKA_BAD_RESULT_IDX;

    get_result_src_ptrs(handle, result_ring_desc, result_desc, &result_ptrs);

    operand_parts = &result_ptrs.result_parts[result_idx];
    actual_len    = operand_parts->total_len;

    if (result_buf == NULL)
        return PKA_RESULT_BUF_NULL;
    else if (result_buf_len < actual_len)
        return PKA_RESULT_BUF_TOO_SMALL;

    copy_operand_parts(result_buf, operand_parts, handle->big_endian);
    return actual_len;
}



static void internal_copy_rand_bytes(gxcr_pka_handle_t *handle,
                                     uint8_t           *dst_rand_bytes,
                                     uint32_t           num_rand_bytes)
{
    rand_fifo_desc_t *rand_fifo_desc;
    uint32_t          base_offset, head_offset, rem_len, copy_len;
    uint8_t          *src_ptr;

    // See if we need one memcpy or two.
    rand_fifo_desc = handle->rand_fifo_desc;
    base_offset    = rand_fifo_desc->base_offset;
    head_offset    = handle->rand_fifo_head_offset;
    rem_len        = 0x10000 - head_offset;
    src_ptr        = handle->master_ptr + (base_offset + head_offset);
    copy_len       = MIN(num_rand_bytes, rem_len);
    memcpy(dst_rand_bytes, src_ptr, copy_len);
    dst_rand_bytes += copy_len;
    handle->rand_fifo_head_offset += copy_len;

    copy_len = num_rand_bytes - copy_len;
    if (copy_len != 0)
    {
        src_ptr = handle->master_ptr + base_offset;
        memcpy(dst_rand_bytes, src_ptr, copy_len);
        handle->rand_fifo_head_offset = copy_len;
    }

    atomic_sub(&rand_fifo_desc->curr_size, num_rand_bytes);
    __insn_mf();
}

static gxcr_pka_err_t internal_get_rand_bytes(gxcr_pka_handle_t *handle,
                                              uint8_t           *rand_bytes,
                                              uint32_t           num_rand_bytes,
                                              boolean_t          wait_for_bytes)
{
    rand_fifo_desc_t *rand_fifo_desc;
    uint32_t          curr_size, bytes_to_copy;

    // First see if there are enough bytes to service this request.
    __insn_mf();
    rand_fifo_desc = handle->rand_fifo_desc;
    curr_size      = arch_atomic_access_once(rand_fifo_desc->curr_size);
    if (curr_size < num_rand_bytes)
    {
        if (! wait_for_bytes)
            return PKA_TRY_GET_RANDOM_FAILED;

        while (1)
        {
            bytes_to_copy = MIN(curr_size, num_rand_bytes);
            if (bytes_to_copy != 0)
            {
                internal_copy_rand_bytes(handle, rand_bytes, bytes_to_copy);
                rand_bytes     += bytes_to_copy;
                num_rand_bytes -= bytes_to_copy;
                if (num_rand_bytes == 0)
                    break;
            }

            busy_wait_delay();
            curr_size = arch_atomic_access_once(rand_fifo_desc->curr_size);
        };
    }
    else
        internal_copy_rand_bytes(handle, rand_bytes, num_rand_bytes);

    return PKA_NO_ERROR;
}

gxcr_pka_err_t gxcr_pka_get_rand_bytes(gxcr_pka_handle_t *handle,
                                       uint8_t           *rand_bytes,
                                       uint32_t           num_rand_bytes)
{
    return internal_get_rand_bytes(handle, rand_bytes, num_rand_bytes, TRUE);
}

gxcr_pka_err_t gxcr_pka_try_get_rand_bytes(gxcr_pka_handle_t *handle,
                                           uint8_t           *rand_bytes,
                                           uint32_t           num_rand_bytes)
{
    return internal_get_rand_bytes(handle, rand_bytes, num_rand_bytes, FALSE);
}

