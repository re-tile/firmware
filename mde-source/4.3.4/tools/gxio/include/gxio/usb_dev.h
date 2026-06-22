/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors.
 *   The software is licensed under the Tilera MDE License.
 *
 *   However, Licensee may elect to use this file under the terms of the
 *   GNU Lesser General Public License version 2.1 as published by the
 *   Free Software Foundation and appearing in the file src/COPYING.LIB
 *   in the MDE distribution.  Please review the following information to
 *   ensure the GNU Lesser General Public License version 2.1 requirements
 *   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 */
#ifndef _GXIO_USB_DEV_H_
#define _GXIO_USB_DEV_H_

#include <gxio/common.h>

#include <arch/usb_device.h>

#include <hv/drv_usb_dev_intf.h>
#include <hv/iorpc.h>

/**
 * @file
 *
 * An API for manipulating the USB device shim.
 */

/**
 * @addtogroup gxio_usb_dev
 * @{
 *
 * The USB shim allows access to the processor's Universal Serial Bus
 * connections.
 *
 * In order to use the USB device shim, it must be enabled in the
 * hypervisor configuration file; the default .hvc will do this (and
 * disable the port 0 USB host shim) if --hvd USB_DEV is added to
 * tile-monitor's command line arguments.
 *
 * If the system is configured to enable the USB device shim in boot/debug
 * mode (which enables the use of tile-monitor and tile-btk over USB), then
 * that functionality will persist until gxio_usb_dev_init() is called.  At
 * that point, the application has control of the shim, and boot/debug
 * operations will no longer work.  In some cases, the boot/debug feature
 * will be reenabled once gxio_usb_dev_destroy() is called, but this is not
 * a supported feature of the hardware and should thus not be relied upon.
 */

/** A context object used to manage USB hardware resources.  This object is
 *  opaque; its contents are not to be directly used by applications. */
typedef struct {
#ifndef __DOXYGEN__
  /** File descriptor for calling up to the hypervisor. */
  int fd;

  /** The VA at which our MMIO registers are mapped. */
  char* mmio_base;

  /** Offsets to the start of each endpoint's receive FIFO (first
   *  HV_USB_DEV_NUM_EP entries) and transmit FIFO (last HV_USB_DEV_NUM_EP
   *  entries). */
  uint16_t fifo_offset[2 * HV_USB_DEV_NUM_EP];
#endif
} gxio_usb_dev_context_t;


/** Initialize a USB device context.
 *
 *  A properly initialized context must be obtained before any of the other
 *  gxio_usb_dev routines may be used.
 *
 *  Note: once this call returns, the application must finish initializing
 *  the physical endpoint registers (via gxio_usb_dev_set_udc_ep()) within
 *  6 milliseconds; otherwise, the system may hang.
 *
 * @param context Pointer to a gxio_usb_dev_context_t, which will be
 *  initialized by this routine, if it succeeds.
 * @param usb_index Index of the USB shim to use.
 * @return Zero if the context was successfully initialized, else a
 *  GXIO_ERR_xxx error code.
 */
extern int
gxio_usb_dev_init(gxio_usb_dev_context_t* context, int usb_index);

/** Destroy a USB device context.
 *
 *  Once destroyed, a context may not be used with any gxio_usb_dev routines
 *  other than gxio_usb_dev_init().  After this routine returns, no further
 *  interrupts requested on this context will be delivered.
 *
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @return Zero if the context was successfully destroyed, else a
 *  GXIO_ERR_xxx error code.
 */
extern int
gxio_usb_dev_destroy(gxio_usb_dev_context_t* context);

/** Read data from an endpoint's receive FIFO.  Note that the hardware does
 *  not permit partial reads; when notified that an X-byte packet is
 *  available, you must retrieve all X bytes with one call to this routine.
 *  Also note that it is legal to call this routine with a len of zero;
 *  this is necessary in order to remove a zero-length packet from the
 *  FIFO.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Logical endpoint number.
 * @param dest Pointer to a memory buffer which will receive the read data.
 * @param len Number of bytes of data to read.
 */
