/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors. The
 *   software is licensed under the Tilera MDE License.
 *
 *   Unless otherwise agreed by Tilera in writing, you may not remove or
 *   alter this notice or any other notice embedded in Materials by Tilera
 *   or Tilera's suppliers or licensors in any way.
 */

#ifndef _GXIO_MICA_QUEUE_H_
#define _GXIO_MICA_QUEUE_H_

#include <stdint.h>
#include <gxio/mica.h>


__BEGIN_DECLS

/**
 * @file
 *
 * An API for allocating and manipulating MiCA Queues.
 */

/**
 * @addtogroup gxio_mica_queue
 * @{
 *
 * The MiCA&tm; (Multicore iMesh Coprocessing Accelerator) shim provides a
 * common front end to various acceleration functions such as crypto
 * or compression.  The gxio_mica_ API, declared in <gxio/mica.h>,
 * provides a software programming interface to that common front-end.
 * Familiarity with that documentation is assumed here.
 *
 * This file adds a MiCA queuing mechanism on top of the gxio_mica_ API.
 * This allows an enhancement to the lower level primitives in <gxcr/mica.h>,
 * by declaring a more powerful data structure and letting the code deal with
 * the lower level details of mapping a set of MiCA hardware contexts onto this
 * queue.  Using this interface, you can ignore the details of dealing with
 * individual mica contexts.
 *
 * When creating a mica_queue, there are three top level decisions to make.
 *
 * The first decision is whether to make the queue shared or not.  By default
 * the queue is created non-shared, which means that only one thread should be
 * calling the main post-initialization routines of gxio_mica_queue_op,
 * gxio_mica_queue_sync_op, gxio_mica_queue_poll and gxio_mica_get_result.
 * In other words  no locks are used to protect against these functions being
 * called by different threads.  Instead a queue can be created shared using the
 * queue_flags parameters to gxio_mica_create_queue.  When only
 * SHARED_MICA_QUEUE is set on a call to gxio_mica_create_queue then locking
 * code is enabled for all functions that access the common request fifo and
 * when accessing the common HW MiCA contexts.  In this case, it is assumed that
 * only one thread will access any given result fifo, and so locks are not
 * used on calls to gxio_mica_get_result (and may or may not be used for a call
 * to gxio_mica_queue_poll depending on whether it needs to change the common
 * internal state).  If this later assumption is not true, then an additional
 * flag - SHARED_RESULT_FIFO - can also be specfied in addition to
 * SHARED_MICA_QUEUE which will enable all locking code - at the cost of
 * slightly worse performance. Note that specifying SHARED_RESULT_FIFO without
 * SHARED_MICA_QUEUE does not make any sense.  Also shared mica_queues can only
 * be shared by multiple threads within a single process.
 *
 * The second decision is whether to supply the memory needed or to let
 * mica_queue code do that automatically.  The gxio_mica_create_queue
 * gxio_mica_create_queue function allows the user to supply the
 * memory that is turned into a mica_queue object.  The reason to
 * allow this is to give the caller control over the "type" of memory
 * allocated - e.g. whether shared across processes or whether the
 * memory is locally homed or uses hash for home.  If the user doesn't
 * wish to allocate the memory, then passing in NULL causes
 * gxio_mica_create_queue to use the standard libc malloc to acquire the
 * needed memory.
 *
 * The third decision is whether to use the User Space interrupt handlers.
 * This capability is specfied by the USE_COMPLETION_INTERRUPT queue flag.
 * NOTE: THIS VERSION CURRENTLY DOES NOT IMPLEMENT "USE_COMPLETION_INTERRUPT"!
 *
 * Note that some functions in this modules can be called from any thread.
 * For example gxio_mica_queue_count, gxio_mica_queue_split_ctxt_request and
 * gxio_mica_queue_mem_size neither read nor write any internal state and so
 * can be called from any thread.  The gxio_mica_create_queue,
 * gxio_mica_queue_register_page and gxio_mica_queue_stats functions can also
 * be called from any thread. On the other hand, gxio_reserve_result_fifo,
 * gxio_mica_queue_op, gxio_mica_queue_sync_op, gxio_mica_queue_poll, and
 * gxio_mica_get_result functions have constraints on which threads can call
 * them.
 */

