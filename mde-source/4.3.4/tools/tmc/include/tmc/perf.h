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

//! @file
//!
//! Support accessing perf counters from user space.
//!

//! @addtogroup tmc_perf
//! @{
//!
//! Access performance counters from user space.
//! Note: Before profiling, you should enable access to perf counters
//! for user space by calling tmc_perf_enable_counters() or issuing
//! "sysctl -w tile.userspace_perf_counters=1", or you can use
//! /etc/sysctl.conf to set it permanently.
//! 
//!

#ifndef __TMC_PERF_H__
#define __TMC_PERF_H__

#include <features.h>
#include <stdint.h>
#include <arch/spr_def.h>
#include <arch/diag.h>

__BEGIN_DECLS

//! Event definitions.
enum tmc_perf_events_e
{
  //! Invalid event code.
  TMC_PERF_INVALID,

  //! Always zero - no event ever happens.
  TMC_PERF_ZERO,

  //! Always one - an event occurs every cycle.
  TMC_PERF_ONE,

  //! The event indicates that the PASS SPR was written.
  TMC_PERF_PASS_WRITTEN,

  //! The event indicates that the FAIL SPR was written.
  TMC_PERF_FAIL_WRITTEN,

  //! The event indicates that the DONE SPR was written.
  TMC_PERF_DONE_WRITTEN,

  //! An event occurs every cycle that the tile is in the instruction issue
  //! quiesced state.
  TMC_PERF_SBOX_QUIESCED,

  //! An event occurs every cycle that a tile is in the coherence traffic
  //! quiesced state.
  TMC_PERF_CBOX_QUIESCED,

  //! An event occurs every time a diagnostic trace sample is triggered.
  TMC_PERF_TRACE_SAMPLE,

  //! The event occurs when bit 12 of the cycle_counter SPR is asserted.  The
  //! transition of this event can be used to generate periodic triggers every
  //! 4k cycles.
  TMC_PERF_CYCLE_12,

  //! The event occurs when bit 8 of the cycle_counter SPR is asserted.  The
  //! transition of this event can be used to generate periodic triggers every
  //! 256 cycles.
  TMC_PERF_CYCLE_8,

  //! The event occurs when bit 4 of the cycle_counter SPR is asserted.  The
  //! transition of this event can be used to generate periodic triggers every
  //! 16 cycles.
  TMC_PERF_CYCLE_4,

  //! The event occurs when an icoh instruction completes.
  TMC_PERF_ICOH,

  //! The event occurs when a memory operation stalls due to L1 DCache being
  //! busy doing a fill.
  TMC_PERF_L1D_FILL_STALL,

  //! The event occurs when a memory operation stalls due to Cbox queue being
  //! full.
  TMC_PERF_CBOX_FULL_STALL,

  //! The event occurs when an instruction 2 cycles after a load stalls due to
  //! a source operand being the destination.  The L1 DCache hit latency is two
  //! cycles, so the instruction would stall on a miss but not on a hit.
  TMC_PERF_LOAD_HIT_STALL,

  //! The event occurs when an instruction stalls due to a source operand being
  //! the destination of a load instruction.  This event happens on all cycles
  //! that stall except for the one 2 cycles after the load, which is counted
  //! by LOAD_HIT_STALL event.
  TMC_PERF_LOAD_STALL,

  //! The event occurs when an instruction stalls due to a source operand being
  //! the destination of an ALU instruction.
  TMC_PERF_ALU_SRC_STALL,

  //! The event occurs when an instruction stalls due to IDN source register
  //! not available.
  TMC_PERF_IDN_SRC_STALL,

  //! The event occurs when an instruction stalls due to UDN source register
  //! not available.
  TMC_PERF_UDN_SRC_STALL,

  //! The event during stalls for the Memory Fence instruction.
  TMC_PERF_MF_STALL,

