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
 * Definition of the link plugin interface.
 *
 * A link plugin is a set of routines which allow various Ethernet
 * link types to be managed in a consistent manner.  Effectively, the
 * plugin is a mini-device driver for the link's hardware (for instance,
 * an external PHY device).  The link plugin interface is experimental
 * and is likely to change in the future.
 *
 * Note that link plugins function in both the hypervisor and the BME
 * environments, and must therefore restrict themselves to services which
 * appear in both.  In particular, the BME does not support the driver
 * memory allocation routines used in the hypervisor, so link plugins may
 * not dynamically allocate memory.
 *
 * To add a new plugin, in addition to providing implementations of these
 * routines, one would need to provide an instance of an enet_link_ops_t
 * structure holding pointers to those routines, and then add a pointer to
 * that structure to enet_link_plugins[], in sys/common_hvbme/xgbe_link.c.
 */

#ifndef _SYS_COMMON_ENET_PLUGIN_H
#define _SYS_COMMON_ENET_PLUGIN_H

#include <stdint.h>

#ifndef __DOXYGEN__
// This solves a circular reference problem between the link_config
// structure and the link_ops structure.  The function signatures
// below use struct _enet_link_config for this same reason, but in
// general one would expect actual plugins to use enet_link_config_t.
struct _enet_link_config;
#endif


/** Link plugin probe routine.
 *
 *  A plugin's probe() routine is responsible for determining whether the
 *  plugin is capable of controlling a particular link, and, if so, for
 *  setting certain link properties in the link_config structure.  In
 *  making this decision, an important source of information is the gbe and
 *  phytype members of the link_config structure, and many probe routines
 *  may be able to make their decisions based soley on them.  However, the
 *  probe routine may do additional I/O accesses (for instance, MDIO reads
 *  and writes to a PHY) to the device if needed.
 *
 *  Since the device might not ever be enabled, or if enabled, might not be
 *  controlled by the plugin whose probe routine is used, the probe routine
 *  should in general not allocate any resources or set any state which
 *  would be depended upon by a later driver routine.
 *
 *  In addition to providing a return value, the probe routine is
 *  responsible for setting the following members of the link_config
 *  structure:
 *
 *  - link_can_intr, which specifies whether the plugin is prepared to
 *    handle the link in interrupt mode;
 *  
 *  - link_int_actlow, which specifies whether the interrupt signal from
 *    the PHY, if any, is active low; and
 *
 *  - possible_state, which specifies the capabilities of the link (e.g.,
 *    the speeds it can run at).  On TILE-Gx, this value is preinitialized
 *    to the available link speeds from the BIB, and the plugin is
 *    responsible for clearing any speeds it doesn't support, and adding the
 *    other capability bits; on TILEPro, the plugin is responsible for
 *    setting all of the possible_state bits.
 *
 *  No other values in the link_config structure should be modified,
 *  and these values must not be touched unless the probe routine
 *  returns nonzero.
 *
 * @param lc Pointer to the link configuration structure for the link.
 * @return Nonzero if this plugin can handle the described link, zero if it
 *  cannot.
 */
typedef int enet_link_probe_func(struct _enet_link_config* lc);

/** Link plugin initialization routine.
 *
 *  A plugin's init() routine will be called once, on one tile, after the
 *  probe() routine has been called, but before any other calls are made to
 *  other plugin entry points for that link.  The purpose of the init
 *  routine is to configure any link hardware to an inactive (down) state.
 *  It may also do any other one-time setup required for use of the link
 *  (for instance, loading firmware into an intelligent PHY).
 *
 *  In addition to configuring the hardware, if the plugin may operate
 *  in interrupt mode, its init routine is responsible for configuring the
 *  mac_intrs member of the link_config structure.
 *
 * @param lc Pointer to the link configuration structure for the link.
 * @return Zero if the link was successfully initialized; a negative error
 *   code if it could not be.
 */
typedef int enet_link_init_func(struct _enet_link_config* lc);

/** Link plugin begin configuration routine.
 *
 *  A plugin's begin_configuration() routine is responsible for initiating
 *  a link state change: for instance, bringing a link up or taking it
 *  down.  Typically, the state change is completed later, either in the
 *  wait_config() routine for plugins operating in non-interrupt mode, or
 *  in the intr() routine for plugins operating in interrupt mode.  In
 *  some cases it might be appropriate for this routine to wait short
 *  amounts of time for various configuration operations to complete, but
 *  in general anything which could be handled by one of those subsequent
 *  routines should be taken care of there.
 *
 * @param lc Pointer to the link configuration structure for the link.
 * @param new_config Desired configuration for the link (a mask of
 *  ENET_LINK_xxx values; if none of the bits found in ENET_LINK_SPEED
 *  are set, the link is to be taken down).
 * @return Zero if the configuration change was successfully initiated
 *  (not necessarily completed); a negative error code if it was not.
 */
typedef int enet_link_start_config_func(struct _enet_link_config* lc,
                                        uint32_t new_config);

