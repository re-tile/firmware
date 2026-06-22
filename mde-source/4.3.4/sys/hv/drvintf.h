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

/**
 * @file
 * Device driver interface routines.
 */

#ifndef _SYS_HV_DRVINTF_H
#define _SYS_HV_DRVINTF_H

#ifndef __ASSEMBLER__

#include <limits.h>

#include <hv/hypervisor.h>

#include "bits.h"
#include "board_info.h"
#include "drvchan.h"
#include "param.h"
#include "types.h"
#include "downcall.h"

#include "hvbme/sig.h"

// For various "shim_type" values.


/** Maximum length of a driver or device name, including the trailing nul. */
#define DRV_NAME_MAX 64

/** Maximum number of shared tiles per instance. */
#define DRV_STILE_MAX 1

/** Maximum number of dedicated tiles per instance. */
#define DRV_DTILE_MAX 7

/** pread/pwrite calls no larger than this are guaranteeed to be handled
 * as one pread or pwrite on the shared tile. */
#define DRV_ATOMIC_LEN  256

/** Maximum length, in bytes, of a message passed to drv_send_msg(),
 * or a reply passed to drv_reply_msg(). */
#define DRV_MAX_MSG_LEN 256

/** Driver-specific bits in the device handle.
 */
#define DRV_BITS_SHIFT 0                                  ///< Driv bits shift
#define DRV_BITS_WIDTH 24                                 ///< Driv bits width
#define DRV_BITS_RMASK RMASK(DRV_BITS_WIDTH)              ///< Driv bits RJ mask
#define DRV_BITS_MASK  (DRV_BITS_RMASK << DRV_BITS_SHIFT) ///< Driv bits mask

/** Convert a device handle to the driver-specific state bits. */
#define DRV_HDL2BITS(devhdl) (((devhdl) >> DRV_BITS_SHIFT) & DRV_BITS_RMASK)

/** Device information structure. */
struct dev_info
{
  pos_t stiles[DRV_STILE_MAX];    /**< Shared tiles for this device */
  int num_stiles;                 /**< Number of shared tiles */
  pos_t dtiles[DRV_DTILE_MAX];    /**< Dedicated tiles for this device */
  int num_dtiles;                 /**< Number of dedicated tiles */
  pos_t idn_ports[MAX_IDN_PORTS]; /**< Address you'd use to talk to IDN ports */
  int num_idn_ports;              /**< Number of IDN ports */
  pos_t mdn_ports[MAX_MDN_PORTS]; /**< Off-grid coordinates of the MDN ports */
  int num_mdn_ports;              /**< Number of MDN ports */
  unsigned long  channel;         /**< Channel device is on */
  int intchan;                    /**< Lowest interrupt channel assigned to
                                       device */
  int num_intchan;                /**< Number of interrupt channels assigned to
                                       device */
  const char* name;               /**< Base name of device */
#if MAX_DEVICE_CLOCKS > 0
  long speeds[MAX_DEVICE_CLOCKS]; /**< Requested speed settings in Hz */
#endif
};

/** Additional flags passed to driver entry points, and/or passed as the
 *  flags value for hv_drv_xxx_remote() calls.  (Flags passed by the
 *  client are defined in hypervisor.h; these flags are supplied by the
 *  hypervisor.)
 */

/** This is part of a larger transfer, and there are more requests to come
 *  before it's complete.  Passed to driver entry points only. */
#define DRV_FLG_PARTIAL  0x80000000

/** This request terminates a transfer early; it is acknowledged but no
 *  further I/O is done.  Passed to driver entry points only. */
#define DRV_FLG_FAULT    0x40000000

/** Virtual addresses passed as arguments are hypervisor VA's, not client VA's,
 *  and need not be validated.  Passed to driver entry points or passed
 *  to hv_drv_xxx_remote() calls. */
#define DRV_FLG_HVADDR   0x20000000

/** This is a second-hop remote request.  Normally, a driver entry point
 *  which was invoked due to a hv_drv_pread/pwrite/preada/pwritea_remote()
 *  call on another tile is not allowed to call hv_drv_xxx_remote() again
 *  to forward the request to a third tile.  This is allowed if this flag
 *  is set in the second hv_drv_xxx_remote() invocation.  (Such a request
 *  may not be forwarded to a fourth tile; only two hops are allowed.)
 *  Passed to hv_drv_xxx_remote() calls only. */
#define DRV_FLG_2NDHOP   0x10000000

/** Token used in replying to a driver message. */
typedef uint32_t drv_reply_msg_token_t;

// Driver entry points.

/**
 *  Probe a hypervisor device.  A probe() routine will be called once
 *  on every tile for each device instance, before any other calls are made
 *  to any other driver entry points for that instance.  This happens as
 *  part of the device probe process, before the hypervisor configuration
 *  file has been parsed, and thus before it is known which driver is
 *  responsible for the device, or in fact whether the device will be
 *  used at all.  Therefore, the probe routine from the first driver in
 *  the drivers table which claims to handle the relevant device type
 *  will be used.
 *
 *  The probe routine is responsible for determining whether the device
 *  is usable; it may do I/O accesses to the device to determine this
 *  if needed.  If the device is not usable, the routine should return an
 *  appropriate error code, and in this case the device will be treated
 *  as if it is not present.  In this case it is advisable for the probe
 *  routine to disable the relevant hardware.
 *
 *  Since the device might not ever be enabled, and, if enabled, might
 *  not be driven by the driver whose probe routine is used, the routine
 *  should in general not allocate any memory or set any driver state
 *  which would be depended upon by a later driver routine.
 *
 * @param instance Instance number of the device.
 * @param info Information about device shim characteristics.
 * @return Zero if the device may be used, or a negative error code.
 */
typedef int drv_probe_func(const char* drvname, int instance,
                           pos_t tile, const struct dev_info* info);


/** Initialize a hypervisor device.  A driver's init() routine will be called
 *  once on every tile for each of its device instances, after the probe()
 *  routine has been called, but before any other calls are made to other
 *  driver entry points for that instance on that tile.
 *
 * @param drvname Name of the driver handling the device.  This parameter does
 *        not point to persistent storage; the name must be copied if it is
 *        to be used after this routine returns.
 * @param statep Driver state pointer.  The driver may write the void* that
 *        this parameter points to; that void* will be passed to all other
 *        driver entry points.  Each driver instance has a separate state
 *        pointer.
 * @param instance Instance number of the device.
 * @param tile Tile coordinates of the current tile.
 * @param tileno 0 if this is not a dedicated or shared tile; a positive tile
 *        ordinal if this is a dedicated tile; or a negative tile ordinal if
 *        this is a shared tile.  The tile ordinal is the index of the tile
 *        within info->tiles[], plus one.
 * @param info Information about device shim characteristics.  This
 *        parameter points to persistent storage and thus may be saved by
 *        the driver for later use.
 * @param args Argument string from the configuration file, or NULL if no
 *        arguments.  This parameter does not point to persistent storage;
 *        the argument string must be copied if it is to be used after this
 *        routine returns.
 * @return Zero if the init is successful, or a negative error code.
 */