  //! The event occurs during stalls to slow SPR access.
  TMC_PERF_SLOW_SPR_STALL,

  //! The event occurs when a valid instruction in pipeline Decode stage stalls
  //! due to network destination full.
  TMC_PERF_NETWORK_DEST_STALL,

  //! The event occurs when a valid instruction in pipeline Decode stage stalls
  //! for any reason.
  TMC_PERF_INSTRUCTION_STALL,

  //! The event occurs when an instruction lookup hits in prefetch buffer,
  //! already in PFB.
  TMC_PERF_PFB_HIT_IN_PFB,

  //! The event occurs when an instruction lookup hits in prefetch buffer,
  //! either in the PFB or in-flight.
  TMC_PERF_PFB_HIT,

  //! The event occurs when Cbox responds with fill data, either demand or
  //! prefetch.
  TMC_PERF_CBOX_RESP,

  //! The event occurs when a memory operation is sent to Mbox.
  TMC_PERF_MEM_OP,

  //! The event occurs when an instruction request is sent to Cbox, either
  //! demand or prefetch.
  TMC_PERF_CBOX_REQ,

  //! The event occurs when an ITLB_MISS interrupt is taken.
  TMC_PERF_ITLB_MISS_INTERRUPT,

  //! The event occurs when an ITLB_MISS interrupt is taken.
  //! (Note: internal name, deprecated. Use TMC_PERF_ITLB_MISS_INTERRUPT instead.)
  TMC_PERF_ITLB_MISS_INT = TMC_PERF_ITLB_MISS_INTERRUPT,

  //! The event occurs when any interrupt is taken.
  TMC_PERF_INTERRUPT,

  //! The event occurs when any interrupt is taken.
  //! (Note: internal name, deprecated. Use TMC_PERF_INTERRUPT instead.)
  TMC_PERF_INT = TMC_PERF_INTERRUPT,

  //! The event occurs each cycle and Icache fill is pending.
  TMC_PERF_ICACHE_FILL_PEND,

  //! The event occurs each time a line in Icache is filled.
  TMC_PERF_ICACHE_FILL,

  //! The event occurs when the Icache has to correct the way prediction due
  //! to a mispredict.
  TMC_PERF_WAY_MISPREDICT,

  //! The event occurs when a correctly predicted branch instruction is
  //! committed.
  TMC_PERF_COND_BRANCH_PRED_CORRECT,

  //! The event occurs when an icorrectly predicted branch instruction is
  //! committed.
  TMC_PERF_COND_BRANCH_PRED_INCORRECT,

  //! The event occurs when there is a valid instruction in pipeline WB stage
  //! (Note: internal name, deprecated. Use TMC_PERF_INSTRUCTION_BUNDLE instead.)
  TMC_PERF_VALID_WB,

  //! The event occurs when an instruction is committed).
  TMC_PERF_INSTRUCTION_BUNDLE,

  //! The event occurs when the instruction pipeline is restarted.
  TMC_PERF_RESTART,

  //! The event occurs when the Return Address Stack is used for jump
  //! instruction.
  TMC_PERF_USED_RAS,

  //! The event occurs when the value from Return Address Stack was correct.
  TMC_PERF_RAS_CORRECT,

  //! The event occurs when a predicted taken branch that did not do a
  //! prediction due to bypassing branch predict pipeline is committed.
  TMC_PERF_COND_BRANCH_NO_PREDICT,

  //! The event occurs when a data memory operation is issued and the data
  //! translation lookaside buffer,
  //! address into the physical address.
  TMC_PERF_TLB,

  //! The event occurs when a load is issued.
  TMC_PERF_READ,

  //! The event occurs when a store is issued.
  TMC_PERF_WRITE,

  //! The event occurs when the address of a data stream memory operation
  //! causes a Data TLB Exception including TLB Misses and protection
  //! violations.
  TMC_PERF_TLB_EXCEPTION,