/** Link plugin get state routine.
 *
 *  A plugin's get_state() routine determines the current state of the
 *  link, typically by interrogating the hardware.  The overall link
 *  framework will generally not call this routine unless needed; in
 *  particular, if the link is being managed in interrupt mode, it will
 *  trust the current_state value from the link_config structure.  Because
 *  of that, this routine should get the latest state from the hardware and
 *  should not rely on cached data.  This routine is often called
 *  repeatedly as part of the implementation of the wait_config() function,
 *  so it should be efficient.
 *
 * @param lc Pointer to the link configuration structure for the link.
 * @return Current state for the link (a mask of ENET_LINK_xxx values; if
 *  none of the bits found in ENET_LINK_SPEED are set, the link is down).
 */
typedef uint32_t enet_link_get_state_func(struct _enet_link_config* lc);

/** Link plugin wait for configuration routine.
 *
 *  A plugin's wait_config() routine waits for some unspecified but
 *  reasonable amount of time to see whether the link's current state
 *  becomes equal to its desired state; if so, it calls
 *  enet_new_link_state() to convey it to the link framework.
 *
 * @param lc Pointer to the link configuration structure for the link.
 * @return Zero if the link's current state is equal to its desired
 *  state; a negative error code if it is not.
 */
typedef int enet_link_wait_config_func(struct _enet_link_config* lc);

/** Link plugin interrupt routine.
 *
 *  A plugin's intr() routine is called when one of the MAC interrupts
 *  it named in the link_config structure occurs.  Typically, the
 *  routine will then get the link's current state, probably with
 *  the get_state() routine, and call enet_new_link_state() to convey
 *  it to the link framework.
 *
 *  Plugins may be used in interrupt and non-interrupt modes, and the
 *  intr() routine is only called when in interrupt mode.  A plugin
 *  advertises its capability to be used in interrupt mode by setting the
 *  link_can_intr field of the link_config structure; however, the
 *  framework may still decide to use it in non-interrupt mode.  The
 *  link_does_intr field of the link_config structure determines whether
 *  the plugin is currently being used in interrupt mode; this decision
 *  is made once, before the plugin's init() routine is called.
 *
 *  The intr() routine is responsible for resetting the shim's interrupt
 *  status bit for the interrupt which caused it to be invoked.  This must
 *  be done by the plugin itself because it must be done after the plugin
 *  has retrieved and cleared any PHY interrupts.  To avoid losing events
 *  which occur between checking for PHY interrupts and clearing the MAC
 *  interrupt the plugin should re-check its PHY interrupt status after
 *  the MAC interrupt status has been reset.
 *
 * @param lc Pointer to the link configuration structure for the link.
 * @param mac_intrnum Interrupt bit number of the interrupt in the shim's
 *  MAC interrupt register which caused this routine to be called.
 * @return Nonzero if the routine found that the MAC or PHY was asserting
 *  an interrupt; zero if neither were doing so.  (This may eventually
 *  be used to allow interrupt lines shared between multiple PHYs,
 *  although the link framework does not currently support that.)
 */
typedef int enet_link_intr_func(struct _enet_link_config* lc, int mac_intrnum);

/** Link plugin module EEPROM read routine.
 *
 *  A plugin's get_module_eeprom() routine retrieves data from the EEPROM
 *  of a plug-in SFP module, if such a module is associated with the given
 *  link.  As with the corresponding GXIO operation, the first 256 bytes of
 *  the EEPROM address space are the SFF-8079 EEPROM values and the next
 *  256 bytes are the SFF-8472 dynamic optical monitoring values.
 *
 * @param lc Pointer to the link configuration structure for the link.
 * @param type Pointer to returned module type (ENET_MODULE_xxx); may be
 *  NULL in which case no type is returned.
 * @param offset Offset within the EEPROM at which to start the transfer.
 * @param buf Buffer to hold the transferred bytes.
 * @param len Number of bytes to transfer.
 * @return The number of bytes that were available to be transferred at the
 *  given offset, or a negative error code.  Note that this number of bytes
 *  may be more, or less, than the number of bytes that were specified in
 *  len.
 */
typedef int enet_get_module_eeprom_func(struct _enet_link_config* lc,
                                        int* type, int offset, void* buf,
                                        int len);


/** Link plugin operations structure; defines the routines for a particular
 *  link plugin. */
typedef struct _enet_link_ops
{
  /** Name of the plugin. */
  char* name;
  /** Probe routine. */
  enet_link_probe_func* probe;
  /** Initialization routine. */
  enet_link_init_func* init;
  /** Begin configuration routine. */
  enet_link_start_config_func* start_config;
  /** Get state routine. */
  enet_link_get_state_func* get_state;
  /** Wait for configuration routine. */
  enet_link_wait_config_func* wait_config;
  /** Interrupt routine. */
  enet_link_intr_func* intr;
  /** Get module eeprom routine. */
  enet_get_module_eeprom_func* get_module_eeprom;
} enet_link_ops_t;

/** Table of link plugin ops pointers. */
extern const enet_link_ops_t* enet_link_plugins[];

/** Null link ops vector, used when no appropriate plugin found. */
extern const enet_link_ops_t enet_null_link_ops;

/** Loopback ops vector, used for mPIPE loopback channels */
extern const enet_link_ops_t enet_loop_link_ops;

#endif // _SYS_COMMON_ENET_PLUGIN_H