/* The following constant limits generally should not be increased. */
#define MAX_MICA_SHIM_IDX       1   ///< 1 for GX
#define MAX_CONTEXTS_PER_QUEUE  16  ///< Max num of HW contexts per mica queue
#define MAX_RESULT_FIFO_CNT     16  ///< Max num result fifos per mica queue


// Just placeholder error codes for now.
#define UNINITIALIZED_QUEUE_ERR  -1991  ///< Use of closed queue in an API call
#define REQUEST_QUEUE_FULL       -1992  ///< Op rejected bceause queue was full
#define QUEUE_OP_TIMEOUT_ERR     -1993  ///< Operation timed out

/** gxio_mica_result_t
 *
 * The gxio_mica_result_t record is used to hold the result of a MiCA
 * operation.
 */
typedef struct
{
    uint64_t user_ptr;        ///< The opaque pointer supplied with the request.
    uint64_t context_status;  ///< The 64-bit status register value.
} gxio_mica_result_t;

/** gxio_mica_queue_t
 *
 * The gxio_mica_queue_t is the main context handle used by almost all of the
 * functions in this header file.
 */
typedef struct gxio_mica_queue_s  gxio_mica_queue_t;

/** gxio_mica_kind_t
 *
 * gxio_mica_kind_t is a shorter synonym for  gxio_mica_accelerator_type_t.
 */
typedef gxio_mica_accelerator_type_t gxio_mica_kind_t;


/** mica_ctxt_req_t
 *
 * The mica_ctxt_req_t record is used to describe the number of MiCA HW
 * contexts that must and/or should be associated with this mica_queue.
 * An array of the desired number of mica HW contexts to be assigned to this
 * queue, ordered by mica_shim index.
 */
typedef struct
{
    uint8_t num_contexts[MAX_MICA_SHIM_IDX + 1];  ///< num contexts per shim
} mica_ctxt_req_t;


/** Mica_queue flags.
 *
 * Currently there are three mica_queue flags defined -
 * USE_COMPLETION_INTERRUPT:
 * When set on a call to gxio_mica_create_queue, it causes the code here to
 * to use the IPI mechanism and the MiCA completion interrupt to advance the
 * request fifo.  The client code still needs to call gxio_mica_get_result,
 * and gxio_mica_consume_result - but does not need to (and shouldn't) call
 * gxio_mica_queue_poll anymore.
 * NOTE: THIS VERSION CURRENTLY DOES NOT IMPLEMENT "USE_COMPLETION_INTERRUPT"!
 *
 * SHARED_MICA_QUEUE:
 * When set on a call to gxio_mica_create_queue, it causes the code to use
 * a queued spin lock from the <tmc/spin.h> module so that the code can
 * properly handle multiple threads accessing the same mica_queue.
 *
 * SHARED_RESULT_FIFO:
 * Only used if SHARED_MICA_QUEUE is also set.  When set on a call to
 * gxio_mica_create_queue, it causes the code to use multiple result fifos
 * for a single mica_queue.
 */
typedef uint32_t mica_queue_flags_t;

#define SHARED_MICA_QUEUE         0x01  ///< Share the mica_queue across threads
#define SHARED_RESULT_FIFO        0x02  ///< Allow multiple result fifos
#define USE_COMPLETION_INTERRUPT  0x08  ///< Use Interrupts. NOT YET IMPLEMENTED


/** Determine the suggested number of mica_queues to use.
 *
 * This function can be used to pick a reasonable number of mica_queues to
 * create given a number of threads and number of mica contexts available.
 *
 * @param num_of_threads     The number of threads using mica_queues.
 * @param num_mica_contexts  The number of MiCA hardware contexts that are to
 *                           be used by the threads above.
 * @return                   Returns a reasonable value for how to divide up
 *                           the given number of MiCA contexts amongst the
 *                           given number of threads.
 */

uint32_t gxio_mica_queue_count(uint32_t num_of_threads,
                               uint32_t num_mica_contexts);