  //! The event occurs when a load is issued and data is not returned from the
  //! level 1 data cache.
  TMC_PERF_READ_MISS,

  //! The event occurs when a store is issued and the 16-byte aligned block
  //!,
  //! at the level 1 data cache.
  TMC_PERF_WRITE_MISS,

  //! The event occurs when the L1 data cache system processes a request.
  TMC_PERF_L1_OPCODE_VALID,

  //! The event occurs when the L1 data cache system processes a request with
  //! the opcode LD.
  TMC_PERF_L1_OPCODE_LD_VALID,

  //! The event occurs when the L1 data cache system processes a request with
  //! the opcode ST.
  TMC_PERF_L1_OPCODE_ST_VALID,

  //! The event occurs when the L1 data cache system processes a request with
  //! the opcode ATOMIC.
  TMC_PERF_L1_OPCODE_ATOMIC_VALID,

  //! The event occurs when the L1 data cache system processes a request with
  //! the opcode FLUSH.
  TMC_PERF_L1_OPCODE_FLUSH_VALID,

  //! The event occurs when the L1 data cache system processes a request with
  //! the opcode INV.
  TMC_PERF_L1_OPCODE_INV_VALID,

  //! The event occurs when the L1 data cache system processes a request with
  //! the opcode FINV.
  TMC_PERF_L1_OPCODE_FINV_VALID,

  //! The event occurs when the L1 data cache system processes a request with
  //! the opcode MF.
  TMC_PERF_L1_OPCODE_MF_VALID,

  //! The event occurs when the L1 data cache system processes a request with
  //! the opcode PFETCH.
  TMC_PERF_L1_OPCODE_PFETCH_VALID,

  //! The event occurs when the L1 data cache system processes a request with
  //! the opcode WH64.
  TMC_PERF_L1_OPCODE_WH64_VALID,

  //! The event occurs when the L1 data cache system processes a request with
  //! the opcode DTLBPR.
  TMC_PERF_L1_OPCODE_DTLBPR_VALID,

  //! The event occurs when the L1 data cache system processes a request with
  //! the opcode FWB.
  TMC_PERF_L1_OPCODE_FWB_VALID,

  //! The event occurs when the L1 data cache system processes a request with
  //! a non-temporal load.
  TMC_PERF_L1_OPCODE_LD_NON_TEMPORAL_VALID,

  //! The event occurs when the L1 data cache system processes a request with
  //! a non-temporal store.
  TMC_PERF_L1_OPCODE_ST_NON_TEMPORAL_VALID,

  //! The event occurs when a read request is received from another tile off
  //! the SDN and the Level 3 cache will track the share state.
  TMC_PERF_SNOOP_INCREMENT_READ,

  //! The event occurs when a read request is received from another tile off
  //! the SDN and the Level 3 cache will not track the share state.
  TMC_PERF_SNOOP_NON_INCREMENT_READ,

  //! The event occurs when a write request is received from another tile off
  //! the SDN.
  TMC_PERF_SNOOP_WRITE,

  //! The event occurs when a read request is received from an IO device off
  //! the SDN.
  TMC_PERF_SNOOP_IO_READ,

  //! The event occurs when a write request is received from an IO device off
  //! the SDN.
  TMC_PERF_SNOOP_IO_WRITE,

  //! The event occurs when a data read request is received from the main
  //! processor and the Level 3 cache resides in the tile.
  TMC_PERF_LOCAL_DATA_READ,

  //! The event occurs when a write request is received from the main processor
  //! and the Level 3 cache resides in the tile.
  TMC_PERF_LOCAL_WRITE,

  //! The event occurs when an instruction read request is received from the
  //! main processor and the Level 3 cache resides in the tile.
  TMC_PERF_LOCAL_INSTRUCTION_READ,

  //! The event occurs when a data read request is received from the main
  //! processor and the Level 3 cache resides in another tile.
  TMC_PERF_REMOTE_DATA_READ,

