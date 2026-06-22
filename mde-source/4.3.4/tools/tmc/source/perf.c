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

// Support for accessing performance counters from user space.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <arch/diag.h>
#include <arch/spr_def.h>

#include <tmc/perf.h>

// We don't cache the returned value, as it's possible for the cpu
// speed to change while the system is running.
uint64_t
tmc_perf_get_cpu_speed(void)
{
  // Acquire (part of) "/proc/cpuinfo".
  int fd = open("/proc/cpuinfo", O_RDONLY);
  if (fd == -1)
    return 0;
  char buffer[1024];
  ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
  close(fd);
  if (n < 0)
    return 0;
  buffer[n] = '\0';

  // Find the first "cpu MHz" line.
  char* p = strstr(buffer, "cpu MHz");
  if (p == NULL)
    return 0;

  // Clip at the end of line.
  char* e = strchr(p, '\n');
  if (e == NULL)
    return 0;
  *e = '\0';

  // Find the colon.
  char* c = strchr(p, ':');
  if (c == NULL)
    return 0;

  // Extract the actual "MHz".
  double mhz = strtod(c + 1, NULL);

  return (uint64_t)(mhz * 1000000 + 0.5);
}

static int
tmc_perf_set_counters(const char *value)
{
  char *filename = "/proc/sys/tile/userspace_perf_counters";
  int fd = open(filename, O_RDWR);
  if (fd < 0)
    return -1;

  int rc = write(fd, value, 1);
  if (rc >= 0)
    rc = 0;

  close(fd);
  return rc;
}

int
tmc_perf_enable_counters(void)
{
  return tmc_perf_set_counters("1");
}

int
tmc_perf_disable_counters(void)
{
  return tmc_perf_set_counters("0");
}


// Define a short way to express the combined box number and event number.
// We set its high bit as a flag to show that it's a valid entry in the
// table; we'll mask that off before storing the value into a hardware
// register.

#define EVENT_VALID_MASK (1 << 15)

#define _EVT(box, evt) \
  (EVENT_VALID_MASK | DIAG_ ## box ## _EVENT_ ## evt | \
   (SPR_AUX_PERF_COUNT_CTL__COUNT_0_BOX_VAL_ ## box << \
    SPR_PERF_COUNT_CTL__COUNT_0_BOX_SHIFT))

#define EVT(evt, box) _EVT(evt, box)

// Cope with a naming irregularity in the diag definitions.
#define SPR_AUX_PERF_COUNT_CTL__COUNT_0_BOX_VAL_XDN \
  SPR_AUX_PERF_COUNT_CTL__COUNT_0_BOX_VAL_NETWORK

