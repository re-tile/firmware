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
#ifndef _GXIO_GPIO_H_
#define _GXIO_GPIO_H_

#include <gxio/common.h>

#include <arch/gpio.h>

#include <hv/drv_gpio_gxio_intf.h>
#include <hv/iorpc.h>

__BEGIN_DECLS

/**
 * @file
 *
 * An API for manipulating general-purpose I/O pins.
 */

/**
 * @addtogroup gxio_gpio
 * @{
 *
 * The GPIO shim allows access to the processor's general purpose I/O pins.
 * These pins are normally used for low-speed devices like switches and
 * indicators, or to perform board functions such as device reset or
 * interrupt notification.  GPIO pins can also be used for moderate-speed
 * data transfer, emulating a low-speed communications bus via software
 * control.
 *
 * The gxio_gpio API, declared in <gxio/gpio.h>, allows applications to
 * allocate particular GPIO pins, then assert, deassert, or sense the state
 * of those pins.  The API is designed to be a minimal wrapper around the
 * GPIO hardware, making system calls only for configuration purposes.
 *
 * We strongly recommend reading the GPIO section of the IO Device Guide
 * (UG404) before working with this API.  Most functions in the gxio_gpio
 * API are directly analogous to hardware interfaces, and the documentation
 * assumes that the reader understands those hardware interfaces.  Proper
 * configuration and use of GPIO pins generally requires knowledge of
 * specific details of the system design, such as might be found in the
 * hardware reference manual or board schematics.  Note that in many
 * systems, a subset of the available pins are used for board control
 * purposes and are not available via the gxio_gpio API.
 */

/** A context object used to manage GPIO hardware resources. */
typedef struct {

  /** File descriptor for calling up to Linux (and thus the HV). */
  int fd;

  /** The VA at which our MMIO registers are mapped. */
  char* mmio_base;
} gxio_gpio_context_t;


/** Initialize a GPIO context.
 *
 *  A properly initialized context must be obtained before any of the other
 *  gxio_gpio routines may be used.
 *
 * @param context Pointer to a gxio_gpio_context_t, which will be initialized
 *  by this routine, if it succeeds.
 * @param gpio_index Index of the GPIO shim to use.
 * @return Zero if the context was successfully initialized, else a
 *  GXIO_ERR_xxx error code.
 */
extern int
gxio_gpio_init(gxio_gpio_context_t* context, int gpio_index);

/** Destroy a GPIO context.
 *
 *  Once destroyed, a context may not be used with any gxio_gpio routines
 *  other than gxio_gpio_init().  After this routine returns, no further
 *  interrupts requested on this context will be delivered.  The state and
 *  configuration of the pins which had been attached to this context are
 *  unchanged by this operation.
 *
 * @param context Pointer to a gxio_gpio_context_t.
 * @return Zero if the context was successfully destroyed, else a
 *  GXIO_ERR_xxx error code.
 */
extern int
gxio_gpio_destroy(gxio_gpio_context_t* context);

/** Request control of a set of GPIO pins.
 *
 *  This routine must be called before any pin state can be sensed or
 *  modified.  Attempts to set, clear, or sense the state of pins which
 *  have not been attached to via this routine will fail silently.
 *  Attempts to reconfigure pins which have not been attached to via this
 *  routine will return an error.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param pin_mask Mask of pins to be associated with this context.  If
 *  more than one successful call to this routine is made, the set of pins
 *  attached will be those specified on the last successful call; in other
 *  words, this routine does not add to the set of attached pins, it
 *  defines that set.
 * @return Zero if all of the requested pins were available and were
 *  successfully associated with this context.  If any of the requested pins
 *  are not available for use, either ::GXIO_GPIO_ERR_PINS_UNAVAIL
 *  or ::GXIO_GPIO_ERR_PINS_BUSY will be returned, and the set of pins
 *  attached to this context will remain unchanged.
 */
extern int
gxio_gpio_attach(gxio_gpio_context_t* context, uint64_t pin_mask);