extern void
gxio_usb_dev_read_rx_fifo(gxio_usb_dev_context_t* context, int ep, void* dest,
                          int len);

/** Write data to an endpoint's transmit FIFO.  Note that the hardware does
 *  not permit partial writes; to send an X-byte packet, you must write all
 *  of its data with one call to this routine.  Also note that it is legal
 *  to send a null-packet with this routine, by specifying a len of 0
 *  bytes.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Logical endpoint number.
 * @param src Pointer to a memory buffer which will supply the write data.
 * @param len Number of bytes of data to write.
 */
extern void
gxio_usb_dev_write_tx_fifo(gxio_usb_dev_context_t* context, int ep, void* src,
                           int len);

#ifndef __DOXYGEN__
extern void __gxio_usb_dev_adjust_fifo_ptrs(gxio_usb_dev_context_t* context,
                                            int ep, int is_tx, int delta);
#endif // __DOXYGEN__

//
// These functions account for the fact that we only mmap the MAC
// registers, which start at offset 0x10000 from the start of the shim.
// Subtracting that offset here means that the per-register accesors
// below can just use the #defines from the architecture header files
// directly.  Since those values are constants, and this stuff is all
// inlined, it's all constant-folded by the compiler.
//

#ifndef __DOXYGEN__
static __USUALLY_INLINE uint32_t
__gxio_usb_dev_r32(gxio_usb_dev_context_t* context, int reg)
{
  return __gxio_mmio_read32(context->mmio_base + reg - 0x10000);
}


static __USUALLY_INLINE void
__gxio_usb_dev_w32(gxio_usb_dev_context_t* context, int reg, uint32_t val)
{
  __gxio_mmio_write32(context->mmio_base + reg - 0x10000, val);
}
#endif // __DOXYGEN__


/** Get the value of an endpoint's input control register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Logical endpoint number.
 * @return Register value.
 */
static __USUALLY_INLINE unsigned int
gxio_usb_dev_get_in_ctrl(gxio_usb_dev_context_t* context, int ep)
{
  const int ep_delta =
    USB_DEVICE_EP1_IN_CTRL_UDC_REG - USB_DEVICE_EP0_IN_CTRL_UDC_REG;

  return __gxio_usb_dev_r32(context, ep * ep_delta +
                            USB_DEVICE_EP0_IN_CTRL_UDC_REG);
}

/** Set the value of an endpoint's input control register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Logical endpoint number.
 * @param val New register value.
 */
static __USUALLY_INLINE void
gxio_usb_dev_set_in_ctrl(gxio_usb_dev_context_t* context, int ep,
                         unsigned int val)
{
  const int ep_delta =
    USB_DEVICE_EP1_IN_CTRL_UDC_REG - USB_DEVICE_EP0_IN_CTRL_UDC_REG;

  __gxio_usb_dev_w32(context, ep * ep_delta +
                     USB_DEVICE_EP0_IN_CTRL_UDC_REG, val);
}


/** Get the value of an endpoint's input status register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Logical endpoint number.
 * @return Register value.
 */
static __USUALLY_INLINE unsigned int
gxio_usb_dev_get_in_sts(gxio_usb_dev_context_t* context, int ep)
{
  const int ep_delta =
    USB_DEVICE_EP1_IN_STS_UDC_REG - USB_DEVICE_EP0_IN_STS_UDC_REG;

  return __gxio_usb_dev_r32(context, ep * ep_delta +
                            USB_DEVICE_EP0_IN_STS_UDC_REG);
}

/** Set the value of an endpoint's input status register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Logical endpoint number.
 * @param val New register value.
 */
static __USUALLY_INLINE void
gxio_usb_dev_set_in_sts(gxio_usb_dev_context_t* context, int ep,
                        unsigned int val)
{
  const int ep_delta =
    USB_DEVICE_EP1_IN_STS_UDC_REG - USB_DEVICE_EP0_IN_STS_UDC_REG;

  __gxio_usb_dev_w32(context, ep * ep_delta +
                     USB_DEVICE_EP0_IN_STS_UDC_REG, val);
}