/** Split a total ctxt_request into N pieces
 *
 * This function can be used to take an overall total number of MiCA contexts
 * and fairly evenly divide it up into N separate ctxt_requests in order to
 * create N mica_queues.  Note that it will not assign more than
 * MAX_CONTEXTS_PER_QUEUE to any separate ctxt_request, and so it is possible
 * for it to not assign all of the supplied mica contexts.  Because of this
 * this function returns the actual number of mica contexts used.
 *
 * @param total_ctxt_request   A pointer to a single mica_ctxt_req_t which gives
 *                             the total number of MiCA hardware contexts - per
 *                             MiCA shim - to be distributed amongst the given
 *                             number of mica_queues.
 * @param num_of_queues        The number of mica_queues to be used.
 * @param ctxt_requests        An array mica_ctxt_req_t - one for each MiCA
 *                             queue.
 * @return                     Returns the number of mica contexts used.
 */

uint32_t gxio_mica_queue_split_ctxt_request(mica_ctxt_req_t *total_ctxt_request,
                                            uint32_t         num_of_queues,
                                            mica_ctxt_req_t  ctxt_requests[]);


/** Determine the mica_queue memory size.
 *
 * This function can be used to determine the size of a contiguous memory area
 * that can be used by gxio_mica_create_queue. This is needed because the
 * function gxio_mica_create_queue can be given the memory it is to use - in
 * which case this function can be used to determine how much memory is
 * required.
 *
 * @param type                The type of MiCA - i.e. compression or bulk
 *                            cryptography.
 * @param req_fifo_entries    The desired number of entries in the request fifo.
 *                            This should be a fairly big number, i.e. a number
 *                            such that if the request fifo gets this big, means
 *                            that it is probably worth discarding this MiCA
 *                            operation altogther (i.e. thousands of entries).
 *                            This number does NOT have to be a power of 2.
 * @param result_fifo_cnt     The desired number of result fifos.  The main use
 *                            for multiple result fifos is when a mica_queue is
 *                            shared amongst several threads and each thread
 *                            only wants to see its own results.
 *                            This number does NOT have to be a power of 2.
 * @param result_fifo_entries The desired number of entries in each result fifo.
 *                            Setting this too small could cause the result fifo
 *                            to fill up.  This number does NOT have to be a
 *                            power of 2.
 * @param ctxt_request        A description of the desired and required number
 *                            of MiCA HW contexts to make available to this
 *                            queue. These contexts become exclusive to this
 *                            queue and cannot be used by other mica_queues or
 *                            by regular mica start_ops.
 * @param queue_flags         See mica_queue flags description above.  Used for
 *                            indicating options like interrupt handler support
 *                            and locking support.
 * @return                    The number of bytes needed by a mica_queue object
 *                            with the given parameterization.
 */

uint32_t gxio_mica_queue_mem_size(gxio_mica_kind_t   type,
                                  uint32_t           req_fifo_entries,
                                  uint32_t           result_fifo_cnt,
                                  uint32_t           result_fifo_entries,
                                  mica_ctxt_req_t   *ctxt_request,
                                  mica_queue_flags_t queue_flags);