/** Return the current state of a context's attached GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @return Mask of pin state.  Normally, a pin's bit is 1 if and only if
 *  it is an input pin and it is asserted.  gxio_gpio_set_input_inv() can
 *  invert input pins, and gxio_gpio_set_in_cnd() can cause pins to be
 *  sampled even if they are configured as outputs.
 */
extern uint64_t
gxio_gpio_get(gxio_gpio_context_t* context);

/** Set the output state of some of a context's attached GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param pins Mask specifying the new output state for the pins to be
 *  modified.  By default, a 1 here means that the output signal is asserted;
 *  this can be reversed for certain pins with gxio_gpio_set_out_inv().
 * @param mask Mask specifying which pins' state is modified; if a pin's
 *  bit is off in this mask, its output state is unchanged.
 */
extern void
gxio_gpio_set(gxio_gpio_context_t* context, uint64_t pins, uint64_t mask);

/** Toggle (invert) the output state of some of a context's attached GPIO
 *  pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param pins Mask specifying which pins should have their output states
 *  toggled.
 */
extern void
gxio_gpio_toggle(gxio_gpio_context_t* context, uint64_t pins);

/** Assert some of a context's attached GPIO output pins for one
 *  GPIO clock cycle.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param pins Mask specifying which pins should have their output states
 *  temporarily asserted.
 */
extern void
gxio_gpio_pulse_assert(gxio_gpio_context_t* context, uint64_t pins);

/** Deassert some of a context's attached GPIO output pins for one
 *  GPIO clock cycle.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param pins Mask specifying which pins should have their output states
 *  temporarily deasserted.
 */
extern void
gxio_gpio_pulse_deassert(gxio_gpio_context_t* context, uint64_t pins);

/** Release some of a context's attached GPIO output pins.
 *
 *  Any pin whose bit is set in the supplied pin mask will be tri-stated,
 *  ignoring the current output state.  A subsequent call to this routine
 *  which clears that bit in the pin mask will resume that pin's output.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param pins Mask specifying which pins should have their output states
 *  tri-stated.
 */
extern void
gxio_gpio_release(gxio_gpio_context_t* context, uint64_t pins);

/** Retrieve the current electrical settings for one of a context's
 *  attached GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param pin Number of the pin whose settings should be retrieved.
 * @param pad_ctl On successful return, current settings for the specified pin.
 * @return Zero if the settings were retrieved successfully; a nonzero error
 *  code if the process had not successfully attached to the relevant pin.
 */
extern int
gxio_gpio_get_elec(gxio_gpio_context_t* context, unsigned int pin,
                   GPIO_PAD_CONTROL_t *pad_ctl);

/** Change the current electrical settings for one of a context's
 *  attached GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param pin Number of the pin whose settings should be changed.
 * @param pad_ctl New settings for the specified pin.
 * @return Zero if the register was updated successfully; a nonzero error
 *  code if the process had not successfully attached to the relevant pin,
 *  or if the specific parameters are found to be outside acceptable limits.
 */
extern int
gxio_gpio_set_elec(gxio_gpio_context_t* context, unsigned int pin,
                   GPIO_PAD_CONTROL_t pad_ctl);

/** Retrieve the current clock settings for a context's GPIO shim.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param gclk_mode On successful return, current clock settings.
 * @return Zero if the settings were retrieved successfully; a nonzero error
 *  code otherwise.
 */
extern int
gxio_gpio_get_gclk_mode(gxio_gpio_context_t* context,
                        GPIO_GCLK_MODE_t* gclk_mode);

/** Change the current clock settings for a context's GPIO shim.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param gclk_mode New settings for the shim.
 * @return Zero if the register was updated successfully; a nonzero error
 *  code if the process is not allowed to update this register, or if
 *  the specific parameters are found to be outside acceptable limits.
 */
extern int
gxio_gpio_set_gclk_mode(gxio_gpio_context_t* context,
                        GPIO_GCLK_MODE_t gclk_mode);

/** Retrieve the current output inversion settings for a context's
 *  attached GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @return Mask of current settings for the pins; 1 if a pin's output
 *  state is inverted.
 */