  //! The event occurs when a write request is received from the main processor
  //! and the Level 3 cache resides in another tile.
  TMC_PERF_REMOTE_WRITE,

  //! The event occurs when an instruction read request is received from the
  //! main processor and the Level 3 cache resides in another tile.
  TMC_PERF_REMOTE_INSTRUCTION_READ,

  //! The event occurs when a coherence invalidation is received from another
  //! tile off the QDN.
  TMC_PERF_COHERENCE_INVALIDATION,

  //! The event occurs when a read request is received from another tile off
  //! the SDN and misses the Level 3 cache. The level 3 cache will track the
  //! share state.
  TMC_PERF_SNOOP_INCREMENT_READ_MISS,

  //! The event occurs when a read request is received from another tile off
  //! the SDN and misses the Level 3 cache. The Level 3 cache will not track
  //! the share state.
  TMC_PERF_SNOOP_NON_INCREMENT_READ_MISS,

  //! The event occurs when a write request is received from another tile off
  //! the SDN and misses the Level 3 cache.
  TMC_PERF_SNOOP_WRITE_MISS,

  //! The event occurs when a read request is received from an IO device off
  //! the SDN and misses the Level 3 cache.
  TMC_PERF_SNOOP_IO_READ_MISS,

  //! The event occurs when a write request is received from an IO device off
  //! the SDN and misses the Level 3 cache.
  TMC_PERF_SNOOP_IO_WRITE_MISS,

  //! The event occurs when a data read request is received from the main
  //! processor and misses the Level 3 cache resided in the tile.
  TMC_PERF_LOCAL_DATA_READ_MISS,

  //! The event occurs when a write request is received from the main processor
  //! and misses the Level 3 cache resided in the tile.
  TMC_PERF_LOCAL_WRITE_MISS,

  //! The event occurs when an instruction read request is received from the
  //! main processor and misses the Level 3 cache resided in the tile.
  TMC_PERF_LOCAL_INSTRUCTION_READ_MISS,

  //! The event occurs when a data read request is received from the main
  //! processor and misses the Level 2 cache. The Level 3 cache resides in
  //! another tile.
  TMC_PERF_REMOTE_DATA_READ_MISS,

  //! The event occurs when a write request is received from the main processor
  //! and misses the Level 2 cache. The Level 3 cache resides in another tile.
  TMC_PERF_REMOTE_WRITE_MISS,

  //! The event occurs when an instruction read request is received from the
  //! main processor and misses the Level 2 cache. The Level 3 cache resides
  //! in another tile.
  TMC_PERF_REMOTE_INSTRUCTION_READ_MISS,

  //! The event occurs when a coherence invalidation is received from another
  //! tile off the QDN and hits the level 2 cache. 
  TMC_PERF_COHERENCE_INVALIDATION_HIT,

  //! The event occurs when a cache writeback to main memory, including victim
  //! writes or explicit flushes, leaves the tile.
  TMC_PERF_CACHE_WRITEBACK,

  //! The event occurs when a snoop is received and the controller enters the
  //! SDN starved condition.
  TMC_PERF_SDN_STARVED,

  //! The event occurs when the controller enters the RDN starved condition.
  TMC_PERF_RDN_STARVED,

  //! The event occurs when the controller enters the QDN starved condition.
  TMC_PERF_QDN_STARVED,

  //! The event occurs when the controller enters the skid FIFO starved
  //! condition.
  TMC_PERF_SKF_STARVED,

  //! The event occurs when the controller enters the re-try FIFO starved
  //! condition.
  TMC_PERF_RTF_STARVED,

  //! The event occurs when the controller enters the instruction stream
  //! starved condition.
  TMC_PERF_IREQ_STARVED,

  //! The event occurs when an instruction read request associated with the
  //! ITLB entry specified by ITLB_PERF is received from the main processor
  //! and misses the Level 2 cache. The Level 3 cache resides in another tile.
  TMC_PERF_ITLB_OLOC_CACHE_MISS,