typedef int drv_init_func(const char* drvname, void** statepp, int instance,
                          int tileno, pos_t tile, const struct dev_info* info,
                          const char* args);

/** Open a hypervisor device.
 * @param devhdl Device handle of the device to be opened.  This is an
 *        incomplete handle provided for use with drv_open_remote() and should
 *        not be otherwise used by the driver.
 * @param statep Driver state pointer.
 * @param suffix Name of the device minus the leading device name and instance
 *        number; so, if "xgbe/0/ctl" were opened, suffix would be "/ctl".
 * @param flags Flags.
 * @param tile Tile coordinates of the tile on which the request was made.
 * @return A positive integer device handle, or a negative error code.
 *         The device handle will be supplied with any other calls to the
 *         device with the same suffix and instance number.
 */
typedef int drv_open_func(int devhdl, void* statep, const char* suffix,
                          uint32_t flags, pos_t tile);

/** Close a hypervisor device.
 * @param devhdl Device handle of the device to be closed.
 * @param statep Driver state pointer.
 * @param tile Tile coordinates of the tile on which the request was made.
 * @return Zero if the close is successful, otherwise, a negative error code.
 */
typedef int drv_close_func(int devhdl, void* statep, pos_t tile);

/** Close all of the opened instances of the device.
 * @param dev_idx Device index in the device table.
 * @param statep Driver state pointer.
 * @return Zero if successful, otherwise, a negative error code.
 */
typedef int drv_close_all_func(int dev_idx, void* statep);


/** Read data from a hypervisor device synchronously.
 * @param devhdl Device handle of the device to be read from.
 * @param statep Driver state pointer.
 * @param flags Flags.
 * @param va Virtual address of the target data buffer.  This is a client VA,
 *        and must be checked for safety before the data is be accessed.
 * @param len Number of bytes to be transferred.
 * @param offset Driver-dependent offset.
 * @param tile Tile coordinates of the tile on which the request was made.
 * @return A non-negative value if the read was at least partially successful;
 *         otherwise, a negative error code.
 */
typedef int drv_pread_func(int devhdl, void* statep, uint32_t flags, char* va,
                           uint32_t len, uint64_t offset, pos_t tile);

/** Write data to a hypervisor device synchronously.
 * @param devhdl Device handle of the device to be written to.
 * @param statep Driver state pointer.
 * @param flags Flags.
 * @param va Virtual address of the source data buffer.  This is a client VA,
 *        and must be checked for safety before the data is accessed.
 * @param len Number of bytes to be transferred.
 * @param offset Driver-dependent offset.
 * @param tile Tile coordinates of the tile on which the request was made.
 * @return A non-negative value if the write was at least partially successful;
 *         otherwise, a negative error code.
 */
typedef int drv_pwrite_func(int devhdl, void* statep, uint32_t flags, char* va,
                            uint32_t len, uint64_t offset, pos_t tile);

/** Request an interrupt when a device condition is satisfied.
 * @param devhdl Device handle of the device to be polled.
 * @param statep Driver state pointer.
 * @param events Flags denoting the events which will cause the interrupt to
 *        be delivered.
 * @param intarg First parameter which will be delivered to the device
 *        interrupt vector.
 * @param tile Tile coordinates of the tile on which the request was made.
 * @return Zero if the interrupt was successfully scheduled; otherwise, a
 *         negative error code.
 */
typedef int drv_poll_func(int devhdl, void* statep, uint32_t events,
                          uint32_t intarg, pos_t tile);

/** Cancel a request for an interrupt when a device event occurs.
 * @param devhdl Device handle of the device on which to cancel polling.
 * @param statep Driver state pointer.
 * @param tile Tile coordinates of the tile on which the request was made.
 * @return Zero if the poll was successfully canceled; otherwise, a negative
 *         error code.
 */
typedef int drv_poll_cancel_func(int devhdl, void* statep, pos_t tile);

/** Read data from a hypervisor device asynchronously.
 * @param devhdl Device handle of the device to be read from.
 * @param statep Driver state pointer.
 * @param flags Flags.
 * @param sgl_len Number of elements in the scatter-gather list.
 * @param sgl Scatter-gather list describing the memory from which data will be
 *        read.
 * @param offset Driver-dependent offset.
 * @param intarg First parameter which will be delivered to the device
 *        interrupt vector.
 * @param tile Tile coordinates of the tile on which the request was made.
 * @return Zero if the read was successfully scheduled; otherwise, a negative
 *         error code.
 */
typedef int drv_preada_func(int devhdl, void* statep, uint32_t flags,
                            uint32_t sgl_len, HV_SGL sgl[sgl_len],
                            uint64_t offset, uint32_t intarg, pos_t tile);

/** Write data to a hypervisor device asynchronously.
 * @param devhdl Device handle of the device to be written to.
 * @param statep Driver state pointer.
 * @param flags Flags.
 * @param sgl_len Number of elements in the scatter-gather list.
 * @param sgl Scatter-gather list describing the memory from which data will be
 *        read.
 * @param offset Driver-dependent offset.
 * @param intarg First parameter which will be delivered to the device
 *        interrupt vector.
 * @param tile Tile coordinates of the tile on which the request was made.
 * @return Zero if the write was successfully scheduled; otherwise, a negative
 *         error code.
 */
typedef int drv_pwritea_func(int devhdl, void* statep, uint32_t flags,
                             uint32_t sgl_len, HV_SGL sgl[sgl_len],
                             uint64_t offset, uint32_t intarg, pos_t tile);

/** Receive a driver-to-driver message.
 * @param devhdl Device handle of the device.
 * @param statep Driver state pointer.
 * @param token Token which must be passed to drv_msg_reply() when replying
 *        to the message.
 * @param msg The message which was received; this will be word-aligned.
 * @param msglen Number of bytes in the message; will not exceed
 *        DRV_MAX_MSG_LEN.
 * @param tile Tile coordinates of the tile on which the request was made.
 */
typedef void drv_msg_func(int devhdl, void* statep, drv_reply_msg_token_t token,
                          void* msg, int msglen, pos_t tile);


/** Run a dedicated driver tile's service routine.
 * @param statep Driver state pointer.
 * @return A negative error code; normally this routine never returns.
 */