extern uint64_t
gxio_gpio_get_out_inv(gxio_gpio_context_t* context);

/** Change the current output inversion settings for a context's
 *  attached GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param pins Mask of new settings for the pins; 1 if a pin's output
 *  state is to be inverted.
 */
extern void
gxio_gpio_set_out_inv(gxio_gpio_context_t* context, uint64_t pins);

/** Retrieve the current input inversion settings for a context's
 *  attached GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @return Mask of current settings for the pins; 1 if a pin's input
 *  state is inverted.
 */
extern uint64_t
gxio_gpio_get_in_inv(gxio_gpio_context_t* context);

/** Change the current input inversion settings for a context's
 *  attached GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param pins Mask of new settings for the pins; 1 if a pin's input
 *  state is to be inverted.
 */
extern void
gxio_gpio_set_in_inv(gxio_gpio_context_t* context, uint64_t pins);

/** Retrieve the current output mask settings for a context's attached
 *  GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @return Mask of current settings for the pins; 1 if a pin's output
 *  state is masked.  The output state of masked pins is not affected
 *  by calls to gxio_gpio_set().
 */
extern uint64_t
gxio_gpio_get_out_mask(gxio_gpio_context_t* context);

/** Change the current output mask settings for a context's
 *  attached GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param pins Mask of new settings for the pins; 1 if a pin's output
 *  state is to be masked.  The output state of masked pins is not affected
 *  by calls to gxio_gpio_set().
 */
extern void
gxio_gpio_set_out_mask(gxio_gpio_context_t* context, uint64_t pins);

/** Retrieve the current input mask settings for a context's attached
 *  GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @return Mask of current settings for the pins; 1 if a pin's input
 *  state is masked.  The input state of masked pins is not reflected
 *  in the result of gxio_gpio_get(); masked pins always return zero.
 */
extern uint64_t
gxio_gpio_get_in_mask(gxio_gpio_context_t* context);

/** Change the current input mask settings for a context's
 *  attached GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param pins Mask of new settings for the pins; 1 if a pin's input
 *  state is to be masked.  The input state of masked pins is not reflected
 *  in the result of gxio_gpio_get(); masked pins always return zero.
 */
extern void
gxio_gpio_set_in_mask(gxio_gpio_context_t* context, uint64_t pins);

/** Retrieve the current input synchronization settings for a context's
 *  attached GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @return Mask of current settings for the pins; 1 if a pin's input
 *  state is synchronized.
 */
extern uint64_t
gxio_gpio_get_in_sync(gxio_gpio_context_t* context);

/** Change the current input synchronization settings for a context's
 *  attached GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param pins Mask of new settings for the pins; 1 if a pin's input
 *  state is to be synchronized.
 */
extern void
gxio_gpio_set_in_sync(gxio_gpio_context_t* context, uint64_t pins);

/** Retrieve the current input sampling settings for a context's
 *  attached GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @return Mask of current settings for the pins; 1 if a pin's state
 *  state is sampled even when it is configured as an output.
 */
extern uint64_t
gxio_gpio_get_in_cnd(gxio_gpio_context_t* context);

/** Change the current input sampling settings for a context's
 *  attached GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param pins Mask of new settings for the pins; 1 if a pin's state
 *  state is to be sampled even when it is configured as an output.
 */
extern void
gxio_gpio_set_in_cnd(gxio_gpio_context_t* context, uint64_t pins);

/** Retrieve the current input/output settings for a context's GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param disabled_pins Pointer to a pin mask.  Upon return, this mask
 *  will contain a 1 bit corresponding to each pin which is disabled.
 * @param input_pins Pointer to a pin mask.  Upon return, this mask
 *  will contain a 1 bit corresponding to each pin which is configured
 *  as an input pin.
 * @param output_pins Pointer to a pin mask.  Upon return, this mask
 *  will contain a 1 bit corresponding to each pin which is configured
 *  as an output pin.
 * @param output_od_pins Pointer to a pin mask.  Upon return, this mask
 *  will contain a 1 bit corresponding to each pin which is configured
 *  as an open-drain output pin.
 */