  //! The event occurs when a data read or write,
  //! associated with the DTLB entry specified by DTLB_PERF is received from
  //! the main processor and misses the Level 2 cache. The Level 3 cache
  //! resides in another tile.
  TMC_PERF_DTLB_OLOC_CACHE_MISS,

  //! The event occurs when a write request is received from the main processor
  //! and allocates a write buffer in the Level 3 cache resided in the tile.
  TMC_PERF_LOCAL_WRITE_BUFFER_ALLOC,

  //! The event occurs when a write request is received from the main processor
  //! and allocates a write buffer in the Level 2 cache resided in the tile.
  //! The Level 3 cache resides in another tile.
  TMC_PERF_REMOTE_WRITE_BUFFER_ALLOC,

  //! The event occurs when a request is processed in the L2 pipeline.
  TMC_PERF_ARB_VALID,

  //! The event occurs when a request generates a MDF write.
  TMC_PERF_MDF_WRITE,

  //! The event occurs when a request generates a Load Buffer read.
  TMC_PERF_LDB_READ,

  //! The event occurs when the L2 cache system processes a request with
  //! a load opcode.
  TMC_PERF_L2_OPCODE_LD_VALID,

  //! The event occurs when the L2 cache system processes a request with
  //! a store opcode.
  TMC_PERF_L2_OPCODE_ST_VALID,

  //! The event occurs when the L2 cache system processes a request with
  //! the opcode ATOMIC.
  TMC_PERF_L2_OPCODE_ATOMIC_VALID,

  //! The event occurs when the L2 cache system processes a request with
  //! the opcode FLUSH.
  TMC_PERF_L2_OPCODE_FLUSH_VALID,

  //! The event occurs when the L2 cache system processes a request with
  //! the opcode INV.
  TMC_PERF_L2_OPCODE_INV_VALID,

  //! The event occurs when the L2 cache system processes a request with
  //! the opcode FINV.
  TMC_PERF_L2_OPCODE_FINV_VALID,

  //! The event occurs when the L2 cache system processes a request with
  //! the opcode MF.
  TMC_PERF_L2_OPCODE_MF_VALID,

  //! The event occurs when the L2 cache system processes a request with
  //! the opcode PFETCH.
  TMC_PERF_L2_OPCODE_PFETCH_VALID,

  //! The event occurs when the L2 cache system processes a request with
  //! the opcode WH64.
  TMC_PERF_L2_OPCODE_WH64_VALID,

  //! The event occurs when the L2 cache system processes a request with
  //! the opcode FWB.
  TMC_PERF_L2_OPCODE_FWB_VALID,

  //! The event occurs when the L2 cache system processes a request with
  //! the opcode LD_NON_TEMPORAL.
  TMC_PERF_L2_OPCODE_LD_NON_TEMPORAL_VALID,

  //! The event occurs when the L2 cache system processes a request with
  //! the opcode ST_NON_TEMPORAL.
  TMC_PERF_L2_OPCODE_ST_NON_TEMPORAL_VALID,

  //! The event occurs when the L2 cache system processes a request with
  //! the opcode LD_NOFIL.
  TMC_PERF_L2_OPCODE_LD_NOFIL_VALID,

  //! The event occurs when the L2 cache system processes a request with
  //! the opcode LD_NOFIL_NON_TEMPORAL.
  TMC_PERF_L2_OPCODE_LD_NOFIL_NON_TEMPORAL_VALID,

  //! The event occurs when the L2 cache system processes a request from RDN
  //! network.
  TMC_PERF_L2_OPCODE_RDN_VALID,

  //! The event occurs when the L2 cache system processes a request from QDN
  //! network.
  TMC_PERF_L2_OPCODE_QDN_VALID,