/** Initialize a mica_queue.
 *
 * Initializes and also possibly allocates a MiCA software queue.  Each
 * such queue has a dedicated set of MiCA contexts bound to it which it
 * efficiently manages.  Contexts remain allocated until the queue is destroyed
 * with gxio_mica_destroy_queue or when the allocating process is terminated.
 * This function can either be given the memory to use OR it can internally
 * use a basic malloc call to get the memory it needs.  When the user wishes
 * to provide the memory, the gxio_mica_queue_mem_size function can be used
 * to determine how much memory is required.
 *
 * @param mem_ptr             The memory to use when initializing the queue.
 *                            If mem_ptr is NULL or too small (as determined by
 *                            mem_size), this function will instead use malloc
 *                            to aquire the needed memory.
 * @param mem_size            The size in bytes of the memory pointed to by
 *                            mem_ptr.  Should be 0 when mem_ptr is NULL.
 * @param type                The type of MiCA - i.e. compression or bulk
 *                            cryptography.
 * @param req_fifo_entries    The desired number of entries in the request fifo.
 *                            This should be a fairly big number, i.e. a number
 *                            such that if the request fifo gets this big, means
 *                            that it is probably worth discarding this MiCA
 *                            operation altogther (i.e. thousands of entries).
 *                            This number does NOT have to be a power of 2.
 * @param result_fifo_cnt     The desired number of result fifos.  The main use
 *                            for multiple result fifos is when a mica_queue is
 *                            shared amongst several threads and each thread
 *                            only wants to see its own results.  In such a case
 *                            the number of threads (and hence result fifos)
 *                            should range from 2 to 8.  Sharing a single
 *                            queue with more than 8 threads is not recommended.
 *                            This number does NOT have to be a power of 2.
 * @param result_fifo_entries The desired number of entries in each result fifo.
 *                            Setting this too small could cause the result fifo
 *                            to fillup.  This number does NOT have to be a
 *                            power of 2.
 * @param ctxt_request        A description of the desired and required number
 *                            of MiCA HW contexts to make available to this
 *                            queue. These contexts become exclusive to this
 *                            queue and cannot be used by other mica_queues or
 *                            by regular mica start_ops.
 * @param queue_flags         See mica_queue flags description above.  Used for
 *                            indicating options like interrupt handler support
 *                            and locking support.
 * @return                    NULL on error, a pointer to the newly created
 *                            queue object on success.  Note that if sufficient
 *                            memory was supplied via mem_ptr and mem_size, then
 *                            the returned ptr will have the same address as
 *                            mem_ptr.
 */

gxio_mica_queue_t *gxio_mica_create_queue(void              *mem_ptr,
                                          uint32_t           mem_size,
                                          gxio_mica_kind_t   type,
                                          uint32_t           req_fifo_entries,
                                          uint32_t           result_fifo_cnt,
                                          uint32_t           result_fifo_entries,
                                          mica_ctxt_req_t   *ctxt_request,
                                          mica_queue_flags_t queue_flags);


/** Reserve a free result fifo
 *
 * @param queue  A pointer to an initialized MiCA software queue.
 * @return       Returns a result_fifo_idx that has not been previously been
 *               returned, or < 0 if there is an error.
 */

int32_t gxio_reserve_result_fifo(gxio_mica_queue_t *queue);


/** Destroy a mica_queue.
 *
 * This function frees the MiCA contexts associated with this mica_queue,
 * and also frees the underlying memory used, if the memory was allocated in
 * gxio_mica_create_queue.  Of course, if gxio_mica_create_queue was supplied
 * with the memory by the caller to gxio_mica_create_queue, then this function
 * will leave it for the caller to potentially free if they desire.
 *
 * If a process exits without calling this routine, the kernel will destroy
 * the associated MiCA contexts as part of process teardown.
 *
 * @param  queue A pointer to an initialized MiCA software queue.
 * @return       Zero on success, UNINITIALIZED_QUEUE_ERR when called on a
 *               queue that hasn't be created with gxio_mica_create_queue or
 *               was previously destroyed by gxio_mica_destroy_queue.
 */

int gxio_mica_destroy_queue(gxio_mica_queue_t *queue);


/** Register a page with a mica sofware queue
 *
 * This is equivalent to registering this memory with each MiCA HW context
 * associated with this mica_queue.  All source, destination, and extra data
 * memory must be registered via this function.  Up to 16 pages may be
 * registered per context.
 *
 * @param queue      A pointer to an initialized MiCA software queue.
 * @param page       Starting VA of a contiguous memory page, must be
 *                   page-aligned.
 * @param page_size  Size of the page in bytes, must be a page size that is
 *                   supported by the TILE-Gx.
 * @param page_flags gxio_mica_mem_flags_e memory flags.
 * @return           Zero on success, EINVAL if page does not map a contiguous
 *                   page, GXIO_ERR_IOTLB_ENTRY if no more IOTLB entries are
 *                   available.
 */

int gxio_mica_queue_register_page(gxio_mica_queue_t *queue,
                                  void              *page,
                                  size_t             page_size,
                                  uint32_t           page_flags);