typedef int drv_service_func(void* statep);


/** Get the current setting for a device clock frequency.
 * @param info Information about device shim characteristics.
 * @param clock_index Index of the clock (0 for the first clock, 1 for the
 *  second, etc.).
 * @return Current device clock in hertz, or a negative error code.
 */
typedef long drv_get_cur_freq_func(const struct dev_info* info,
                                   int clock_index);


/** Get the desired setting for a device clock frequency.
 * @param info Information about device shim characteristics.
 * @param clock_index Index of the clock (0 for the first clock, 1 for the
 *  second, etc.).
 * @return Desired device clock in hertz, or a negative error code.  As
 *  special cases, DRV_DESIRED_FREQ_RAISEV requests the highest possible
 *  speed, raising the core voltage if needed, while DRV_DESIRED_FREQ_MAX
 *  requests the highest possible speed without raising the core voltage
 *  beyond that required by the other shims.
 */
typedef long drv_get_desired_freq_func(const struct dev_info* info,
                                       int clock_index);

//
// Note that the clock frequency code assumes that the two following
// #defined values are larger than the largest possible frequency for any
// shim.
//

/** Request the highest possible speed, raising core voltage if needed. */
#define DRV_DESIRED_FREQ_MAX_RAISEV  LONG_MAX

/** Request the highest possible speed, without modifying core voltage. */
#define DRV_DESIRED_FREQ_MAX         (LONG_MAX - 1)

/** Set a device clock frequency.
 * @param info Information about device shim characteristics.
 * @param clock_index Index of the clock (0 for the first clock, 1 for the
 *  second, etc.).
 * @return Zero if the clock was set successfully, or a negative error code.
 */
typedef int drv_set_freq_func(const struct dev_info* info,
                              int clock_index, long freq);


//
// Note: if you add a routine to the ops vector, make sure you
// also add an appropriate no_xxx or null_xxx routine, and modify
// canonicalize_driver_table() appropriately.
//

/** Driver operations vector. */
struct drv_ops
{
  drv_probe_func* probe;                    ///< probe routine
  drv_init_func* init;                      ///< init routine
  drv_open_func* open;                      ///< open routine
  drv_close_func* close;                    ///< close routine
  drv_close_all_func* close_all;            ///< close_all routine
  drv_pread_func* pread;                    ///< pread routine
  drv_pwrite_func* pwrite;                  ///< pwrite routine
  drv_poll_func* poll;                      ///< poll routine
  drv_poll_cancel_func* poll_cancel;        ///< poll_cancel routine
  drv_preada_func* preada;                  ///< preada routine
  drv_pwritea_func* pwritea;                ///< pwritea routine
  drv_msg_func* msg;                        ///< msg routine
  drv_service_func* service;                ///< service routine
#if MAX_DEVICE_CLOCKS > 0
  drv_get_cur_freq_func* get_cur_freq;      ///< get_cur_freq routine
  drv_get_desired_freq_func*
    get_desired_freq;                       ///< get_desired_freq routine
  drv_set_freq_func* set_freq;              ///< set_freq routine
#endif
};

// Convenience routines used as common entry points.

drv_probe_func null_probe;                  ///< Null probe routine
drv_init_func null_init;                    ///< Null init routine
drv_close_func null_close;                  ///< Null close routine
drv_close_all_func null_close_all;          ///< Null close_all routine

drv_open_func no_open;                      ///< No open routine
drv_close_func no_close;                    ///< No close routine
drv_pread_func no_pread;                    ///< No pread routine
drv_pwrite_func no_pwrite;                  ///< No pwrite routine
drv_poll_func no_poll;                      ///< No poll routine
drv_poll_cancel_func no_poll_cancel;        ///< No poll_cancel routine
drv_preada_func no_preada;                  ///< No preada routine
drv_pwritea_func no_pwritea;                ///< No pwritea routine
drv_msg_func no_msg;                        ///< No msg routine
drv_service_func no_service;                ///< No service routine
drv_get_cur_freq_func no_get_cur_freq;      ///< No get_cur_freq routine
drv_get_desired_freq_func
  no_get_desired_freq;                      ///< No get_desired_freq routine
drv_set_freq_func no_set_freq;              ///< No set_freq routine


/** Device driver descriptor. */
typedef struct
{
  /** Which shim this driver can drive. */
  const uint32_t shim_type;

  /** Driver name (for config file; no embedded whitespace). */
  const char* const name;

  /** Driver description. */
  const char* const desc;

  /** Driver ops vector. */
  struct drv_ops* ops;

  /** Driver shared tile requirements. */
  const int stilereq;

  /** Driver dedicated tile requirements. */
  const int dtilereq;

  /** Driver interrupt channel requirements. */
  const int intchanreq;

  /** Maximum number of delayed interrupts this device could have
   * outstanding to any one tile. */
  const int maxdelint;

  /** Driver flags (DRV_FLG_xxx). */
  const uint32_t flags;

} driver_t;


/** Magic attribute for "driver_t" definitions. */
#define __DRIVER_ATTR __attribute__((used, section(".driver_table")))


/** An instance needs its own tiles. */
#define DRV_FLG_TILES_PER_INSTANCE  0x1

/** This device can be the console. */
#define DRV_FLG_CONSOLE             0x2

/** This is an automatic driver; if we probe a device and the hvconfig
    does not specify a driver, load this one automatically.  If
    multiple drivers have this flag set, then the first one in the
    driver list is used.  Automatic drivers should require a single,
    shared driver tile, which will be located on the default shared
    tile. */
#define DRV_FLG_AUTOMATIC           0x4

//
// Hypervisor services for drivers.
//

/** Allocate driver state.  This requests that memory local to the requesting
 *  tile be provided for use by a driver.  Local memory is limited, and, once
 *  allocated, is permanently dedicated to the requesting driver; there is no
 *  corresponding drv_state_free() routine.  Because of this, drivers are urged
 *  to minimize their demands on this scarce resource.  Most data buffers and
 *  other large requests should be allocated from client memory by supervisor
 *  device drivers and then passed to the hypervisor driver.
 * @param size Number of bytes to allocate.
 * @param align Required alignment of the allocated bytes; if 0, the block
 *        will be aligned to the smallest alignment necessary to make it
 *        suitable for storing any data type.
 * @return Address of the start of the allocated block, or NULL if the
 *         requested amount is unavailable.  Note that the current
 *         implementation panics rather than returning NULL on allocation
 *         failure.
 */
void* drv_state_alloc(int size, int align);