extern int
gxio_gpio_get_dir(gxio_gpio_context_t* context, uint64_t* disabled_pins,
                  uint64_t* input_pins, uint64_t* output_pins,
                  uint64_t* output_od_pins);

/** Modify the current input/output settings for a context's GPIO pins.
 *
 *  The settings for pins not specified in any mask are unchanged.  If pins
 *  are specified in more than one mask, the behavior is undefined.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param disabled_pins Mask of pins which should be disabled.
 * @param input_pins Mask of pins to be configured for input.
 * @param output_pins Mask of pins to be configured for output.
 * @param output_od_pins Mask of pins to be configured for open-drain output.
 */
extern int
gxio_gpio_set_dir(gxio_gpio_context_t* context, uint64_t disabled_pins,
                  uint64_t input_pins, uint64_t output_pins,
                  uint64_t output_od_pins);


/** Request GPIO pin interrupts.
 *
 *  Request that interrupts be delivered to a tile when input pins attached
 *  to a context are asserted and/or deasserted.  Once such an interrupt is
 *  delivered for a specific pin, that specific interrupt will no longer be
 *  delivered for that pin until gxio_gpio_reset_interrupt() is called.
 *  The same GPIO pin and assertion/deassertion status may not be
 *  concurrently specified in more than one of gxio_gpio_cfg_interrupt() or
 *  gxio_gpio_cfgpoll(); attempts to do so will cause undefined results.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param target_cpu CPU number of interrupt target tile.
 * @param target_event Specific event which will be set in the IPI0
 *  register when the interrupt occurs.  If this event is -1, then the
 *  interrupt events specified in on_assert/on_deassert will be disabled
 *  for the specified pins.
 * @param on_assert Mask of pins whose assertion will cause this interrupt.
 *  If more than one successful call to this routine is made, the set of
 *  pins which interrupt is the union of those specified on all successful
 *  calls.
 * @param on_deassert Mask of pins whose deassertion will cause this
 *  interrupt.  If more than one successful call to this routine is made,
 *  the set of pins which interrupt is the union of those specified on all
 *  successful calls.
 * @return Zero if all of the requested pins were successfully configured
 *  to interrupt.  If any of the requested pins are not attached to this
 *  context, GXIO_GPIO_ERR_PINS_UNAVAIL will be returned, and no pins'
 *  interrupt state will be changed.
 */
extern int
gxio_gpio_cfg_interrupt(gxio_gpio_context_t* context,
                        int target_cpu, int target_event,
                        uint64_t on_assert, uint64_t on_deassert);


/** Report GPIO pin interrupts.
 *
 *  Retrieve the interrupt state of all GPIO pins.  This can be useful in
 *  determining which pin caused an interrupt when multiple pins are
 *  associated with the same target event.  Note that the current state of
 *  the pin may have changed since the interrupt occurred.  Note also that
 *  even after this routine has noted that a pin caused an interrupt, until
 *  the interrupt status is reset with gxio_gpio_reset_interrupt() or
 *  gxio_gpio_report_reset_interrupt(), subsequent calls to this routine
 *  will continue to report that pin as having caused an interrupt.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param asserted Pointer to a mask which will be written with the set of
 *  pins which have interrupted due to pin assertion and whose interrupt
 *  status has not been reset.  May be NULL if no information on assertion
 *  interrupts is desired.
 * @param deasserted Pointer to a mask which will be written with the set of
 *  pins which have interrupted due to pin deassertion and whose interrupt
 *  status has not been reset.  May be NULL if no information on deassertion
 *  interrupts is desired.
 */
extern void
gxio_gpio_report_interrupt(gxio_gpio_context_t* context,
                           uint64_t* asserted, uint64_t* deasserted);