/** Queue a MiCA operation (asynchronously - i.e. non blocking).
 *
 * This function is used to add a MiCA request to the given mica software queue.
 * If there is a free associated MiCA HW context, then this request is loaded
 * directly into a HW context, otherwise it is queued until such time as a
 * HW context becomes available.
 *
 * @param queue      A pointer to an initialized MiCA software queue.
 * @param result_fifo_idx
 * @param src        Pointer to source data, memory must be registered with
 *                   the MiCA shim via gxio_mica_register_page().
 * @param dst        Pointer to destination memory, memory must be registered
 *                   with the MiCA shim via gxio_mica_register_page().
 * @param extra_data Pointer to 'extra data', used by some operations.
 *                   Memory must be registered with the MiCA shim via
 *                   gxio_mica_register_page().  NULL if no extra data is used.
 * @param opcode     The value to put in the MICA_OPCODE register, which
 *                   triggers the start of the context operation.
 * @param user_ptr   An opaque pointer that is associated with this specific
 *                   request which can be used to match the result.
 * @return           Returns zero on success, or one of the negative error codes
 *                   listed at the begin of this file on failure.
 */

int gxio_mica_queue_op(gxio_mica_queue_t *queue,
                       uint32_t           result_fifo_idx,
                       void              *src,
                       void              *dst,
                       void              *extra_data,
                       gxio_mica_opcode_t opcode,
                       void              *user_ptr);


/** Synchronous version of gxio_mica_queue_op.
 *
 * gxio_mica_queue_sync_op is a blocking version of gxio_mica_queue_op, but
 * otherwise implements the same MiCA ops.  In particular, when calling this
 * function, if a MiCA HW context is not immediately available, this funcvtion
 * will continually do the equivalent of a gxio_mica_queue_poll.  Once a HW
 * context is available, these parameters are loaded into it and the context
 * will be started.  Then this function will continue to do the equivalent of
 * a gxio_mica_queue_poll, until it notices that its specific request has been
 * completed in which case it returns the associated context_status.  Note that
 * in general, the result is not put on any result_fifo, and so this function
 * returns its associated result ahead of any gxio_mica_queue_op results that
 * may be older.  I.e. though there may have been many results in the result
 * fifo at the time this function was called, this function does not wait for
 * them to be read out or processed before returning its result.
 * Frequently use of this function, especially in a non-shared queue environment
 * can lead to poor overall system performance.
 *
 * @param queue      A pointer to an initialized MiCA software queue.
 * @param result_fifo_idx
 * @param src        Pointer to source data, memory must be registered with
 *                   the MiCA shim via gxio_mica_register_page().
 * @param dst        Pointer to destination memory, memory must be registered
 *                   with the MiCA shim via gxio_mica_register_page().
 * @param extra_data Pointer to 'extra data', used by some operations.
 *                   Memory must be registered with the MiCA shim via
 *                   gxio_mica_register_page().  NULL if no extra data is used.
 * @param opcode     The value to put in the MICA_OPCODE register, which
 *                   triggers the start of the context operation.
 * @param max_cycles The maximum amount of time, in units of clock cycles, that
 *                   this function will wait.  This parameter needs to
 *                   be set to a large number because, if it ever times out a
 *                   request, the queue could be left in a inconsistent state.
 * @param context_status  status from the MiCA HW context at the end of the
 *                        operation
 * @return           Returns zero on success, or one of the negative error codes
 *                   listed at the begin of this file on failure.
 *
 */

int gxio_mica_queue_sync_op(gxio_mica_queue_t *queue,
                            uint32_t           result_fifo_idx,
                            void              *src,
                            void              *dst,
                            void              *extra_data,
                            gxio_mica_opcode_t opcode,
                            uint64_t           max_cycles,
                            uint64_t          *context_status);


/** Queue Polling.
 *
 * The function gxio_mica_queue_poll is used to check all of the busy mica
 * contexts for done operations, and when a done operation is found,
 * put the result on the end of the result fifo and start a new mica_op if
 * the request fifo is not empty.  If the system knows that there are no busy
 * mica contexts (and hence no entries on the request queue), then it can stop
 * calling this function.  This function is suitable to be called as an
 * interrupt handler (presumably, as an IPI handler for a mica completion
 * interrupt).  This function should be called frequently when there are busy
 * micas and the system is not using completion interrupts.
 *
 * @param  queue            A pointer to an initialized MiCA software queue.
 * @param  result_fifo_idx  The index of the result result fifo.
 * @return                  Always returns the number of entries in the result
 *                          fifo.
 */