/** Allocate zeroed driver state.  The caveats noted under drv_state_alloc()
 *  apply here as well.
 * @param size Number of bytes to allocate.
 * @param align Required alignment of the allocated bytes; if 0, the block
 *        will be aligned to the smallest alignment necessary to make it
 *        suitable for storing any data type.
 * @return Address of the start of the allocated block, which will be set to
 *         zeroes, or NULL if the requested amount is unavailable.  Note
 *         that the current implementation panics rather than returning
 *         NULL on allocation failure.
 */
void* drv_state_zalloc(int size, int align);

/** Allocate shared driver state.  This requests that memory shared between
 *  all tiles be provided for use by a driver.  Shared memory is limited,
 *  and, once allocated, is permanently dedicated to the requesting driver;
 *  there is no corresponding drv_shared_state_free() routine.  Because of
 *  this, drivers are urged to minimize their demands on this scarce
 *  resource.  Most data buffers and other large requests should be allocated
 *  from client memory by supervisor device drivers and then passed to the
 *  hypervisor driver.  Note that shared memory is likely to have lower
 *  performance than local memory, since it will not necessarily be cached
 *  on the using tile, and because accessing it may incur a TLB miss.
 *
 *  Small amounts of shared memory may also be created by declaring
 *  variables outside functions (or inside functions, if declared static),
 *  with the _SHARED modifier, for instance:
 *
 *  @code
 *  int foo _SHARED = 123;
 *  struct bar* baz _SHARED;
 *  @endcode
 *
 *  These non-dynamic objects are useful for holding things like locks,
 *  or pointers to larger dynamically-allocated pieces of shared memory.
 *
 *  Shared state, either static or dynamic, may not be referenced in
 *  a driver's probe routine.  Drivers are advised to allocate dynamic
 *  shared state in their init routines; while it is legal to do so
 *  elsewhere, in many cases such allocations will fail.  This is because
 *  other driver entry points generally run after the client supervisor
 *  has been started, and in most configurations the supervisor is given
 *  all available free memory when it starts.
 *
 * @param size Number of bytes to allocate.
 * @param align Required alignment of the allocated bytes; if 0, the block
 *        will be aligned to the smallest alignment necessary to make it
 *        suitable for storing any data type.
 * @return Address of the start of the allocated block, or NULL if the
 *         requested amount is unavailable.  Note that the current
 *         implementation panics rather than returning NULL on allocation
 *         failure.
 */
void* drv_shared_state_alloc(int size, int align);

/** Allocate zeroed shared driver state.  The caveats noted under
 *  drv_shared_state_alloc() apply here as well.
 * @param size Number of bytes to allocate.
 * @param align Required alignment of the allocated bytes; if 0, the block
 *        will be aligned to the smallest alignment necessary to make it
 *        suitable for storing any data type.
 * @return Address of the start of the allocated block, which will be set to
 *         zeroes, or NULL if the requested amount is unavailable.  Note
 *         that the current implementation panics rather than returning
 *         NULL on allocation failure.
 */
void* drv_shared_state_zalloc(int size, int align);

/** A spinlock, used with drv_spin_lock(), drv_spin_trylock, and
 *  drv_spin_unlock().  Should be initialized to DRV_SPINLOCK_INIT. */
typedef struct { /** Lock word. */ int lock; } drv_spinlock_t;

/** Value with which to initialize a spinlock. */
#define DRV_SPINLOCK_INIT { 0 }

/** Acquire a spinlock, blocking until it is available.  Spinlocks may
 *  not be acquired recursively.
 * @param mutex Pointer to the spinlock.
 */
void drv_spin_lock(drv_spinlock_t* mutex);

/** Acquire a spinlock if it can be done without blocking.
 * @param mutex Pointer to the spinlock.
 * @return Zero if the lock was successfully acquired, non-zero otherwise.
 */
int drv_spin_trylock(drv_spinlock_t* mutex);

/** Release a spinlock.
 * @param mutex Pointer to the spinlock.
 */
void drv_spin_unlock(drv_spinlock_t* mutex);

/** Initialize a spinlock.
 * @param mutex Pointer to the spinlock.
 */
void drv_spin_lock_init(drv_spinlock_t* mutex);

/** Allocate shared client memory.  This requests that memory local to the
 *  requesting tile be provided for use by a driver, and simultaneously
 *  mapped into the client's virtual address space.  Client-shared memory is
 *  limited, and, once allocated, is permanently dedicated to the requesting
 *  driver; there is no corresponding drv_client_free() routine.  Because of
 *  this, drivers are urged to minimize their demands on this scarce resource.
 *  The client will only be able to access the shared memory when a context
 *  with the HV_CTX_DIRECTIO flag set is installed.
 * @param size Number of bytes to allocate.
 * @param align Required alignment of the allocated bytes; if 0, the block
 *        will be aligned to the smallest alignment necessary to make it
 *        suitable for storing any data type.
 * @param readonly If nonzero, the memory will not be writable by the
 *        client.
 * @param superonly If nonzero, the memory will not be readable or writable
 *        by code running at PL0.
 * @param client_va The client virtual address at which the memory is mapped
 *        will be written to this location.
 * @return Address of the start of the allocated block, or NULL if the
 *         requested amount is unavailable.
 */
void* drv_client_alloc(int size, int align, int readonly, int superonly,
                       VA* client_va);

/** Allocate zeroed shared client memory.  The caveats noted under
 *  drv_client_alloc() apply here as well.
 * @param size Number of bytes to allocate.
 * @param align Required alignment of the allocated bytes; if 0, the block
 *        will be aligned to the smallest alignment necessary to make it
 *        suitable for storing any data type.
 * @param readonly If nonzero, the memory will not be writable by the
 *        client.
 * @param superonly If nonzero, the memory will not be readable or writable
 *        by code running at PL0.
 * @param client_va The client virtual address at which the memory is mapped
 *        will be written to this location.
 * @return Address of the start of the allocated block, or NULL if the
 *         requested amount is unavailable.
 */
void* drv_client_zalloc(int size, int align, int readonly, int superonly,
                        VA* client_va);


/** Mask out drv_map_dtlb_page() flag bits related to caching mode. */
#define DRV_MAP_MODE_RMASK 0x7

/** The caching mode is stored in the low bits. */
#define DRV_MAP_MODE_SHIFT 0

/** Loads and stores access memory directly and never hit in the cache. */
#define DRV_MAP_MODE_UNCACHED HV_PTE_MODE_UNCACHED

/** Loads and stores can hit in local cache, then go to memory. */
#define DRV_MAP_MODE_CACHE_NO_L3 HV_PTE_MODE_CACHE_NO_L3