// Translate an enum tmc_perf_events_e to the real value used by the
// hardware.  Since we or EVENT_VALID_MASK into all entries, the default
// array initialization value of zero signifies an illegal value.
static uint16_t perf_event_to_perf_ctl[] =
{
  [TMC_PERF_ZERO] = EVT(TOP, ZERO),
  [TMC_PERF_ONE] = EVT(TOP, ONE),
  [TMC_PERF_PASS_WRITTEN] = EVT(TOP, PASS_WRITTEN),
  [TMC_PERF_FAIL_WRITTEN] = EVT(TOP, FAIL_WRITTEN),
  [TMC_PERF_DONE_WRITTEN] = EVT(TOP, DONE_WRITTEN),
  [TMC_PERF_SBOX_QUIESCED] = EVT(TOP, SBOX_QUIESCED),
  [TMC_PERF_CBOX_QUIESCED] = EVT(TOP, CBOX_QUIESCED),

  [TMC_PERF_TRACE_SAMPLE] = EVT(TOP, TRACE_SAMPLE),
  [TMC_PERF_CYCLE_12] = EVT(SBOX, CYCLE_12),
  [TMC_PERF_CYCLE_8] = EVT(SBOX, CYCLE_8),
  [TMC_PERF_CYCLE_4] = EVT(SBOX, CYCLE_4),

  [TMC_PERF_ICOH] = EVT(SBOX, ICOH),
  [TMC_PERF_L1D_FILL_STALL] = EVT(SBOX, L1D_FILL_STALL),
  [TMC_PERF_CBOX_FULL_STALL] = EVT(SBOX, CBOX_FULL_STALL),
  [TMC_PERF_LOAD_HIT_STALL] = EVT(SBOX, LOAD_HIT_STALL),
  [TMC_PERF_LOAD_STALL] = EVT(SBOX, LOAD_STALL),
  [TMC_PERF_ALU_SRC_STALL] = EVT(SBOX, ALU_SRC_STALL),

  [TMC_PERF_IDN_SRC_STALL] = EVT(SBOX, IDN_SRC_STALL),
  [TMC_PERF_UDN_SRC_STALL] = EVT(SBOX, UDN_SRC_STALL),

  [TMC_PERF_MF_STALL] = EVT(SBOX, MF_STALL),
  [TMC_PERF_SLOW_SPR_STALL] = EVT(SBOX, SLOW_SPR_STALL),

  [TMC_PERF_NETWORK_DEST_STALL] = EVT(SBOX, NETWORK_DEST_STALL),

  [TMC_PERF_INSTRUCTION_STALL] = EVT(SBOX, INSTRUCTION_STALL),
  [TMC_PERF_PFB_HIT_IN_PFB] = EVT(SBOX, PFB_HIT_IN_PFB),
  [TMC_PERF_PFB_HIT] = EVT(SBOX, PFB_HIT),
  [TMC_PERF_CBOX_RESP] = EVT(SBOX, CBOX_RESP),
  [TMC_PERF_MEM_OP] = EVT(SBOX, MEM_OP),
  [TMC_PERF_CBOX_REQ] = EVT(SBOX, CBOX_REQ),
  [TMC_PERF_ITLB_MISS_INTERRUPT] = EVT(SBOX, ITLB_MISS_INTERRUPT),
  [TMC_PERF_INTERRUPT] = EVT(SBOX, INTERRUPT),
  [TMC_PERF_ICACHE_FILL_PEND] = EVT(SBOX, ICACHE_FILL_PEND),
  [TMC_PERF_ICACHE_FILL] = EVT(SBOX, ICACHE_FILL),
  [TMC_PERF_WAY_MISPREDICT] = EVT(SBOX, WAY_MISPREDICT),
  [TMC_PERF_COND_BRANCH_PRED_CORRECT] = EVT(SBOX, COND_BRANCH_PRED_CORRECT),
  [TMC_PERF_COND_BRANCH_PRED_INCORRECT] = EVT(SBOX, COND_BRANCH_PRED_INCORRECT),
  // Note: VALID_WB is internal name for INSTRUCTION_BUNDLE event, and hence is deprecated.
  [TMC_PERF_VALID_WB] = EVT(SBOX, INSTRUCTION_BUNDLE),
  [TMC_PERF_INSTRUCTION_BUNDLE] = EVT(SBOX, INSTRUCTION_BUNDLE),
  [TMC_PERF_RESTART] = EVT(SBOX, RESTART),
  [TMC_PERF_USED_RAS] = EVT(SBOX, USED_RAS),
  [TMC_PERF_RAS_CORRECT] = EVT(SBOX, RAS_CORRECT),
  [TMC_PERF_COND_BRANCH_NO_PREDICT] = EVT(SBOX, COND_BRANCH_NO_PREDICT),
  [TMC_PERF_TLB] = EVT(MBOX, TLB),
  [TMC_PERF_READ] = EVT(MBOX, READ),
  [TMC_PERF_WRITE] = EVT(MBOX, WRITE),
  [TMC_PERF_TLB_EXCEPTION] = EVT(MBOX, TLB_EXCEPTION),
  [TMC_PERF_READ_MISS] = EVT(MBOX, READ_MISS),
  [TMC_PERF_WRITE_MISS] = EVT(MBOX, WRITE_MISS),
  [TMC_PERF_L1_OPCODE_VALID] = EVT(MBOX, L1_OPCODE_VALID),
  [TMC_PERF_L1_OPCODE_LD_VALID] = EVT(MBOX, L1_OPCODE_LD_VALID),
  [TMC_PERF_L1_OPCODE_ST_VALID] = EVT(MBOX, L1_OPCODE_ST_VALID),
  [TMC_PERF_L1_OPCODE_ATOMIC_VALID] = EVT(MBOX, L1_OPCODE_ATOMIC_VALID),
  [TMC_PERF_L1_OPCODE_FLUSH_VALID] = EVT(MBOX, L1_OPCODE_FLUSH_VALID),
  [TMC_PERF_L1_OPCODE_INV_VALID] = EVT(MBOX, L1_OPCODE_INV_VALID),
  [TMC_PERF_L1_OPCODE_FINV_VALID] = EVT(MBOX, L1_OPCODE_FINV_VALID),
  [TMC_PERF_L1_OPCODE_MF_VALID] = EVT(MBOX, L1_OPCODE_MF_VALID),
  [TMC_PERF_L1_OPCODE_PFETCH_VALID] = EVT(MBOX, L1_OPCODE_PFETCH_VALID),
  [TMC_PERF_L1_OPCODE_WH64_VALID] = EVT(MBOX, L1_OPCODE_WH64_VALID),
  [TMC_PERF_L1_OPCODE_DTLBPR_VALID] = EVT(MBOX, L1_OPCODE_DTLBPR_VALID),
  [TMC_PERF_L1_OPCODE_FWB_VALID] = EVT(MBOX, L1_OPCODE_FWB_VALID),

  [TMC_PERF_L1_OPCODE_LD_NON_TEMPORAL_VALID] =
    EVT(MBOX, L1_OPCODE_LD_NON_TEMPORAL_VALID),
  [TMC_PERF_L1_OPCODE_ST_NON_TEMPORAL_VALID] =
    EVT(MBOX, L1_OPCODE_ST_NON_TEMPORAL_VALID),

  [TMC_PERF_SNOOP_INCREMENT_READ] = EVT(CBOX, SNOOP_INCREMENT_READ),
  [TMC_PERF_SNOOP_NON_INCREMENT_READ] = EVT(CBOX, SNOOP_NON_INCREMENT_READ),
  [TMC_PERF_SNOOP_WRITE] = EVT(CBOX, SNOOP_WRITE),
  [TMC_PERF_SNOOP_IO_READ] = EVT(CBOX, SNOOP_IO_READ),
  [TMC_PERF_SNOOP_IO_WRITE] = EVT(CBOX, SNOOP_IO_WRITE),
  [TMC_PERF_LOCAL_DATA_READ] = EVT(CBOX, LOCAL_DATA_READ),
  [TMC_PERF_LOCAL_WRITE] = EVT(CBOX, LOCAL_WRITE),
  [TMC_PERF_LOCAL_INSTRUCTION_READ] = EVT(CBOX, LOCAL_INSTRUCTION_READ),
  [TMC_PERF_REMOTE_DATA_READ] = EVT(CBOX, REMOTE_DATA_READ),
  [TMC_PERF_REMOTE_WRITE] = EVT(CBOX, REMOTE_WRITE),
  [TMC_PERF_REMOTE_INSTRUCTION_READ] = EVT(CBOX, REMOTE_INSTRUCTION_READ),
  [TMC_PERF_COHERENCE_INVALIDATION] = EVT(CBOX, COHERENCE_INVALIDATION),
  [TMC_PERF_SNOOP_INCREMENT_READ_MISS] = EVT(CBOX, SNOOP_INCREMENT_READ_MISS),
  [TMC_PERF_SNOOP_NON_INCREMENT_READ_MISS] =
    EVT(CBOX, SNOOP_NON_INCREMENT_READ_MISS),
  [TMC_PERF_SNOOP_WRITE_MISS] = EVT(CBOX, SNOOP_WRITE_MISS),
  [TMC_PERF_SNOOP_IO_READ_MISS] = EVT(CBOX, SNOOP_IO_READ_MISS),
  [TMC_PERF_SNOOP_IO_WRITE_MISS] = EVT(CBOX, SNOOP_IO_WRITE_MISS),
  [TMC_PERF_LOCAL_DATA_READ_MISS] = EVT(CBOX, LOCAL_DATA_READ_MISS),
  [TMC_PERF_LOCAL_WRITE_MISS] = EVT(CBOX, LOCAL_WRITE_MISS),
  [TMC_PERF_LOCAL_INSTRUCTION_READ_MISS] =
    EVT(CBOX, LOCAL_INSTRUCTION_READ_MISS),
  [TMC_PERF_REMOTE_DATA_READ_MISS] = EVT(CBOX, REMOTE_DATA_READ_MISS),
  [TMC_PERF_REMOTE_WRITE_MISS] = EVT(CBOX, REMOTE_WRITE_MISS),
  [TMC_PERF_REMOTE_INSTRUCTION_READ_MISS] =
    EVT(CBOX, REMOTE_INSTRUCTION_READ_MISS),
  [TMC_PERF_COHERENCE_INVALIDATION_HIT] = EVT(CBOX, COHERENCE_INVALIDATION_HIT),
  [TMC_PERF_CACHE_WRITEBACK] = EVT(CBOX, CACHE_WRITEBACK),
  [TMC_PERF_SDN_STARVED] = EVT(CBOX, SDN_STARVED),
  [TMC_PERF_RDN_STARVED] = EVT(CBOX, RDN_STARVED),
  [TMC_PERF_QDN_STARVED] = EVT(CBOX, QDN_STARVED),
  [TMC_PERF_SKF_STARVED] = EVT(CBOX, SKF_STARVED),
  [TMC_PERF_RTF_STARVED] = EVT(CBOX, RTF_STARVED),
  [TMC_PERF_IREQ_STARVED] = EVT(CBOX, IREQ_STARVED),
  [TMC_PERF_ITLB_OLOC_CACHE_MISS] = EVT(CBOX, ITLB_OLOC_CACHE_MISS),
  [TMC_PERF_DTLB_OLOC_CACHE_MISS] = EVT(CBOX, DTLB_OLOC_CACHE_MISS),
  [TMC_PERF_LOCAL_WRITE_BUFFER_ALLOC] = EVT(CBOX, LOCAL_WRITE_BUFFER_ALLOC),
  [TMC_PERF_REMOTE_WRITE_BUFFER_ALLOC] = EVT(CBOX, REMOTE_WRITE_BUFFER_ALLOC),
  [TMC_PERF_ARB_VALID] = EVT(CBOX, ARB_VALID),
  [TMC_PERF_MDF_WRITE] = EVT(CBOX, MDF_WRITE),
  [TMC_PERF_LDB_READ] = EVT(CBOX, LDB_READ),
  [TMC_PERF_L2_OPCODE_LD_VALID] = EVT(CBOX, L2_OPCODE_LD_VALID),
  [TMC_PERF_L2_OPCODE_ST_VALID] = EVT(CBOX, L2_OPCODE_ST_VALID),
  [TMC_PERF_L2_OPCODE_ATOMIC_VALID] = EVT(CBOX, L2_OPCODE_ATOMIC_VALID),
  [TMC_PERF_L2_OPCODE_FLUSH_VALID] = EVT(CBOX, L2_OPCODE_FLUSH_VALID),
  [TMC_PERF_L2_OPCODE_INV_VALID] = EVT(CBOX, L2_OPCODE_INV_VALID),
  [TMC_PERF_L2_OPCODE_FINV_VALID] = EVT(CBOX, L2_OPCODE_FINV_VALID),
  [TMC_PERF_L2_OPCODE_MF_VALID] = EVT(CBOX, L2_OPCODE_MF_VALID),
  [TMC_PERF_L2_OPCODE_PFETCH_VALID] = EVT(CBOX, L2_OPCODE_PFETCH_VALID),
  [TMC_PERF_L2_OPCODE_WH64_VALID] = EVT(CBOX, L2_OPCODE_WH64_VALID),
  [TMC_PERF_L2_OPCODE_FWB_VALID] = EVT(CBOX, L2_OPCODE_FWB_VALID),

  [TMC_PERF_L2_OPCODE_LD_NON_TEMPORAL_VALID] =
    EVT(CBOX, L2_OPCODE_LD_NON_TEMPORAL_VALID),
  [TMC_PERF_L2_OPCODE_ST_NON_TEMPORAL_VALID] =
    EVT(CBOX, L2_OPCODE_ST_NON_TEMPORAL_VALID),

  [TMC_PERF_L2_OPCODE_LD_NOFIL_VALID] = EVT(CBOX, L2_OPCODE_LD_NOFIL_VALID),

  [TMC_PERF_L2_OPCODE_LD_NOFIL_NON_TEMPORAL_VALID] =
    EVT(CBOX, L2_OPCODE_LD_NOFIL_NON_TEMPORAL_VALID),

  [TMC_PERF_L2_OPCODE_RDN_VALID] = EVT(CBOX, L2_OPCODE_RDN_VALID),
  [TMC_PERF_L2_OPCODE_QDN_VALID] = EVT(CBOX, L2_OPCODE_QDN_VALID),
  [TMC_PERF_L2_OPCODE_IO_READ_VALID] = EVT(CBOX, L2_OPCODE_IO_READ_VALID),
  [TMC_PERF_L2_OPCODE_IO_WRITE_VALID] = EVT(CBOX, L2_OPCODE_IO_WRITE_VALID),
  [TMC_PERF_L2_OPCODE_I_STREAM_VALID] = EVT(CBOX, L2_OPCODE_I_STREAM_VALID),
  [TMC_PERF_L2_OPCODE_MDF_VALID] = EVT(CBOX, L2_OPCODE_MDF_VALID),
  [TMC_PERF_L2_OPCODE_IREQ_IV_VALID] = EVT(CBOX, L2_OPCODE_IREQ_IV_VALID),

  [TMC_PERF_UDN_PACKET_SENT] = EVT(XDN, UDN_PACKET_SENT),
  [TMC_PERF_UDN_WORD_SENT] = EVT(XDN, UDN_WORD_SENT),
  [TMC_PERF_UDN_BUBBLE] = EVT(XDN, UDN_BUBBLE),
  [TMC_PERF_UDN_CONGESTION] = EVT(XDN, UDN_CONGESTION),
  [TMC_PERF_IDN_PACKET_SENT] = EVT(XDN, IDN_PACKET_SENT),
  [TMC_PERF_IDN_WORD_SENT] = EVT(XDN, IDN_WORD_SENT),
  [TMC_PERF_IDN_BUBBLE] = EVT(XDN, IDN_BUBBLE),
  [TMC_PERF_IDN_CONGESTION] = EVT(XDN, IDN_CONGESTION),

  [TMC_PERF_RDN_PACKET_SENT] = EVT(XDN, RDN_PACKET_SENT),
  [TMC_PERF_RDN_WORD_SENT] = EVT(XDN, RDN_WORD_SENT),
  [TMC_PERF_RDN_BUBBLE] = EVT(XDN, RDN_BUBBLE),
  [TMC_PERF_RDN_CONGESTION] = EVT(XDN, RDN_CONGESTION),
  [TMC_PERF_SDN_PACKET_SENT] = EVT(XDN, SDN_PACKET_SENT),
  [TMC_PERF_SDN_WORD_SENT] = EVT(XDN, SDN_WORD_SENT),
  [TMC_PERF_SDN_BUBBLE] = EVT(XDN, SDN_BUBBLE),
  [TMC_PERF_SDN_CONGESTION] = EVT(XDN, SDN_CONGESTION),
  [TMC_PERF_QDN_PACKET_SENT] = EVT(XDN, QDN_PACKET_SENT),
  [TMC_PERF_QDN_WORD_SENT] = EVT(XDN, QDN_WORD_SENT),
  [TMC_PERF_QDN_BUBBLE] = EVT(XDN, QDN_BUBBLE),
  [TMC_PERF_QDN_CONGESTION] = EVT(XDN, QDN_CONGESTION),

  [TMC_PERF_UDN_DEMUX_STALL] = EVT(XDN, UDN_DEMUX_STALL),
  [TMC_PERF_IDN_DEMUX_STALL] = EVT(XDN, IDN_DEMUX_STALL),
  [TMC_PERF_WATCH] = EVT(SPCL, WATCH),
  [TMC_PERF_BCST0] = EVT(SPCL, BCST0),
  [TMC_PERF_BCST1] = EVT(SPCL, BCST1),
  [TMC_PERF_BCST2] = EVT(SPCL, BCST2),
  [TMC_PERF_BCST3] = EVT(SPCL, BCST3),

  [TMC_PERF_PCNT0] = EVT(SPCL, PCNT0),
  [TMC_PERF_PCNT1] = EVT(SPCL, PCNT1),
  [TMC_PERF_AUX_PCNT0] = EVT(SPCL, AUX_PCNT0),
  [TMC_PERF_AUX_PCNT1] = EVT(SPCL, AUX_PCNT1),
};