/** Report and reenable GPIO pin interrupts.
 *
 *  Retrieve the interrupt state of all GPIO pins, and reenable interrupts
 *  on pins which had previously caused an interrupt.  This can be useful
 *  in determining which pin caused an interrupt when multiple pins are
 *  associated with the same target event.  Note that the current state
 *  of the pin may have changed since the interrupt occurred.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param asserted Pointer to a mask which will be written with the set of
 *  pins which have interrupted due to pin assertion and whose interrupt
 *  status has not been reset.  May be NULL if no information on assertion
 *  interrupts is desired; in this case, no assertion interrupts will be
 *  reenabled, even if some have occurred.
 * @param deasserted Pointer to a mask which will be written with the set of
 *  pins which have interrupted due to pin deassertion and whose interrupt
 *  status has not been reset.  May be NULL if no information on deassertion
 *  interrupts is desired; in this case, no deassertion interrupts will be
 *  reenabled, even if some have occurred.
 */
extern void
gxio_gpio_report_reset_interrupt(gxio_gpio_context_t* context,
                                 uint64_t* asserted, uint64_t* deasserted);

/** Reenable GPIO pin interrupts.
 *
 *  Request that interrupts be reenabled for a context's attached GPIO
 *  pins.  As noted in gxio_gpio_cfg_interrupt(), interrupts are one-time
 *  events; once a pin assertion or deassertion has produced an interrupt,
 *  subsequent occurrences of the same event will not produce an interrupt
 *  until it has been reenabled.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param en_assert Mask of pins for which the assertion interrupt
 *  should be enabled.
 * @param en_deassert Mask of pins for which the deassertion interrupt
 *  should be enabled.
 */
extern void
gxio_gpio_reset_interrupt(gxio_gpio_context_t* context,
                          uint64_t en_assert, uint64_t en_deassert);


/** Get a file descriptor which may be polled to determine GPIO pin state
 *  changes.
 *
 *  Before the returned file descriptor can be used with poll() or
 *  select(), it must first be armed with gxio_gpio_arm_pollfd().  See the
 *  description of that routine for the precise behavior of the arming
 *  operation.  Note that the application must not do any read() or write()
 *  operations on the returned file descriptor.
 *
 *  The same GPIO pin and assertion/deassertion status may not be
 *  concurrently specified in more than one of gxio_gpio_cfg_interrupt() or
 *  gxio_gpio_cfgpoll(); attempts to do so will cause undefined results.
 *  Similarly, modification of the interrupt state for pins specified in a
 *  call to this routine may also cause undefined results.
 *
 *  If a GPIO context is destroyed, any file descriptor returned from
 *  this routine specifying that context is no longer valid.  If the file
 *  descriptor is no longer needed before destruction of the GPIO context,
 *  it may be explicitly closed.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param on_assert Mask of pins whose transition from deasserted to
 *  asserted will cause the returned and armed file descriptor to become
 *  readable.
 * @param on_deassert Mask of pins whose transition from asserted to
 *  deasserted will cause the returned and armed file descriptor to become
 *  readable.
 * @return If the call was successful, a non-negative file descriptor;
 *  otherwise, a negative error code.
 */
extern int
gxio_gpio_get_pollfd(gxio_gpio_context_t* context,
                     uint64_t on_assert, uint64_t on_deassert);

/** Arm a pollable file descriptor.
 *
 *  Calling this routine clears the interrupt status bits which were
 *  specified when gxio_gpio_get_pollfd() was called to retrieve the
 *  pollable file descriptor.  Once any of those interrupt status bits
 *  becomes asserted, the file descriptor passed to this routine will
 *  appear to be readable, as determined by the poll() or select()
 *  routines.
 *
 *  After that first interrupt assertion has occurred, the file descriptor
 *  will remain readable until this routine is called again; in other
 *  words, there is no more than one transition of readability, from
 *  unreadable to readable, per call to this routine.
 *
 *  Applications may request that an asynchronous signal be sent upon
 *  interrupt assertion by using fcntl() with the F_SETOWN command on the
 *  pollable file descriptor to set a target process ID or process group,
 *  and then using fcntl() with the F_SETFL command to set the O_ASYNC flag
 *  on that file descriptor.  The requested signal is sent on the first
 *  transition of the file descriptor from unreadable to readable.  No more
 *  than one signal will be sent per call to this routine.
 *
 *  Note that GPIO pins may change while this routine is executing, and
 *  that multiple changes may happen simultaneously.  Applications waiting
 *  for a specific pin state should check for that state after calling this
 *  routine, since the state may have been attained before the interrupt
 *  status was reset; in that case there would be no readability transition
 *  on the file descriptor, and no signal, since GPIO interrupts are edge-
 *  and not level-sensitive.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param fd File descriptor obtained from gxio_gpio_get_pollfd().
 * @return If the call was successful, zero; otherwise, a negative error
 *  code.
 */