/** Loads and stores go to a specific tile's L3, then to memory. */
#define DRV_MAP_MODE_CACHE_TILE_L3 HV_PTE_MODE_CACHE_TILE_L3

/** Loads and stores go to an L3 determined by a hash function. */
#define DRV_MAP_MODE_CACHE_HASH_L3 HV_PTE_MODE_CACHE_HASH_L3

/** (old) Flag to create an OLOC'ed mapping with drv_map_dtlb_page(). */
#define DRV_MAP_FLAG_OLOC DRV_MAP_MODE_CACHE_TILE_L3

  
/** Map a page into this tile's VA space.  This will create a pinned
 *  TLB entry for the specified page with the specified mapping
 *  characteristics.  This function may fail if no TLB entries are
 *  available or VA space could not be allocated.  It may only be
 *  called on dedicated tiles.
 * @param page_start Start of mapped page; must be aligned to 'size'.
 * @param size Size of mapping; must be a valid page size.
 * @param flags A combination of DRV_MAP_ flags.
 * @param lotar The home tile if ::DRV_MAP_MODE_CACHE_TILE_L3.
 * @param va Filled with the virtual address at which the memory is mapped.
 * @return Zero if successfully mapped, nonzero on failure.
 */
int drv_map_dtlb_page(PA page_start, int size, uint32_t flags, Lotar lotar,
                      VA* va);

/** Unmap a page from this tile's VA space.  The page specified must be
 *  that which was most recently mapped via drv_map_dtlb_page(), and not
 *  already unmapped; in other words, if you map pages A, B, and C in that
 *  order, they can all be unmapped, but you must first unmap C, then B,
 *  then A.  This function may only be called on dedicated tiles.
 * @param va Virtual address at which the memory is mapped.
 * @param size Size of mapping; must be a valid page size.
 * @param flags Reserved for future use; must be zero.
 * @return Zero if successfully unmapped, nonzero on failure.
 */
int drv_unmap_dtlb_page(VA va, int size, uint32_t flags);

/** Allow client tiles to remotely-home memory on a tile.
 *  Normally clients may only remotely-home memory on tiles which
 *  are part of the client; this allows the use of other tiles,
 *  such as dedicated driver tiles.
 * @param target_tile The tile that now may be OLOC'ed to.
 * @param client_lotar Filled with the Lotar that client can use to
 *        home memory on target_tile.
 * @return Zero on success, nonzero on failure.
 */
int drv_allow_client_pte_lotar(pos_t target_tile, Lotar* client_lotar);

/** Deliver an interrupt message.
 * @param tile Destination tile (can be the current tile).
 * @param intarg The intarg member of the delivered message.
 * @param intdata The intdata member of the delivered message.
 * @return Nonzero if the message could not be delivered, zero otherwise.
 */
int drv_deliver_intr(pos_t tile, uint32_t intarg, uint32_t intdata);

/** Type for driver interrupt functions */
typedef void drv_intr_func(void* intarg, void* msg, int len);

/** Register interest in an interrupt.
 * @param func Function which will be called when the interrupt occurs.
 *
 *        If type is DRV_INTR_DELAYED, the function will be called in a
 *        context suitable for execution of a C function, and will be
 *        passed three arguments: the value of arg specified with the
 *        function was registered, a pointer to the interrupt message
 *        received from the IDN, and the length of that message in bytes.
 *        The function may call driver services, including those which
 *        write to the IDN.
 *
 *        If type is DRV_INTR_INSTANT, the function will be called in a
 *        context suitable for execution of an assembly-language function,
 *        and will be passed two arguments: the value of arg specified
 *        with the function was registered, and the first word of the
 *        interrupt message received from the IDN.  Any remainder of the
 *        message must be retrieved by the function via reads to idn1.
 *        The function may use only registers r0-r3; may not use the stack;
 *        may not call other functions; and may not write to the IDN (or
 *        other networks).  The function may only read or write hypervisor
 *        memory obtained through drv_state_alloc(), or client-shared
 *        memory obtained through drv_client_alloc().  The function must
 *        terminate in one of two ways: either by jumping to the label
 *        "drv_intr_exit", or by verifying that more data is available on
 *        idn1, reading the first word of that next message data into r1,
 *        and jumping to the label "drv_intr_again".
 *
 *        Drivers must configure their shims such that interrupts
 *        intended to be handled by delayed handlers are sent with a
 *        message tag of DRV_INTR_DELAYED_TAG, and those intended to
 *        be handled by instant handlers are sent with a message tag
 *        of DRV_INTR_INSTANT_TAG.
 * @param arg Argument which will be passed to the called interrupt handler.
 * @param type Type of the called interrupt handler (see above).
 * @param chan Interrupt channel number which this handler will handle.
 * @return Zero if the handler was added successfully, nonzero if not; failure
 *        could be due to invalid arguments, or a handler having been
 *        previously installed on the given channel.
 */
int drv_register_intr(drv_intr_func* func, void* arg, int type, int chan);

#define DRV_INTR_DELAYED  0  /**< Delayed interrupt handling */
#define DRV_INTR_INSTANT  1  /**< Instant interrupt handling */

// Tags to use for sending interrupt messages

#define DRV_INTR_INSTANT_TAG 0x79   /**< Tag for instant interrupts */
#define DRV_INTR_DELAYED_TAG 0x7B   /**< Tag for delayed interrupts */

// TODO: these are currently the same as the normal HV values, but they may
// want to be different once we get all of the printf stuff sorted out, on
// the theory that it would help catch improperly addressed messages.
#define DRV_IDN_TAG_0  0x78  /**< Tag used for IDN 0 on dedicated tiles */
#define DRV_IDN_TAG_1  0x79  /**< Tag used for IDN 1 on dedicated tiles */

/** Copy data to a client virtual address, ensuring its validity.
 * @param client_va Address to copy to.
 * @param hv_va Address to copy from.
 * @param len Number of bytes to copy.
 * @param flags Driver flags; if DRV_FLG_HVADDR is set in this word, the
 *        copy will be done, but it's not an error if client_va is a hypervisor
 *        address.
 * @return Zero if the copy was successful, nonzero if not.
 */
int drv_copy_to_client(char* client_va, char* hv_va, int len, uint32_t flags);

/** Copy data from a client virtual address, ensuring its validity.
 * @param hv_va Address to copy to.
 * @param client_va Address to copy from.
 * @param len Number of bytes to copy.
 * @param flags Driver flags; if DRV_FLG_HVADDR is set in this word, the
 *        copy will be done, but it's not an error if client_va is a hypervisor
 *        address.
 * @return Zero if the copy was successful, nonzero if not.
 */
int drv_copy_from_client(char* hv_va, char* client_va, int len, uint32_t flags);