  //! The event occurs when the L2 cache system processes an IO read request.
  TMC_PERF_L2_OPCODE_IO_READ_VALID,

  //! The event occurs when the L2 cache system processes an IO write request.
  TMC_PERF_L2_OPCODE_IO_WRITE_VALID,

  //! The event occurs when the L2 cache system processes an I-stream request.
  TMC_PERF_L2_OPCODE_I_STREAM_VALID,

  //! The event occurs when the L2 cache system processes an MDF request.
  TMC_PERF_L2_OPCODE_MDF_VALID,

  //! The event occurs when the L2 cache system processes a request with the
  //! opcode IREQ_IV.
  TMC_PERF_L2_OPCODE_IREQ_IV_VALID,

  //! Main processor finished sending a packet to the UDN.
  TMC_PERF_UDN_PACKET_SENT,

  //! UDN word sent to an output port.  Participating ports are selected with
  //! the UDN_EVT_PORT_SEL field.
  TMC_PERF_UDN_WORD_SENT,

  //! Bubble detected on an output port.  A bubble is defined as a cycle in
  //! which no data is being sent, but the first word of a packet has already
  //! traversed the switch.  Participating ports are selected with the
  //! UDN_EVT_PORT_SEL field.
  TMC_PERF_UDN_BUBBLE,

  //! Out of credit on an output port.  Participating ports are selected with
  //! the UDN_EVT_PORT_SEL field.
  TMC_PERF_UDN_CONGESTION,

  //! Main processor finished sending a packet to the IDN.
  TMC_PERF_IDN_PACKET_SENT,

  //! IDN word sent to an output port.  Participating ports are selected with
  //! the IDN_EVT_PORT_SEL field.
  TMC_PERF_IDN_WORD_SENT,

  //! Bubble detected on an output port.  A bubble is defined as a cycle in
  //! which no data is being sent, but the first word of a packet has already
  //! traversed the switch.  Participating ports are selected with the
  //! IDN_EVT_PORT_SEL field.
  TMC_PERF_IDN_BUBBLE,

  //! Out of credit on an output port.  Participating ports are selected with
  //! the IDN_EVT_PORT_SEL field.
  TMC_PERF_IDN_CONGESTION,

  //! Main processor finished sending a packet to the RDN.
  TMC_PERF_RDN_PACKET_SENT,

  //! RDN word sent to an output port.  Participating ports are selected with
  //! the RDN_EVT_PORT_SEL field.
  TMC_PERF_RDN_WORD_SENT,

  //! Bubble detected on an output port.  A bubble is defined as a cycle in
  //! which no data is being sent, but the first word of a packet has already
  //! traversed the switch.  Participating ports are selected with the
  //! RDN_EVT_PORT_SEL field.
  TMC_PERF_RDN_BUBBLE,

  //! Out of credit on an output port.  Participating ports are selected with
  //! the RDN_EVT_PORT_SEL field.
  TMC_PERF_RDN_CONGESTION,

  //! Main processor finished sending a packet to the SDN.
  TMC_PERF_SDN_PACKET_SENT,

  //! SDN word sent to an output port.  Participating ports are selected with
  //! the SDN_EVT_PORT_SEL field.
  TMC_PERF_SDN_WORD_SENT,

  //! Bubble detected on an output port.  A bubble is defined as a cycle in
  //! which no data is being sent, but the first word of a packet has already
  //! traversed the switch.  Participating ports are selected with the
  //! SDN_EVT_PORT_SEL field.
  TMC_PERF_SDN_BUBBLE,

  //! Out of credit on an output port.  Participating ports are selected with
  //! the SDN_EVT_PORT_SEL field.
  TMC_PERF_SDN_CONGESTION,

  //! Main processor finished sending a packet to the QDN.
  TMC_PERF_QDN_PACKET_SENT,

  //! QDN word sent to an output port.  Participating ports are selected with
  //! the QDN_EVT_PORT_SEL field.
  TMC_PERF_QDN_WORD_SENT,

