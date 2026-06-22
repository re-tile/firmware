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
 * Device handling routines.
 */

#ifndef _SYS_HV_DEVICES_H
#define _SYS_HV_DEVICES_H

#include <arch/chip.h>

#include <hv/hypervisor.h>

#include "config.h"
#include "device_table.h"
#include "drvintf.h"
#include "param.h"
#include "types.h"


/** Maximum number of bytes for driver argument string. */
#define DEV_MAX_ARGLEN    1024


/** The address of the first device table entry. */
extern device_t devices[];

/** The address after the last device table entry. */
extern device_t devices_end[];

/** The address of the first "driver_t". */
extern driver_t driver_table_start[];

/** The address after the last "driver_t". */
extern driver_t driver_table_end[];


/** Data on the probed memory shims, extracted so that it can be used by
 *  non-driver code.  The set of valid mshims is not guaranteed to
 *  be contiguous; code iterating over all mshims should examine all
 *  MAX_MSHIM entries in these tables, and ignore entries for which
 *  mshims[entry] is NULL.
 */
extern const struct dev_info* mshims[];
/** Bytes of memory present on each memory shim */
extern PA mshim_sizes[];
/** Address of first byte of memory present on each memory shim */
extern PA mshim_bases[];
/** Speed of each memory shim (bytes per second) */
extern uint64_t mshim_speeds[];
/** Physical controller number (MSH_PORT) of each memory shim */
extern uint8_t mshim_controller[];
/** Physical address to use when doing a latency ping to each memory shim */
extern PA mshim_ping[];

/** DIMM info on each memory shim */
extern uint8_t mshim_dimm_info[MAX_MSHIMS][HV_MSH_MAX_DIMMS];

/** IPI shim coordinates. */
extern pos_t ipi_pos[];

/** Coordinates of the IPI shim closest to this tile. */
extern pos_t my_ipi_pos;

/** Data on the probed miscellaneous I/O shims. */
extern const struct dev_info* rshims[];

/** Data on the probed GPIO shims. */
extern const struct dev_info* gpio_shims[];

/** Data on the SROM shim. */
extern const struct dev_info* srom_info;

/** Data on the I2C master shim. */
extern const struct dev_info* i2cm_info[MAX_I2CMS];

/** Device table index in the device handle.  These share the handle with the
 *  device-specific bits, which are defined in drvintf.h. */
#define DEV_IDX_SHIFT 24                               ///< Dev index shift
#define DEV_IDX_WIDTH 7                                ///< Dev index width
#define DEV_IDX_RMASK RMASK(DEV_IDX_WIDTH)             ///< Dev index RJ mask
#define DEV_IDX_MASK  (DEV_IDX_RMASK << DEV_IDX_SHIFT) ///< Dev index Mask

/** Convert a device handle to a device table index. */
#define HDL2IDX(devhdl) (((devhdl) >> DEV_IDX_SHIFT) & DEV_IDX_RMASK)

/** Convert the index and state bits back to a device handle. */
#define MK_HDL(idx, bits) ((idx << DEV_IDX_SHIFT) | \
                          ((bits & DRV_BITS_RMASK) << DRV_BITS_SHIFT))

/** Probe the IPI controllers, and set my_ipi_pos. */
void probe_ipic(void);

/** Probe all devices on the chip.
 * @param shim_mask Mask of potentially valid shims.
 * @param rshim Position of the rshim.
 */
void probe_devices(unsigned long shim_mask, pos_t rshim);

#if 2 * ((1 << (HV_XBITS)) + (1 << (HV_YBITS))) > CHIP_WORD_SIZE() 
#error Too many tiles on chip perimeter, shim_mask needs to be larger
#endif

/** Probe a shim and if it's there, create its info struct and call the device
 *  probe routine.
 * @param test_shim Position of shim to probe.  Note that this is an off-grid
 *  coordinate (i.e., 1,0 for the shim to the north of the ULHC tile).
 * @param chan Channel number to probe on shim.
 */