/** Convert a tile coordinate into a tile index, suitable for indexing an
 *  array of per-tile state.  The mapping is intentionally undocumented and
 *  may change, and the resulting index is not guaranteed to be completely
 *  compact.  The returned index will be less than DRV_MAX_TILE_INDEX.
 * @param tile Tile coordinate.
 * @return Tile index.
 */
uint32_t drv_tile2index(pos_t tile);

/** Ceiling for tile2index values */
#define DRV_MAX_TILE_INDEX (1 << (HV_XBITS + HV_YBITS))

/** Convert a tile index generated by drv_tile2index into a tile coordinate.
 * @param idx Tile index.
 * @return Tile coordinate.
 */
pos_t drv_index2tile(uint32_t idx);

/** Translate a client physical address to a real physical address.
 *  This function only works once the client has been launched.  In
 *  particular, it should not be called during a driver's init method.   
 * @param client_pa Client physical address.
 * @param len Number of bytes to validate.
 * @param real_pa Pointer to returned real physical address.
 * @return Zero if all bytes in [client_pa, client_pa + len) are
 *          valid, nonzero otherwise.
 */
uint32_t drv_cpa2pa(CPA client_pa, CPA len, PA* real_pa);

/** Translate a LOTAR value in the client's virtual geometry to the real
 *  value.
 * @param client_lotar LOTAR value in the client's geometry.
 * @param real_lotar Pointer to returned real LOTAR value.
 * @return Nonzero if the client has specified an illegal value, 0 otherwise.
 */
int drv_c2r_lotar(Lotar client_lotar, Lotar* real_lotar);

/** Translate a LOTAR value in the client's virtual geometry to the
 *  real value and allow the LOTAR to include 'extra' tiles as
 *  requested by drivers.
 * @param client_lotar LOTAR value in the client's geometry.
 * @param real_lotar Pointer to returned real LOTAR value.
 * @return Nonzero if the client has specified an illegal value, 0 otherwise.
 */
int drv_c2r_pte_lotar(Lotar client_lotar, Lotar* real_lotar);

/** Wait until a value in memory changes.  While waiting, process incoming
 *  messages whose priorities are higher than our current priority.
 * @param valptr Pointer to target value.
 * @param valsize Size in bytes of value (1, 2, or 4).
 * @param curval Value which *valptr currently has; when *valptr is different
 *        from this value, the function will return.
 */
void drv_nap_until_change(void* valptr, int valsize, uint32_t curval);

/** Allow processing of hypervisor message interrupts, particularly those
 *  generated by calls to drv_xxx_remote() routines targeting this tile.
 */
void drv_yield(void);

//
// Remote driver services.  These routines are used to convey a driver
// service request to a device's shared or dedicated tile.
//

/** Remotely open a hypervisor device.
 */
int drv_open_remote(int devhdl, const char* suffix, uint32_t flags,
                    pos_t rem_tile);

/** Remotely close a hypervisor device.
 */
int drv_close_remote(int devhdl, pos_t rem_tile);

/** Remotely read data from a hypervisor device synchronously.
 */
int drv_pread_remote(int devhdl, uint32_t flags, char* va,
                     uint32_t len, uint64_t offset, pos_t rem_tile);

/** Remotely write data to a hypervisor device synchronously.
 */
int drv_pwrite_remote(int devhdl, uint32_t flags, char* va,
                      uint32_t len, uint64_t offset, pos_t rem_tile);

/** Remotely request an interrupt when a device condition is satisfied.
 */
int drv_poll_remote(int devhdl, uint32_t events, uint32_t intarg,
                    pos_t rem_tile);

/** Remotely cancel a request for an interrupt when a device event occurs.
 */
int drv_poll_cancel_remote(int devhdl, pos_t rem_tile);

/** Remotely read data from a hypervisor device asynchronously.
 */
int drv_preada_remote(int devhdl, uint32_t flags, uint32_t sgl_len,
                      HV_SGL sgl[sgl_len], uint64_t offset,
                      uint32_t intarg, pos_t remote);

/** Remotely write data to a hypervisor device asynchronously.
 */
int drv_pwritea_remote(int devhdl, uint32_t flags, uint32_t sgl_len,
                       HV_SGL sgl[sgl_len], uint64_t offset,
                       uint32_t intarg, pos_t rem_tile);

/** Send a driver-to-driver message.
 * @param devhdl Device handle of the target device.
 * @param msg The message to be sent; must be word-aligned.
 * @param msglen Number of bytes in the message; must not exceed
 *        DRV_MAX_MSG_LEN.
 * @param reply Buffer into which the reply will be written.  May be NULL if
 *        the reply is unwanted.
 * @param replybuflen Length of reply.
 * @param replylen The actual length of the reply will be placed in the
 *        location this points to.  This may be longer than replybuflen,
 *        although only the first replybuflen bytes will be present in reply.
 *        May be NULL if the length of the reply is unwanted.
 * @param rem_tile Tile to send the message to.
 * @return HV_EINVAL if msglen is negative or too large; otherwise, the value
 *         passed as the retval parameter to drv_msg_reply().
 */
int drv_send_msg(int devhdl, void* msg, int msglen, void* reply,
                 int replybuflen, int* replylen, pos_t rem_tile);


/** Reply to a driver-to-driver message.  This routine must be called exactly
 *  once for every call to a driver's drv_msg() entry point; however, it
 *  could be called after that routine returns.
 * @param token Token which was passed to the drv_msg() entry point when the
 *        message being replied to was received.
 * @param retval Value which will be returned from drv_send_msg() on the
 *        sending tile.
 * @param replybuf Buffer containing the reply.
 * @param replylen Length of the reply; must be no larger than DRV_MAX_MSG_LEN.
 * @param tile Tile to send the reply to; this must be the tile from which
 *        the message being replied to was received from.
 */
void drv_reply_msg(drv_reply_msg_token_t token, int retval, void* replybuf,
                   int replylen, pos_t tile);


/** Get a MAC address.
 * @param is_xaui Nonzero if this is a XAUI interface.
 * @param instance Instance number of the device.
 * @param mac Buffer in which to return the 6-byte MAC address.
 * @return Nonzero if a MAC address was returned, zero otherwise.
 */
int drv_get_mac(int is_xaui, int instance, uint8_t mac[]);

/** Get maximum interface speed.
 * @param is_xaui Nonzero if this is a XAUI interface.
 * @param instance Instance number of the device.
 * @return Maximum interface speed in megabits per second, or -1 if no maximum
 *        speed is defined.
 */
int drv_get_intf_max_speed(int is_xaui, int instance);