// Look up a event specifier in the table of hardware values; return it to
// the caller, and if the specifier is invalid, set a flag saying so.
static uint32_t
xlate_perf_event(enum tmc_perf_events_e event, int* is_illegal)
{
  uint16_t hw_event = 0;

  if (event < sizeof (perf_event_to_perf_ctl) /
              sizeof (*perf_event_to_perf_ctl))
    hw_event = perf_event_to_perf_ctl[event];

  if (hw_event)
    return hw_event & ~EVENT_VALID_MASK;

  *is_illegal = -1;
  return EVT(TOP, ZERO);
}

// Select four events to initialize the hardware counters.
int
tmc_perf_setup_counters(enum tmc_perf_events_e event1,
                        enum tmc_perf_events_e event2,
                        enum tmc_perf_events_e event3,
                        enum tmc_perf_events_e event4)
{
  int rv = 0;

  uint16_t hw_event1 = xlate_perf_event(event1, &rv);
  uint16_t hw_event2 = xlate_perf_event(event2, &rv);
  uint16_t hw_event3 = xlate_perf_event(event3, &rv);
  uint16_t hw_event4 = xlate_perf_event(event4, &rv);

  __insn_mtspr(SPR_PERF_COUNT_CTL, hw_event1 |
               (hw_event2 << SPR_PERF_COUNT_CTL__COUNT_1_SEL_SHIFT));
  __insn_mtspr(SPR_AUX_PERF_COUNT_CTL, hw_event3 |
               (hw_event4 << SPR_AUX_PERF_COUNT_CTL__COUNT_1_SEL_SHIFT));