/** Get the value of an endpoint's input buffer size register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Logical endpoint number.
 * @return Register value.
 */
static __USUALLY_INLINE unsigned int
gxio_usb_dev_get_in_bufsize(gxio_usb_dev_context_t* context, int ep)
{
  const int ep_delta =
    USB_DEVICE_EP1_IN_BUFSIZE_UDC_REG - USB_DEVICE_EP0_IN_BUFSIZE_UDC_REG;

  return __gxio_usb_dev_r32(context, ep * ep_delta +
                            USB_DEVICE_EP0_IN_BUFSIZE_UDC_REG);
}

/** Set the value of an endpoint's input buffer size register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Logical endpoint number.
 * @param val New register value.
 */
static __USUALLY_INLINE void
gxio_usb_dev_set_in_bufsize(gxio_usb_dev_context_t* context, int ep,
                         unsigned int val)
{
  const int ep_delta =
    USB_DEVICE_EP1_IN_BUFSIZE_UDC_REG - USB_DEVICE_EP0_IN_BUFSIZE_UDC_REG;

  uint32_t old_reg = __gxio_usb_dev_r32(context, ep * ep_delta +
                                        USB_DEVICE_EP0_IN_BUFSIZE_UDC_REG);


  __gxio_usb_dev_adjust_fifo_ptrs(context, ep, 1, (int) (val & 0xFFFF) -
                                              (int) (old_reg & 0xFFFF));

  __gxio_usb_dev_w32(context, ep * ep_delta +
                     USB_DEVICE_EP0_IN_BUFSIZE_UDC_REG, val);
}


/** Get the value of an endpoint's input maximum packet size register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Logical endpoint number.
 * @return Register value.
 */
static __USUALLY_INLINE unsigned int
gxio_usb_dev_get_in_mpkt_sz(gxio_usb_dev_context_t* context, int ep)
{
  const int ep_delta =
    USB_DEVICE_EP1_IN_MPKT_SZ_REG - USB_DEVICE_EP0_IN_MPKT_SZ_REG;

  return __gxio_usb_dev_r32(context, ep * ep_delta +
                            USB_DEVICE_EP0_IN_MPKT_SZ_REG);
}

/** Set the value of an endpoint's input maximum packet size register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Logical endpoint number.
 * @param val New register value.
 */
static __USUALLY_INLINE void
gxio_usb_dev_set_in_mpkt_sz(gxio_usb_dev_context_t* context, int ep,
                         unsigned int val)
{
  const int ep_delta =
    USB_DEVICE_EP1_IN_MPKT_SZ_REG - USB_DEVICE_EP0_IN_MPKT_SZ_REG;

  __gxio_usb_dev_w32(context, ep * ep_delta +
                     USB_DEVICE_EP0_IN_MPKT_SZ_REG, val);
}


#ifndef __DOXYGEN__
//
// Not exposed in the API; used by gxio_usb_dev_write_tx_fifo().
//
static __USUALLY_INLINE void
gxio_usb_dev_wr_cfrm(gxio_usb_dev_context_t* context, int ep)
{
  const int ep_delta =
    USB_DEVICE_EP1_WR_CFRM_UDC_REG - USB_DEVICE_EP0_WR_CFRM_UDC_REG;

  __gxio_usb_dev_w32(context, ep * ep_delta +
                     USB_DEVICE_EP0_WR_CFRM_UDC_REG, 0);
}
#endif


/** Get the value of an endpoint's output control register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Logical endpoint number.
 * @return Register value.
 */
static __USUALLY_INLINE unsigned int
gxio_usb_dev_get_out_ctrl(gxio_usb_dev_context_t* context, int ep)
{
  const int ep_delta =
    USB_DEVICE_EP1_OUT_CTRL_UDC_REG - USB_DEVICE_EP0_OUT_CTRL_UDC_REG;

  return __gxio_usb_dev_r32(context, ep * ep_delta +
                            USB_DEVICE_EP0_OUT_CTRL_UDC_REG);
}