/** Define the type of a fastio handler.  Note that since the actual
 *  implementation of a handler must be in assembly, the argument
 *  declaration isn't all that relevant.
 */
typedef int (drv_fastio_func)(void*, ...);

/** Allocate one or more fast I/O indices.
 * @param nentries Number of indices desired.
 * @param superonly If nonzero, the indices will come from the set which are
 *        only callable by the client supervisor; otherwise, they will come
 *        from the set which are callable by both supervisor and user processes.
 * @return ~0 if the requested indices are not available; otherwise, the
 *        first index in the consecutive sequence of indices is returned.
 */
uint32_t drv_alloc_fastio(int nentries, int superonly);


/** Associate a function and argument with a fast I/O index.
 * @param func Function to be called when the client executes the fast
 *        I/O request.  This function will be called in a context suitable
 *        for execution of an assembly-language function.  Its first
 *        argument will be the value of arg specified with the function
 *        was registered; the remaining argument registers will hold
 *        values set by the requesting user (or supervisor) process.
 *        The function may use only registers r0-r29, and may not call
 *        other functions; however, it may write to the stack, and may
 *        write messages to the IDN.  The function may only read or
 *        write hypervisor memory obtained through drv_state_alloc(),
 *        or client-shared memory obtained through drv_client_alloc().
 *        The function must terminate via "jrp lr".  The value in r0
 *        when the function returns will be made available to the requesting
 *        process.
 * @param arg First argument to be passed to the function when it's called.
 * @param index Fast I/O index as received from drv_alloc_fastio().
 */
void drv_register_fastio(drv_fastio_func* func, void* arg, int index);


/** Unregister a fast I/O index.  After this function completes, calls to
 *  the unregistered function will perform no action.
 * @param index Fast I/O index as received from drv_alloc_fastio().
 */
void drv_unregister_fastio(int index);


/** Start a driver timer.
 * @param usec Microseconds until the timer will complete.
 * @return Value which can be passed to drv_timer_done() to test for
 *         timer completion.
 */
uint64_t drv_timer_start(uint32_t usec);


/** Test a driver timer for completion.
 * @param timer Timer value returned from drv_timer_start().
 * @return Nonzero if the time specified when the timer was started has
 *         passed; zero otherwise.
 */
int drv_timer_done(uint64_t timer);


/** Delay for a short period of time.
 * @param usec Microseconds of delay requested.  The actual delay will be
 *        no less than this value.
 */
void drv_udelay(uint32_t usec);



/** Allocate an interrupt channel number.
 * @return The interrupt channel number allocated, or a negative value if
 *         none are available.
 */
int drv_alloc_intchan(void);

/** Free an interrupt channel number.
 * @return The interrupt channel number to free.
 */
void drv_free_intchan(int chan);

/** The interrupt binding registers use an 8-bit "tile ID" to denote the
 *  target tile.
 * @param x X tile coordinate.
 * @param y Y tile coordinate.
 * @return Tile ID.
 */
#define DRV_COORDS_TO_TILE_ID(x, y) (((x) << 4) | (y))



/** Retrieve the next option (of the form "option", or "option=value") from an
 *  argument string.  Options are separated by whitespace.
 * @param argptr Pointer to a pointer to the string to be parsed; updated
 *        on return to point after the last-parsed option.  Note that the
 *        argument string itself is modified, to null-terminate the option
 *        and value found.
 * @param opt Pointer to a pointer which is set to point to the option found.
 * @param val Pointer to a pointer which is set to point to the value found,
 *            or to NULL if there is no value.
 * @return Nonzero if an option was found, otherwise 0.
 */
int drv_next_opt(char** argptr, char** opt, char** val);


#endif /* __ASSEMBLER__ */

/** Log2 of the size of a memory arena. */
#define DRV_IOMEM_ARENA_SHIFT      24      /* 16 MB */
/** Size of a memory arena. */
#define DRV_IOMEM_ARENA_SIZE       (1 << DRV_IOMEM_ARENA_SHIFT)

/** Log2 of the number of memory arenas supported. (256 arenas)*/
#define DRV_IOMEM_NUM_ARENAS_SHIFT (32 - DRV_IOMEM_ARENA_SHIFT)
/** Maximum number of memory arenas supported. */
#define DRV_IOMEM_NUM_ARENAS       (1 << DRV_IOMEM_NUM_ARENAS_SHIFT)

#ifndef __ASSEMBLER__

/** VA to PA table for registered iomem.  iomem allows drivers to
 *  register particular user VA ranges that map directly to PA space.
 *  Once registered, the PA-to-VA translation can be performed quickly
 *  via a table lookup.  This mechanism is often used for mapping
 *  packet data.

 *  The table itself is indexed by the low DRV_IOMEM_NUM_ARENAS_SHIFT
 *  bits of the top 32-DRV_IOMEM_ARENA_SHIFT bits of the VA.  Each
 *  entry contains the top 15 bits of the corresponding PA in its top
 *  15 bits.  The low bit is 1 iff packets should not be allowed to
 *  run off the end of this large page (i.e., if the next virtually
 *  contiguous page is not physically contiguous), and the high bit is
 *  1 iff the entry is invalid.  Note that this requires
 *  DRV_IOMEM_ARENA_SHIFT to be at least 22; if it were smaller we'd
 *  need more than the 14 bits we have to create a full 36-bit PA.
 */
extern uint16_t drv_iomem_va2pa[DRV_IOMEM_NUM_ARENAS];

/** Register a chunk of memory for use as fast "iomem".  The input VA
 *  and PA must be aligned to a ::DRV_IOMEM_ARENA_SIZE boundary and be
 *  composed of ::DRV_IOMEM_ARENA_SIZE bytes of contiguous memory.
 *
 *  The iomem mechanism allows drivers to quickly translate registered
 *  VAs to PAs via a simple table lookup.  The ::drv_iomem_va2pa table
 *  stores the high translation bits for each arena, along with a bit
 *  indicating whether the next page is contiguous.
 *
 *  This method will succeed if the parameters are valid and either 1)
 *  no iomem has been registered at the specified VA or 2) the same cpa
 *  has already been registered at that VA.  This method will increment
 *  a reference count so that the drv_unregister_iomem() method only
 *  removes the mapping when there has been an unregistration for every
 *  previous registration.
 *
 * @param va Virtual address to be mapped.
 * @param cpa Client physical address of the backing memory.
 * @return HV_EFAULT if not aligned or bad cpa, HV_EBUSY if already
 * mapped with a different backing cpa, otherwise 0.
 */
int drv_register_iomem(VA va, CPA cpa);