  return rv;
}

// Read the four hardware counters without clearing them.
void
tmc_perf_read_counters(struct tmc_perf_counters *counters)
{
  counters->val[0] = __insn_mfspr(SPR_PERF_COUNT_0);
  counters->val[1] = __insn_mfspr(SPR_PERF_COUNT_1);
  counters->val[2] = __insn_mfspr(SPR_AUX_PERF_COUNT_0);
  counters->val[3] = __insn_mfspr(SPR_AUX_PERF_COUNT_1);
}

// Accumulate counts from the four hardware counters without clearing them.
void
tmc_perf_add_counters(struct tmc_perf_counters *counters)
{
  counters->val[0] += __insn_mfspr(SPR_PERF_COUNT_0);
  counters->val[1] += __insn_mfspr(SPR_PERF_COUNT_1);
  counters->val[2] += __insn_mfspr(SPR_AUX_PERF_COUNT_0);
  counters->val[3] += __insn_mfspr(SPR_AUX_PERF_COUNT_1);
}

// Clear the four hardware counters.
void
tmc_perf_clear_counters(void)
{
  __insn_mtspr(SPR_PERF_COUNT_0, 0);
  __insn_mtspr(SPR_PERF_COUNT_1, 0);
  __insn_mtspr(SPR_AUX_PERF_COUNT_0, 0);
  __insn_mtspr(SPR_AUX_PERF_COUNT_1, 0);
}
