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


#ifndef _PKA_DRIVER_H_
#define _PKA_DRIVER_H_

#include <stdint.h>
#include <tmc/queue.h>
#include <tmc/spin.h>
#include <gxcr/pka.h>


// Configurable constants:
#define MAX_NUM_OF_CLIENTS        64
#define PKA_DRIVER_MMAP_NAME      "/pka_driver_mem"
#define PKA_DRIVER_MMAP_SIZE      (16 * MEGABYTE)
#define LOG2_OVERFLOW_QUEUE_SIZE  14

// These constants cannot or should not be changed.
#define MEGABYTE                  (1024 * 1024)
#define NUM_MICA_INSTANCES        2
#define NUM_MICA_RINGS            2   // This is # rings per mica instance!
#define OVERFLOW_QUEUE_SIZE       (1 << LOG2_OVERFLOW_QUEUE_SIZE)
#define OVERFLOW_QUEUE_MAX_SIZE   (OVERFLOW_QUEUE_SIZE - 512)


typedef enum { FALSE = 0, TRUE = 1 } boolean_t;
typedef enum { SUCCESS, FAILURE } status_t;

typedef struct  // 8 bytes long.
{
    uint32_t offset;
    uint16_t byte_len;
    uint8_t  encrypted;
    uint8_t  wraps;
} __attribute__((aligned(8))) len_offset_t;

typedef struct  // 32 bytes long
{
    void        *user_data;
    uint32_t     offset_of_operand_len_offs;
    uint32_t     operands_size;
    uint32_t     tag;
    uint32_t     epoch;
    pka_opcode_t opcode;
    uint8_t      shift_cnt;
    uint8_t      operand_cnt;
    uint8_t      client_idx;
    uint8_t      trace_flag;
} __attribute__((aligned(32))) pka_driver_cmd_desc_t;

typedef struct  // 32 bytes long
{
    void        *user_data;       // Same opaque user_data ptr passed in.
    len_offset_t result[2];
    uint8_t      opcode;
    uint8_t      result_cnt;      // 0, 1 or 2
    uint8_t      status;          // The raw result_code.
    uint8_t      compare_result;  // The raw compare_result.
} __attribute__((aligned(32))) pka_driver_result_desc_t;

TMC_QUEUE(pka_cmd,   pka_driver_cmd_desc_t,    9, TMC_QUEUE_SINGLE_RECEIVER);
TMC_QUEUE(pka_reply, pka_driver_result_desc_t, 8, (TMC_QUEUE_SINGLE_RECEIVER |
                                                   TMC_QUEUE_SINGLE_SENDER));


// Per client operand/result vector ring data structures.
//
// The operand ring holds variable length data (operand vectors) that are
// always aligned on an eight byte boundary.  The operand ring is only ever
// written by the client (the driver just reads it).  The operand ring
// descriptor is split into two different records - one used exclusively by
// the client and the other by the driver.  The only exception to this rule
// is the curr_size (which is in the client's descriptor) is atomically
// decremented by the driver.  The one strange thing about the operand ring,
// is that after all of the operands (plus padding) have been copied, there
// is an additional variable length array - the len_offset array - that is
// added to the operand ring, in order to facilitate the reader in finding
// the offsets and lengths of the operands corresponding to one command.
//
// The result ring holds variable length data (1 or 2 result vectors) that
// are always aligned on an eight byte boundary.  The result ring is only
// ever written by the driver (the client just reads it).  The result ring
// descriptor is similiarly split into a driver record and a client record.
// Unlike the operand, no additional data is required or added.

typedef struct
{
    // This is the descriptor used for writing the operand ring.  All operands
    // in these fifos MUST start on an eight byte boundary (and MUST be padded
    // with zeros).  Nevertheless, all values in this descriptor record are in
    // units of bytes.  Only atomic adds and subtracts may be used to change
    // curr_size!
    uint32_t base_offset;    // Never changed after ring init.
    uint32_t max_size;       // Never changed after ring init.
    uint32_t curr_size;
    uint32_t peak_size;
    uint32_t tail_offset;
    uint64_t bytes_added;
} __attribute__((aligned(CHIP_L2_LINE_SIZE()))) client_operands_desc_t;

typedef struct
{
    // This is the descriptor used for reading the operand ring.  All operands
    // in these fifos MUST start on an eight byte boundary (and MUST be padded
    // with zeros).  Nevertheless, all values in this descriptor record are in
    // units of bytes.
    uint32_t base_offset;    // Never changed after ring init.
    uint32_t max_size;       // Never changed after ring init.
    uint32_t head_offset;
    uint64_t bytes_removed;
} driver_operands_desc_t;

typedef struct
{
    // This is the descriptor used for writing the result ring.  All results
    // in these fifos MUST start on an eight byte boundary (and MUST be padded
    // with zeros).  Nevertheless, all values in this descriptor record are in
    // units of bytes.  Only atomic adds and subtracts may be used to change
    // curr_size!
    uint32_t base_offset;    // Never changed after ring init.
    uint32_t max_size;       // Never changed after ring init.
    uint32_t curr_size;
    uint32_t peak_size;
    uint32_t tail_offset;
    uint64_t bytes_added;
} driver_results_desc_t;