/** Unregister a chunk of "iomem".  This method will decrement the
 *  reference counts for all arenas in the specified VA range, and
 *  remove the mappings of any arenas whose reference counts reach
 *  zero.
 *
 * @param va Starting virtual address.
 * @param size How many bytes to unregister.
 * @return HV_EFAULT if va or size were not ::DRV_IOMEM_ARENA_SIZE
 * aligned, HV_EINVAL if any reference counts were already 0,
 * otherwise 0.
 */
int drv_unregister_iomem(VA va, int size);

/** Convert a register iomem VA to a PA.
 *
 * @param va The virtual address.
 * @param size of buffer, must be greater than 0 and smaller than
 * DRV_IOMEM_ARENA_SIZE.
 * @param pa Filled with the result pa.
 * @return HV_EFAULT if unregistered or noncontiguous, else 0.
 */
int drv_convert_iomem_va2pa(VA va, int size, PA *pa);

/** Enable interrupts on IDN1.  This routine may only be called on a
 *  dedicated tile.  When interrupts are enabled, they are handled by the
 *  standard instant and delayed interrupt handlers installed via
 *  drv_register_intr().
 */
void drv_enable_idn1_intr(void);

/** Disable interrupts on IDN1.  This routine may only be called on a
 *  dedicated tile.  Note that when a dedicated tile's service routine is
 *  called, IDN1 interrupts are already disabled, so this is only necessary
 *  in cases where drv_enable_idn1_intr() has been previously used.
 */
void drv_disable_idn1_intr(void);

/** Set a signal.  Signals are defined in a board's information block, and
 *  define connections to miscellaneous low-speed I/O devices (LEDs, reset
 *  signals for chips, etc.)  This routine allows a driver to manipulate
 *  such signals without having to worry about exactly how they're
 *  connected to the chip.
 *
 * @param sig_desc Signal descriptor.
 * @param action DRV_SIGNAL_xxx flags, ORed together; exactly one of
 *        DRV_SIGNAL_ASSERT and DRV_SIGNAL_DEASSERT must be specified.
 * @return Zero upon success, nonzero on failure.
 */
int drv_set_signal(sigdesc_t sig_desc, int action);

/** Initialize anything needed to drive the signal.  This flag must be
 *  specified on the first call to drv_set_signal() with any particular
 *  descriptor; it's best not to specify it on later calls, although
 *  that should not cause incorrect behavior, it just wastes time. */
#define DRV_SIGNAL_INIT       SIGNAL_INIT

/** Assert the specified signal. */
#define DRV_SIGNAL_ASSERT     SIGNAL_ASSERT

/** Deassert the specified signal. */
#define DRV_SIGNAL_DEASSERT   SIGNAL_DEASSERT

/** Make a range of memory-mapped I/O addresses available to a client.
 *  The region must not currently be partially or wholly available to that
 *  client.  Note that this routine may need to communicate with other
 *  tiles in the client to perform its function; thus, holding a spinlock
 *  during a call to this routine may lead to a deadlock.
 * @param shimaddr Coordinates of the I/O shim to which accesses will be made.
 * @param start First valid byte of the MMIO range; must be aligned to a 4K
 *   boundary.
 * @param len Number of bytes in the MMIO range; this value must be
 *   aligned to a 4K boundary.
 * @param clientno Client number to which the region will be made
 *   available.
 * @return Zero if the region is successfully made available; HV_EINVAL if
 *   the region, shim, or client is incorrectly specified (e.g., start/len
 *   are unaligned); HV_EBUSY if the region is already partially or wholly 
 *   available to the specified client; HV_ENOMEM if memory could not
 *   be allocated or reallocated for the MMIO permissions table.
 */
int drv_permit_mmio_access(pos_t shimaddr, PA start, PA len, int clientno);


/** Remove a range of memory-mapped I/O addresses from the set of addresses
 *  available to a client.  The region must currently be wholly available
 *  to that client.  Note that this routine may need to communicate with
 *  other tiles in the client to perform its function; thus, holding a
 *  spinlock during a call to this routine may lead to a deadlock.
 * @param shimaddr Coordinates of the I/O shim to which accesses will be made.
 * @param start First valid byte of the MMIO range; must be aligned to a 4K
 *   boundary.
 * @param len Number of bytes in the MMIO range; this value must be
 *   aligned to a 4K boundary.
 * @param clientno Client number to which the region will be made
 *   unavailable.
 * @return Zero if successfully mapped; HV_EINVAL if the region, shim, or
 *   client is incorrectly specified (e.g., start/len are unaligned);
 *   HV_ENOTREADY if the region was not wholly available to the specified
 *   client.
 */
int drv_deny_mmio_access(pos_t shimaddr, PA start, PA len, int clientno);

/** Validate a iorpc RPC buffer.  This routine validates RPC
 *  parameters that require extra checking.  For example, it can verify
 *  that a memory buffer's CPA and homing are valid and that the
 *  address is properly aligned.  When appropriate, it will also
 *  translate from client interface structures like HV_PTEs to more
 *  convenient formats like raw lotar bits.
 *
 * @param offset The offset passed to dev_pread or dev_pwrite.
 * @param buffer The RPC data (already copied into HV memory).
 * @param size Number of bytes in the buffer.
 * @param flags Flags indicating extra validation requirements.
 * @return Zero upon success, IORPC error code on failure.
 */
int drv_translate_iorpc(uint64_t offset, void* buffer, uint32_t size,
                        unsigned long flags);

/** Require that a buffer have a least 4kB alignment. */
#define DRV_IORPC_FLAG_ALIGN_4KB 0x1

/** Require that a buffer have a least 64kB alignment. */
#define DRV_IORPC_FLAG_ALIGN_64KB 0x2

/** Require that a buffer be self-size aligned. */
#define DRV_IORPC_FLAG_ALIGN_SELF_SIZE 0x4


/** Configure a set of IOTLB entries which map all of a client's CPA space.
 *
 * @param shim_pos I/O shim which holds the IOTLB.
 * @param asid ASID to use (index into the IOTLB).
 * @param pte Page table entry describing how the space should be mapped.
 * @param tlb_entry_offset Address offset into the system MMIO space for an
 *        mPIPE/TRIO/USB service domain or MiCA context to the first word of
 *        the TLB table for that service domain or context.
 * @param flags Flags with mapping attributes (IORPC_MEM_BUFFER_FLAG_xxx).
 * @return 0 on success, or a GXIO error code to be given to the client.
 */
int drv_map_cpa_space_to_iotlb(pos_t shim_pos, unsigned int asid,
                               HV_PTE pte, PA tlb_entry_offset,
                               unsigned int flags);

#endif /* __ASSEMBLER__ */

#endif /* _SYS_HV_DRVINTF_H */