/** Set the value of an endpoint's output control register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Logical endpoint number.
 * @param val New register value.
 */
static __USUALLY_INLINE void
gxio_usb_dev_set_out_ctrl(gxio_usb_dev_context_t* context, int ep,
                         unsigned int val)
{
  const int ep_delta =
    USB_DEVICE_EP1_OUT_CTRL_UDC_REG - USB_DEVICE_EP0_OUT_CTRL_UDC_REG;

  __gxio_usb_dev_w32(context, ep * ep_delta +
                     USB_DEVICE_EP0_OUT_CTRL_UDC_REG, val);
}


/** Get the value of an endpoint's output_status register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Logical endpoint number.
 * @return Register value.
 */
static __USUALLY_INLINE unsigned int
gxio_usb_dev_get_out_sts(gxio_usb_dev_context_t* context, int ep)
{
  const int ep_delta =
    USB_DEVICE_EP1_OUT_STS_UDC_REG - USB_DEVICE_EP0_OUT_STS_UDC_REG;

  return __gxio_usb_dev_r32(context, ep * ep_delta +
                            USB_DEVICE_EP0_OUT_STS_UDC_REG);
}

/** Set the value of an endpoint's output_status register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Logical endpoint number.
 * @param val New register value.
 */
static __USUALLY_INLINE void
gxio_usb_dev_set_out_sts(gxio_usb_dev_context_t* context, int ep,
                        unsigned int val)
{
  const int ep_delta =
    USB_DEVICE_EP1_OUT_STS_UDC_REG - USB_DEVICE_EP0_OUT_STS_UDC_REG;

  __gxio_usb_dev_w32(context, ep * ep_delta +
                     USB_DEVICE_EP0_OUT_STS_UDC_REG, val);
}


/** Get the value of an endpoint's output buffer size register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Logical endpoint number.
 * @return Register value.
 */
static __USUALLY_INLINE unsigned int
gxio_usb_dev_get_out_bufsize(gxio_usb_dev_context_t* context, int ep)
{
  const int ep_delta =
    USB_DEVICE_EP1_OUT_BUFSIZE_UDC_REG - USB_DEVICE_EP0_OUT_BUFSIZE_UDC_REG;

  return __gxio_usb_dev_r32(context, ep * ep_delta +
                            USB_DEVICE_EP0_OUT_BUFSIZE_UDC_REG);
}

/** Set the value of an endpoint's output buffer size register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Logical endpoint number.
 * @param val New register value.
 */
static __USUALLY_INLINE void
gxio_usb_dev_set_out_bufsize(gxio_usb_dev_context_t* context, int ep,
                         unsigned int val)
{
  const int ep_delta =
    USB_DEVICE_EP1_OUT_BUFSIZE_UDC_REG - USB_DEVICE_EP0_OUT_BUFSIZE_UDC_REG;

  uint32_t old_reg = __gxio_usb_dev_r32(context, ep * ep_delta +
                                        USB_DEVICE_EP0_OUT_BUFSIZE_UDC_REG);

  __gxio_usb_dev_adjust_fifo_ptrs(context, ep, 0, (int) (val >> 16) -
                                              (int) (old_reg >> 16));

  __gxio_usb_dev_w32(context, ep * ep_delta +
                     USB_DEVICE_EP0_OUT_BUFSIZE_UDC_REG, val);
}


#ifndef __DOXYGEN__
//
// Not exposed in the API; used by gxio_usb_dev_read_rx_fifo().
//
static __USUALLY_INLINE void
gxio_usb_dev_rd_cfrm(gxio_usb_dev_context_t* context, int ep)
{
  const int ep_delta =
    USB_DEVICE_EP1_RD_CFRM_UDC_REG - USB_DEVICE_EP0_RD_CFRM_UDC_REG;

  (void) __gxio_usb_dev_r32(context, ep * ep_delta +
                            USB_DEVICE_EP0_RD_CFRM_UDC_REG);
}
#endif