void probe_shim(pos_t test_shim, unsigned long chan);

void init_device_memory_regs(void);
void init_drivers(void);
void call_driver_service(void);
void start_dedicated_tiles(void);

/** Open a hypervisor device.
 * @param name Name of the device.
 * @param flags Flags.
 * @return A positive integer device handle, or a negative error code.
 */
int syscall_dev_open(const char* name, uint32_t flags);

/** Close a hypervisor device.
 * @param devhdl Device handle of the device to be closed.
 * @return Zero if the close is successful, otherwise, a negative error code.
 */
int syscall_dev_close(int devhdl);

/** Read data from a hypervisor device synchronously.
 * @param devhdl Device handle of the device to be read from.
 * @param flags Flags.
 * @param va Virtual address of the target data buffer.
 * @param len Number of bytes to be transferred.
 * @param offset Driver-dependent offset.
 * @return A non-negative value if the read was at least partially successful;
 *         otherwise, a negative error code.
 */
int syscall_dev_pread(int devhdl, uint32_t flags, char* va, uint32_t len,
                      uint64_t offset);

/** Write data to a hypervisor device synchronously.
 * @param devhdl Device handle of the device to be written to.
 * @param flags Flags.
 * @param va Virtual address of the source data buffer.
 * @param len Number of bytes to be transferred.
 * @param offset Driver-dependent offset.
 * @return A non-negative value if the write was at least partially successful;
 *         otherwise, a negative error code.
 */
int syscall_dev_pwrite(int devhdl, uint32_t flags, char* va, uint32_t len,
                       uint64_t offset);

/** Request an interrupt when a device condition is satisfied.
 * @param devhdl Device handle of the device to be polled.
 * @param flags Flags denoting the events which will cause the interrupt to
 *        be delivered.
 * @param intarg First parameter which will be delivered to the device
 *        interrupt vector.
 * @return Zero if the interrupt was successfully scheduled; otherwise, a
 *         negative error code.
 */
int syscall_dev_poll(int devhdl, uint32_t flags, uint32_t intarg);

/** Cancel a request for an interrupt when a device event occurs.
 * @param devhdl Device handle of the device on which to cancel polling.
 * @return Zero if the poll was successfully canceled; otherwise, a negative
 *         error code.
 */
int syscall_dev_poll_cancel(int devhdl);

/** Read data from a hypervisor device asynchronously.
 * @param devhdl Device handle of the device to be read from.
 * @param flags Flags.
 * @param sgl_len Number of elements in the scatter-gather list.
 * @param sgl Scatter-gather list describing the memory to which data will be
 *        written.
 * @param offset Driver-dependent offset.
 * @param intarg First parameter which will be delivered to the device
 *        interrupt vector.
 * @return Zero if the read was successfully scheduled; otherwise, a negative
 *         error code.
 */
int syscall_dev_preada(int devhdl, uint32_t flags, uint32_t sgl_len,
                       HV_SGL sgl[sgl_len], uint64_t offset, uint32_t intarg);

/** Write data to a hypervisor device asynchronously.
 * @param devhdl Device handle of the device to be read from.
 * @param flags Flags.
 * @param sgl_len Number of elements in the scatter-gather list.
 * @param sgl Scatter-gather list describing the memory from which data will be
 *        read.
 * @param offset Driver-dependent offset.
 * @param intarg First parameter which will be delivered to the device
 *        interrupt vector.
 * @return Zero if the write was successfully scheduled; otherwise, a negative
 *         error code.
 */
int syscall_dev_pwritea(int devhdl, uint32_t flags, uint32_t sgl_len,
                        HV_SGL sgl[sgl_len], uint64_t offset, uint32_t intarg);

/** Close all of the opened device instances used by the supervisor.
 *  @return Zero if successful; otherwise, a negative error code.
 */
int syscall_close_all_devices(void);

#endif /* _SYS_HV_DEVICES_H */