uint32_t gxio_mica_queue_poll(gxio_mica_queue_t *queue,
                              uint32_t           result_fifo_idx);


/** Get the next mica_result from the completion of a MiCA operation.
 *
 * Returns a pointer to the oldest completed mica_result in the result fifo,
 * (i.e. the head of the mica_queue result fifo) or NULL if the result fifo is
 * empty.
 *
 * @param queue            A pointer to an initialized MiCA software queue.
 * @param result_fifo_idx  The index of the result result fifo.
 * @param result_ptr       A pointer to client memory where the next result can
 *                         be copied into.
 * @return                 Returns zero if the result fifo is empty, one if a
 *                         result entry was found and returned (advancing the
 *                         result head pointer).
 */

int gxio_mica_get_result(gxio_mica_queue_t  *queue,
                         uint32_t            result_fifo_idx,
                         gxio_mica_result_t *result_ptr);


/** Record for receiving mica_queue stats. */
typedef struct
{
    /** Total number of requests submitted since the queue was created or the
     *  last clear_counters command */
    uint64_t total_requests;

    /** Total number of gxio_mica_queue_sync_op calls since the queue was
     *  created or the last clear_counters command */
    uint64_t total_sync_ops;

    /** Total number of queue_ops that couldn't immediately be assigned a HW
     *  context and so had to be added to the req_fifo. */
     uint64_t total_req_fifo_enqueues;

    /** Total number of MiCA ops that were completed since the queue was
     *  created or the last clear_counters command */
    uint64_t total_dones;

    /** Total number of queue_ops that couldn't be added to the req fifo
     *  because it was full. */
    uint32_t req_fifo_overflows;

    /** Total number of MiCA ops that were completed, but whose results where
     *  discarded because the result fifo was full. */
    uint32_t result_fifo_overflows;

    /** The number of MiCA HW contexts owned by this queue. */
    uint32_t num_contexts;

    /** The number of MiCA HW contexts that are currently busy. */
    uint32_t cur_contexts_busy;

    /** The peak number of MiCA HW contexts that were simultaneously busy since
     *  the queue was created or the last clear_counters command. */
    uint32_t max_contexts_busy;

    /** The current number of entries in the request fifo. */
    uint32_t cur_req_fifo_cnt;

    /** The peak value of cur_req_fifo_cnt since the queue was created or the
     *  last clear_counters command. */
    uint32_t max_req_fifo_cnt;

    /** The current number of entries in the result fifo(s). */
    uint32_t cur_result_fifo_cnt[MAX_RESULT_FIFO_CNT];

    /** The peak value of cur_result_fifo_cnt since the queue was created or
     *  the last clear_counters command. */
    uint32_t max_result_fifo_cnt[MAX_RESULT_FIFO_CNT];
} mica_queue_stats_t;


/** Mica_queue statistics.
 *
 * The gxio_mica_queue_stats is used to get the current fifo counts, the
 * the maximum fifo counts (since the last time this function was called or the
 * queue was created), and the total number of requests and completed MiCA
 * operations (since the last time this function was called or the
 * queue was created).
 *
 * @param queue           A pointer to an initialized MiCA software queue.
 * @param clear_counters  If not zero, then after gathering the queue stats,
 *                        this function clears the following counters -
 *                        total_requests, total_sync_ops,
 *                        total_req_fifo_enqueues, total_dones,
 *                        req_fifo_overflows, result_fifo_overflows,
 *                        max_contexts_busy, max_req_fifo_cnt and
 *                        max_result_fifo_cnt.
 * @param stats           A pointer to client memory where the stats are copied.
 */

void gxio_mica_queue_stats(gxio_mica_queue_t  *queue,
                           uint8_t             clear_counters,
                           mica_queue_stats_t *stats);


/** @} */

__END_DECLS

#endif /* _GXIO_MICA_H_ */