/** Get the value of the device configuration register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @return Register value.
 */
static __USUALLY_INLINE unsigned int
gxio_usb_dev_get_d_cfg(gxio_usb_dev_context_t* context)
{
  return __gxio_usb_dev_r32(context, USB_DEVICE_D_CFG_UDC_REG);
}

/** Set the value of the device configuration register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param val New register value.
 */
static __USUALLY_INLINE void
gxio_usb_dev_set_d_cfg(gxio_usb_dev_context_t* context, unsigned int val)
{
  __gxio_usb_dev_w32(context, USB_DEVICE_D_CFG_UDC_REG, val);
}


/** Get the value of the device control register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @return Register value.
 */
static __USUALLY_INLINE unsigned int
gxio_usb_dev_get_d_ctrl(gxio_usb_dev_context_t* context)
{
  return __gxio_usb_dev_r32(context, USB_DEVICE_D_CTRL_UDC_REG);
}

/** Set the value of the device control register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param val New register value.
 */
static __USUALLY_INLINE void
gxio_usb_dev_set_d_ctrl(gxio_usb_dev_context_t* context, unsigned int val)
{
  __gxio_usb_dev_w32(context, USB_DEVICE_D_CTRL_UDC_REG, val);
}


/** Get the value of the device status register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @return Register value.
 */
static __USUALLY_INLINE unsigned int
gxio_usb_dev_get_d_sts(gxio_usb_dev_context_t* context)
{
  return __gxio_usb_dev_r32(context, USB_DEVICE_D_STS_UDC_REG);
}

/** Set the value of the device status register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param val New register value.
 */
static __USUALLY_INLINE void
gxio_usb_dev_set_d_sts(gxio_usb_dev_context_t* context, unsigned int val)
{
  __gxio_usb_dev_w32(context, USB_DEVICE_D_STS_UDC_REG, val);
}


/** Get the value of the device interrupt register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @return Register value.
 */
static __USUALLY_INLINE unsigned int
gxio_usb_dev_get_d_intr(gxio_usb_dev_context_t* context)
{
  return __gxio_usb_dev_r32(context, USB_DEVICE_D_INTR_UDC_REG);
}

/** Set the value of the device interrupt register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param val New register value.
 */
static __USUALLY_INLINE void
gxio_usb_dev_set_d_intr(gxio_usb_dev_context_t* context, unsigned int val)
{
  __gxio_usb_dev_w32(context, USB_DEVICE_D_INTR_UDC_REG, val);
}


/** Get the value of the device interrupt mask register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @return Register value.
 */
static __USUALLY_INLINE unsigned int
gxio_usb_dev_get_d_intr_msk(gxio_usb_dev_context_t* context)
{
  return __gxio_usb_dev_r32(context, USB_DEVICE_D_INTR_MSK_UDC_REG);
}

/** Set the value of the device interrupt mask register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param val New register value.
 */
static __USUALLY_INLINE void
gxio_usb_dev_set_d_intr_msk(gxio_usb_dev_context_t* context, unsigned int val)
{
  __gxio_usb_dev_w32(context, USB_DEVICE_D_INTR_MSK_UDC_REG, val);
}


/** Get the value of the endpoint interrupt register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @return Register value.
 */
static __USUALLY_INLINE unsigned int
gxio_usb_dev_get_ep_intr(gxio_usb_dev_context_t* context)
{
  return __gxio_usb_dev_r32(context, USB_DEVICE_EP_INTR_UDC_REG);
}

/** Set the value of the endpoint interrupt register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param val New register value.
 */
static __USUALLY_INLINE void
gxio_usb_dev_set_ep_intr(gxio_usb_dev_context_t* context, unsigned int val)
{
  __gxio_usb_dev_w32(context, USB_DEVICE_EP_INTR_UDC_REG, val);
}


/** Get the value of the endpoint interrupt mask register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @return Register value.
 */