  //! Bubble detected on an output port.  A bubble is defined as a cycle in
  //! which no data is being sent, but the first word of a packet has already
  //! traversed the switch.  Participating ports are selected with the
  //! QDN_EVT_PORT_SEL field.
  TMC_PERF_QDN_BUBBLE,

  //! Out of credit on an output port.  Participating ports are selected with
  //! the QDN_EVT_PORT_SEL field.
  TMC_PERF_QDN_CONGESTION,

  //! UDN Demux stalled due to buffer full.
  TMC_PERF_UDN_DEMUX_STALL,

  //! IDN Demux stalled due to buffer full.
  TMC_PERF_IDN_DEMUX_STALL,

  //! The event occurs when the Watch SPR detects a address or address range.
  TMC_PERF_WATCH,

  //! The event occurs whenever broadcast wire-0 asserts.
  TMC_PERF_BCST0,

  //! The event occurs whenever broadcast wire-1 asserts.
  TMC_PERF_BCST1,

  //! The event occurs whenever broadcast wire-2 asserts.
  TMC_PERF_BCST2,

  //! The event occurs whenever broadcast wire-3 asserts.
  TMC_PERF_BCST3,

  //! The event occurs when Performance counter-0 overflows.
  TMC_PERF_PCNT0,

  //! The event occurs when Performance counter-1 overflows.
  TMC_PERF_PCNT1,

  //! The event occurs when Auxiliary Performance counter-0 overflows.
  TMC_PERF_AUX_PCNT0,

  //! The event occurs when Auxiliary Performance counter-1 overflows.
  TMC_PERF_AUX_PCNT1,
};

//! Enable accessing perf counters from user space.
//!
//! @return 0 on success, or else -1 with errno set
//!
extern int
tmc_perf_enable_counters(void);

//! Disable accessing perf counters from user space.
//!
//! @return 0 on success, or else -1 with errno set
//!
extern int
tmc_perf_disable_counters(void);

//! Select four events from the enum below to initialize the hardware
//! counters.
//!
//! @param event1 Used to set event for PERF_COUNT_1.
//! @param event2 Used to set event for PERF_COUNT_0.
//! @param event3 Used to set event for AUX_PERF_COUNT_1.
//! @param event4 Used to set event for AUX_PERF_COUNT_0.
//! @return Zero if all of the events specified were valid, -1 otherwise;
//!  invalid events will be replaced by the ZERO event when the counters are
//!  configured.
//!
extern int
tmc_perf_setup_counters(enum tmc_perf_events_e event1,
                        enum tmc_perf_events_e event2,
                        enum tmc_perf_events_e event3,
                        enum tmc_perf_events_e event4);

//! Number of hardware performance counters.
#define TMC_PERF_NUM_COUNTERS 4

//! The counters are 32-bits, so for example, the TMC_PERF_ONE event counter 
//! will overflow in 3.5 seconds at 1.2 GHz.
//! But the event accumulators are 64-bits.
//!
struct tmc_perf_counters
{
  //! 64-bits vals to store perf counters.
  uint64_t val[TMC_PERF_NUM_COUNTERS];
};

//! Read the four hardware counters without clearing them.
//!
//! @param counters Used to store the values of the four hardware counters.
//!
extern void
tmc_perf_read_counters(struct tmc_perf_counters* counters);

//! Clear the four hardware counters
extern void
tmc_perf_clear_counters(void);

//! Accumulate counts from the four hardware counters without clearing them
//!
//! @param counters Used to store the values of the four hardware counters.
//!
extern void
tmc_perf_add_counters(struct tmc_perf_counters* counters);

//! Get the CPU speed, in hertz.
//!
//! @return CPU speed.
//!
extern uint64_t
tmc_perf_get_cpu_speed(void);

__END_DECLS

#endif // __TMC_PERF_H__

//! @}