typedef struct
{
    // This is the descriptor used for reading the operand ring.  All operands
    // in these fifos MUST start on an eight byte boundary (and MUST be padded
    // with zeros).  Nevertheless, all values in this descriptor record are in
    // units of bytes.
    uint32_t base_offset;    // Never changed after ring init.
    uint32_t max_size;       // Never changed after ring init.
    uint32_t head_offset;
    uint64_t bytes_removed;
} __attribute__((aligned(CHIP_L2_LINE_SIZE()))) client_results_desc_t;

typedef struct
{
    // This descriptor describes a 64KB fifo that of course starts on a
    // cacheline boundary.  The actual random bytes added and removed can be
    // added or removed on a byte boundary.
    uint32_t base_offset;    // Never changed after ring init.
    uint32_t max_offset;     // Never changed after ring init.
    uint32_t curr_size;      // This field gets atomic incs and decrements.
    uint32_t tail_offset;
    uint32_t head_offset;
} __attribute__((aligned(CHIP_L2_LINE_SIZE()))) rand_fifo_desc_t;

typedef struct
{
    uint32_t epoch;
    uint32_t pid;
    uint8_t  initialized;
    uint8_t  client_idx;
    uint8_t  in_use;
    uint8_t  available;
    uint8_t  req_open;
    uint8_t  req_close;
} __attribute__((aligned(CHIP_L2_LINE_SIZE()))) client_desc_t;

typedef struct
{
    tmc_spin_queued_mutex_t client_idx_lock;
    uint32_t                magic_num;
    uint32_t                max_client_idx;
    uint32_t                last_idx_opened;
    client_desc_t           client_descs[MAX_NUM_OF_CLIENTS];
} __attribute__((aligned(CHIP_L2_LINE_SIZE()))) client_idxs_t;

typedef struct
{
    uint64_t requests;
    uint64_t replies;
    uint32_t peak_request_queue_size;
    uint32_t peak_reply_queue_size;
} stats_t;

// The following structures contain only 1 item per cacheline!  They contain
// fields that are operated on by the atomic instructions, and so since those
// atomic instructions invalidate all enclosing cache-lines on every atomic
// access, we try not to put anything else in the same cache line.
typedef struct
{
    // This field is in units of 8-byte "words".  It is only ever incremented,
    // and must never wrap (designed to take decades to wrap in the worst,
    // worst case.
    uint64_t words_removed;
} __attribute__((aligned(CHIP_L2_LINE_SIZE()))) pka_driver_words_t;

typedef struct
{
    uint64_t queue_cnt;
} __attribute__((aligned(CHIP_L2_LINE_SIZE()))) queue_size_t;


// Master record describing the layout of the major data structures
typedef struct
{
    // The following three offsets are used to find the actual operand ring and
    // result ring. Useful for debugging.  They are byte offsets from the start
    // of the master record.
    uint32_t first_operand_ring_offset;
    uint32_t first_result_ring_offset;
    uint32_t first_rand_ring_offset;

    // The overflow_queue_size is used to track the size of the overflow cmd
    // queue, and if it gets too close to being FULL, dontAcceptNewReqs is
    // set to 1 (TRUE).  This way, once a request successfully gets on the
    // cmdQueue, it should never be dropped later for running out of some
    // resource.
    uint32_t dont_accept_new_reqs;

    // The client_idxs record contains the data needed for each client to
    // determine its client_idx.
    client_idxs_t client_idxs;

    // Stats
    stats_t client_idx_stats[MAX_NUM_OF_CLIENTS];

    // cmdQueue is a shared structure, and sharing is controlled by the
    // internal TMC_QUEUE lock.  This is a small fixed size input queue.
    // When things get busy, the internal cmdQueue is used to handle the
    // excess load, not this queue.
    pka_cmd_t cmd_queue;

    // The operand ring descriptors.  See comments above, where the descriptor
    // types are declared.
    client_operands_desc_t client_operand_descs[MAX_NUM_OF_CLIENTS];
    driver_operands_desc_t driver_operand_descs[MAX_NUM_OF_CLIENTS];


    // The replyQueues are NOT shared queues and so do NOT need locks.  These
    // just hold the small reply descriptor.  The reply descriptor is used to
    // (amongst other things) point to the resultRings words that hold the
    // associated (could be 0, 1 or 2) large variable length "big numbers"
    // "answers".
    pka_reply_t  reply_queues[MAX_NUM_OF_CLIENTS];
    queue_size_t reply_queue_sizes[MAX_NUM_OF_CLIENTS];

    // The result ring descriptors.  See comments above, where the descriptor
    // types are declared.
    driver_results_desc_t driver_result_descs[MAX_NUM_OF_CLIENTS];
    client_results_desc_t client_result_descs[MAX_NUM_OF_CLIENTS];


    // These descriptors are used to describe the per client random number
    // fifos.  There is one such fifo per client.  The rand byte ring itself
    // is ONLY ever written to be the driver and similarly most of the fields
    // in the descriptor must only be written by the driver, except that the
    // client MUST do an atomic decrement of the curr_size field.  Similarly
    // the driver must only do an atomic incrment of this field.
    rand_fifo_desc_t rand_fifos[MAX_NUM_OF_CLIENTS];

    // Here is where the actual operand and result ring memory starts.  It is
    // carved up by the pka_driver at initialization time to give all clients
    // an equal share.
    uint8_t mem[0];
} master_record_t;

#endif