extern int
gxio_gpio_arm_pollfd(gxio_gpio_context_t* context, int fd);


/** Look up a named set of GPIO pins and retrieve pin characteristics.
 *
 *  Pinset names are system-dependent, but are generally hierarchical; for
 *  instance, "panel/led/0" might be the pin connected to the first
 *  front-panel LED, while "IDE/0/data" might be the pins on the
 *  data bus for IDE slot 0.  Pins can be configurable in more than
 *  one way (for instance, a pin might be usable as either an input
 *  or an output) and may be members of more than one pinset.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param name Name to be looked up.
 * @param input_pins Pointer to a pin mask.  Upon return, this mask
 *  will contain a 1 bit corresponding to each pin in the named set
 *  which could be used as an input pin.
 * @param output_pins Pointer to a pin mask.  Upon return, this mask
 *  will contain a 1 bit corresponding to each pin in the named set
 *  which could be used as an output pin.
 * @param output_od_pins Pointer to a pin mask.  Upon return, this mask
 *  will contain a 1 bit corresponding to each pin in the named set
 *  which could be used as an open-drain output pin.
 * @param inverted_pins Pointer to a pin mask.  Upon return, this mask
 *  will contain a 1 bit corresponding to each pin in the named set
 *  which should be configured as an inverted pin.
 * @return If the call was successful, zero; otherwise, a negative error
 *  code.
 */
extern int
gxio_gpio_get_pinset(gxio_gpio_context_t* context, const char* name,
                     uint64_t* input_pins, uint64_t* output_pins,
                     uint64_t* output_od_pins, uint64_t* inverted_pins);

/** Retrieve the properties of one of this system's named sets of GPIO pins.
 *
 * @param context Pointer to a properly initialized gxio_gpio_context_t.
 * @param index Pinset index.  If a system supports N named sets of pins,
 *  then indices between 0 and N - 1, inclusive, each correspond to one of
 *  those sets.  Thus, to retrieve all of a system's named pinsets, call
 *  this function in a loop, starting with an index of zero, and incrementing
 *  it once per iteration until an error is returned.
 * @param name Pointer to the buffer which will receive the name of the
 *  set.  The buffer should contain space for at least ::GXIO_GPIO_PIN_NAME_LEN
 *  bytes; the returned name, including the terminating null byte, will be no
 *  longer than that.
 * @param input_pins Pointer to a pin mask.  Upon return, this mask
 *  will contain a 1 bit corresponding to each pin in the named set
 *  which could be used as an input pin.
 * @param output_pins Pointer to a pin mask.  Upon return, this mask
 *  will contain a 1 bit corresponding to each pin in the named set
 *  which could be used as an output pin.
 * @param output_od_pins Pointer to a pin mask.  Upon return, this mask
 *  will contain a 1 bit corresponding to each pin in the named set
 *  which could be used as an open-drain output pin.
 * @param inverted_pins Pointer to a pin mask.  Upon return, this mask
 *  will contain a 1 bit corresponding to each pin in the named set
 *  which should be configured as an inverted pin.
 * @return Zero if a named set of pins was successfully retrieved; a
 *  negative error code if one was not.
 */
extern int
gxio_gpio_enumerate_pinset(gxio_gpio_context_t* context, int index,
                           char* name, uint64_t* input_pins,
                           uint64_t* output_pins, uint64_t* output_od_pins,
                           uint64_t* inverted_pins);

__END_DECLS

/** @} */

#endif /* _GXIO_GPIO_H_ */