static __USUALLY_INLINE unsigned int
gxio_usb_dev_get_ep_intr_msk(gxio_usb_dev_context_t* context)
{
  return __gxio_usb_dev_r32(context,
                            USB_DEVICE_EP_INTR_MSK_UDC_REG);
}

/** Set the value of the endpoint interrupt mask register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param val New register value.
 */
static __USUALLY_INLINE void
gxio_usb_dev_set_ep_intr_msk(gxio_usb_dev_context_t* context, unsigned int val)
{
  __gxio_usb_dev_w32(context, USB_DEVICE_EP_INTR_MSK_UDC_REG,
                     val);
}


/** Get the value of an physical endpoint's configuration register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Physical endpoint number.
 * @return Register value.
 */
static __USUALLY_INLINE unsigned int
gxio_usb_dev_get_udc_ep(gxio_usb_dev_context_t* context, int ep)
{
  const int ep_delta =
    USB_DEVICE_UDC_EP_NE_UDC_REG_1 - USB_DEVICE_UDC_EP_NE_UDC_REG_0;
  return __gxio_usb_dev_r32(context, ep * ep_delta +
                            USB_DEVICE_UDC_EP_NE_UDC_REG_0);
}

/** Set the value of an physical endpoint's configuration register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param ep Physical endpoint number.
 * @param val New register value.
 */
static __USUALLY_INLINE void
gxio_usb_dev_set_udc_ep(gxio_usb_dev_context_t* context, int ep,
                        unsigned int val)
{
  const int ep_delta =
    USB_DEVICE_UDC_EP_NE_UDC_REG_1 - USB_DEVICE_UDC_EP_NE_UDC_REG_0;

  return __gxio_usb_dev_w32(context, ep * ep_delta +
                            USB_DEVICE_UDC_EP_NE_UDC_REG_0, val);
}


/** Get the value of the ULPI PHY access register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @return Register value.
 */
static __USUALLY_INLINE unsigned int
gxio_usb_dev_get_udc_vcon(gxio_usb_dev_context_t* context)
{
  return __gxio_usb_dev_r32(context, USB_DEVICE_UDC_VCON_UDC_REG);
}

/** Set the value of the ULPI PHY access register.
 * @param context Pointer to an initialized gxio_usb_dev_context_t.
 * @param val New register value.
 */
static __USUALLY_INLINE void
gxio_usb_dev_set_udc_vcon(gxio_usb_dev_context_t* context, unsigned int val)
{
  __gxio_usb_dev_w32(context, USB_DEVICE_UDC_VCON_UDC_REG, val);
}


/** Request USB device interrupts.
 *
 *  Request that an IPI interrupt be delivered to a tile when an unmasked
 *  USB device interrupt is asserted.  Once an interrupt has been
 *  delivered, no further interrupts will be delivered until
 *  gxio_usb_dev_reset_interrupt() is called.
 *
 * @param context Pointer to a properly initialized gxio_usb_dev_context_t.
 * @param target_cpu CPU number of interrupt target tile.
 * @param target_event Specific event which will be set in the IPI0
 *  register when the interrupt occurs.  If this event is -1, then the
 *  interrupt will be disabled.
 * @return Zero if the device was successfully configured to interrupt;
 *  otherwise, a nonzero error code.
 */
extern int
gxio_usb_dev_cfg_interrupt(gxio_usb_dev_context_t* context,
                           int target_cpu, int target_event);



/** Reset the USB device interrupt, allowing subsequent device interrupts
 *  to be delivered.  To prevent lost interrupts, the caller should check
 *  for and process device and endpoint events after calling this routine
 *  and before returning from the interrupt handler.
 *
 * @param context Pointer to a properly initialized gxio_usb_dev_context_t.
 * @return Zero if the interruptdevice was successfully reset, otherwise, a
 *  nonzero error code.
 */
extern int
gxio_usb_dev_reset_interrupt(gxio_usb_dev_context_t* context);


/** @} */

#endif /* _GXIO_USB_DEV_H_ */
