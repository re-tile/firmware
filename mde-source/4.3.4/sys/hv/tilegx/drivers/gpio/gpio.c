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
 * GPIO driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/gpio.h>

#include "debug.h"
#include "drvintf.h"
#include "hv.h"
#include "gpio.h"
#include "gpio_rpc_dispatch.h"
#include "cfg.h"
#include "board_info.h"
#include "devices.h"


/** Lock used to make sure that only one tile allocates shared state. */
static drv_spinlock_t gpio_alloc_lock _SHARED = DRV_SPINLOCK_INIT;

/** Address of the shared state object. */
gpio_state_t* gpio_state[MAX_GPIOS] _SHARED = { 0 };


/** Reserve a service domain on a GPIO shim.
 * @param gs Driver state.
 * @return Service domain number, or a negative error code.
 */
static int
gpio_reserve_svc_dom(gpio_state_t* gs)
{
  unsigned long bitmask = gs->reserved_svc_dom_bitmask;
  int svc_dom;
  //
  // We start at 1 since we reserve domain 0 for hypervisor use.
  //
  for (svc_dom = 1; svc_dom < HV_GPIO_NUM_SVC_DOM; svc_dom++)
  {
    if ((bitmask & (1ULL << svc_dom)) == 0)
      break;
  }
  if (svc_dom >= HV_GPIO_NUM_SVC_DOM)
    return GXIO_ERR_NO_SVC_DOM;

  gs->reserved_svc_dom_bitmask |= (1ULL << svc_dom);

  return svc_dom;
}


/** Unreserve a service domain on a GPIO shim.
 * @param gs Driver state.
 * @param svc_dom Service domain number.
 */
static void
gpio_unreserve_svc_dom(gpio_state_t* gs, int svc_dom)
{
  if (svc_dom < HV_GPIO_NUM_SVC_DOM && svc_dom > 0)
    gs->reserved_svc_dom_bitmask &= ~(1ULL << svc_dom);
}


/** See if a service domain on a GPIO shim is legal, and open (i.e., reserved).
 * @param gs Driver state.
 * @param svc_dom Service domain number.
 * @return Nonzero if the given service domain is legal and open.
 */
static int
gpio_svc_dom_is_open(gpio_state_t* gs, int svc_dom)
{
  return svc_dom < HV_GPIO_NUM_SVC_DOM && svc_dom > 0 &&
    (gs->reserved_svc_dom_bitmask & (1ULL << svc_dom));
}


/** Return the base PTE that the client should use to access our
 *  shim's MMIO registers.
 * @param gs Driver state.
 * @param svc_dom Service domain number.
 * @param base Pointer to returned base PTE.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_gpio_get_mmio_base(gpio_state_t* gs, int svc_dom, HV_PTE *base)
{
  PA pa = HV_GPIO_MMIO_OFFSET(svc_dom);

  HV_PTE pte = { 0 };
  pte = hv_pte_set_mode(pte, HV_PTE_MODE_MMIO);
  pte = hv_pte_set_lotar(pte, HV_XY_TO_LOTAR(gs->shim_pos.bits.x,
                                             gs->shim_pos.bits.y));
  pte = hv_pte_set_pa(pte, pa);

  *base = pte;

  return 0;
}


/** Check whether an MMIO range is legal.
 * @param gs Driver state.
 * @param svc_dom Service domain number.
 * @param offset Start of the range.
 * @param size Size of the range.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_gpio_check_mmio_offset(gpio_state_t* gs, int svc_dom,
                                   unsigned long offset, unsigned long size)
{
  if (offset > HV_GPIO_MMIO_SIZE || offset + size > HV_GPIO_MMIO_SIZE)
    return GXIO_ERR_MMIO_ADDRESS;

  return 0;
}


/** Attach a set of pins to this service domain.
 * @param gs Driver state.
 * @param svc_dom Service domain number.
 * @param pin_mask Bitmap of pins to attach.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_gpio_attach(gpio_state_t* gs, int svc_dom, uint64_t pin_mask)
{
  if (pin_mask & ~(gs->input_pins | gs->output_pins | gs->output_od_pins))
    return GXIO_GPIO_ERR_PIN_UNAVAILABLE;

  for (int i = 1; i < HV_GPIO_NUM_SVC_DOM; i++)
    if (i != svc_dom && (pin_mask & gs->attached_pins[i]))
      return GXIO_GPIO_ERR_PIN_BUSY;

  cfg_wr(gs->shim_pos.word, gs->shim_chan, GPIO_MMIO_INIT_CTL, svc_dom << 1);
  cfg_wr(gs->shim_pos.word, gs->shim_chan, GPIO_MMIO_INIT_DAT, pin_mask);

  gs->attached_pins[svc_dom] = pin_mask;

  return 0;
}


/** Return the I/O direction registers.
 * @param gs Driver state.
 * @param svc_dom Service domain number.
 * @param disabled_pins Pointer to returned bitmap of disabled pins.
 * @param input_pins Pointer to returned bitmap of input pins.
 * @param output_pins Pointer to returned bitmap of output pins.
 * @param output_od_pins Pointer to returned bitmap of open-drain output pins.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_gpio_get_dir(gpio_state_t* gs, int svc_dom,
                         uint64_t* disabled_pins, uint64_t* input_pins,
                         uint64_t* output_pins, uint64_t* output_od_pins)
{
  uint64_t i_reg = cfg_rd(gs->shim_pos.word, gs->shim_chan, GPIO_PIN_DIR_I);
  uint64_t o_reg = cfg_rd(gs->shim_pos.word, gs->shim_chan, GPIO_PIN_DIR_O);

  //
  // Don't return any information on unattached pins.
  //
  i_reg &= gs->attached_pins[svc_dom];
  o_reg &= gs->attached_pins[svc_dom];

  *disabled_pins = ~i_reg & ~o_reg;
  *input_pins = i_reg & ~o_reg;
  *output_pins = ~i_reg & o_reg;
  *output_od_pins = i_reg & o_reg;

  return 0;
}


/** Modify the I/O direction registers.
 * @param gs Driver state.
 * @param svc_dom Service domain number.
 * @param disabled_pins Bitmap of disabled pins.
 * @param input_pins Bitmap of input pins.
 * @param output_pins Bitmap of output pins.
 * @param output_od_pins Bitmap of open-drain output pins.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_gpio_set_dir(gpio_state_t* gs, int svc_dom,
                         uint64_t disabled_pins, uint64_t input_pins,
                         uint64_t output_pins, uint64_t output_od_pins)
{
  //
  // If they're asking to configure any unattached pins, return an error.
  //
  if ((disabled_pins | input_pins | output_pins | output_od_pins) &
      ~gs->attached_pins[svc_dom])
    return GXIO_GPIO_ERR_PIN_UNATTACHED;

  //
  // If they're asking to improperly configure any pins, return an error.
  //
  if ((input_pins & ~gs->input_pins) ||
      (output_pins & ~gs->output_pins) ||
      (output_od_pins & ~gs->output_od_pins))
    return GXIO_GPIO_ERR_PIN_INVALID_MODE;

  //
  // Compute new register values and commit.
  //
  uint64_t i_reg = cfg_rd(gs->shim_pos.word, gs->shim_chan, GPIO_PIN_DIR_I);
  uint64_t o_reg = cfg_rd(gs->shim_pos.word, gs->shim_chan, GPIO_PIN_DIR_O);

  i_reg = (i_reg | input_pins | output_od_pins) &
          ~(disabled_pins | output_pins);
  o_reg = (o_reg | output_pins | output_od_pins) &
          ~(disabled_pins | input_pins);

  cfg_wr(gs->shim_pos.word, gs->shim_chan, GPIO_PIN_DIR_I, i_reg);
  cfg_wr(gs->shim_pos.word, gs->shim_chan, GPIO_PIN_DIR_O, o_reg);

  return 0;
}


/** Return the electrical characteristics for a pin.
 * @param gs Driver state.
 * @param svc_dom Service domain number.
 * @param pin Pin number.
 * @param pad_ctl Pointer to returned characteristics.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_gpio_get_elec(gpio_state_t* gs, int svc_dom, unsigned int pin,
                          GPIO_PAD_CONTROL_t* pad_ctl)
{
  if (!(gs->attached_pins[svc_dom] & (1ULL << pin)))
    return GXIO_GPIO_ERR_PIN_UNATTACHED;

  pad_ctl->word = cfg_rd(gs->shim_pos.word, gs->shim_chan,
                         GPIO_PAD_CONTROL__FIRST_WORD +
                         pin * sizeof (GPIO_PAD_CONTROL_t));

  return 0;
}


/** Modify the electrical characteristics for a pin.
 * @param gs Driver state.
 * @param svc_dom Service domain number.
 * @param pin Pin number.
 * @param pad_ctl Desired characteristics.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_gpio_set_elec(gpio_state_t* gs, int svc_dom, unsigned int pin,
                          GPIO_PAD_CONTROL_t pad_ctl)
{
  if (!(gs->attached_pins[svc_dom] & (1ULL << pin)))
    return GXIO_GPIO_ERR_PIN_UNATTACHED;

  //
  // Pull-up and pull-down on the same pin is probably a bad idea.
  //
  if (pad_ctl.pd && pad_ctl.pu)
    return GXIO_GPIO_ERR_PIN_INVALID_MODE;

  cfg_wr(gs->shim_pos.word, gs->shim_chan,
         GPIO_PAD_CONTROL__FIRST_WORD + pin * sizeof (GPIO_PAD_CONTROL_t),
         pad_ctl.word);

  return 0;
}


/** Return the clock mode for the shim.
 * @param gs Driver state.
 * @param svc_dom Service domain number.
 * @param gclk_mode Pointer to returned clock mode.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_gpio_get_gclk_mode(gpio_state_t* gs, int svc_dom,
                               GPIO_GCLK_MODE_t* gclk_mode)
{
  gclk_mode->word = cfg_rd(gs->shim_pos.word, gs->shim_chan, GPIO_GCLK_MODE);

  return 0;
}


/** Modify the clock mode for the shim.
 * @param gs Driver state.
 * @param svc_dom Service domain number.
 * @param gclk_mode Desired clock mode.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_gpio_set_gclk_mode(gpio_state_t* gs, int svc_dom,
                               GPIO_GCLK_MODE_t gclk_mode)
{
  //
  // Don't allow the clock speed to be too low; otherwise, it could
  // take a very long and uninterruptible time to do an mf upon context
  // switch.
  //
  if (gclk_mode.divide > 1000)
    return GXIO_ERR_INVAL;

  cfg_wr(gs->shim_pos.word, gs->shim_chan, GPIO_GCLK_MODE, gclk_mode.word);

  return 0;
}


/** Configure shim interrupts.
 * @param gs Driver state.
 * @param svc_dom Service domain number.
 * @param inter_x X coordinate of target tile.
 * @param inter_y Y coordinate of target tile.
 * @param inter_ipi Target IPI register set, typically the PL.
 * @param inter_event Target IPI event number.
 * @param on_assert Bitmap of pins whose transition from deasserted to
 *  asserted should cause an interrupt.
 * @param on_deassert Bitmap of pins whose transition from asserted to
 *  deasserted should cause an interrupt.
 * @return A non-negative interrupt cookie on success, or a negative error
 *  code.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_gpio_cfg_interrupt(gpio_state_t* gs, int svc_dom,
                               int inter_x, int inter_y,
                               int inter_ipi, int inter_event,
                               uint64_t on_assert, uint64_t on_deassert)
{
  if ((on_assert | on_deassert) & ~gs->attached_pins[svc_dom])
    return GXIO_GPIO_ERR_PIN_UNATTACHED;

  GPIO_INT_BIND_t intbind = {{
    .enable = 1,
    .mode = 0,
    .tileid = DRV_COORDS_TO_TILE_ID(inter_x, inter_y),
    .int_num = inter_ipi,
    .evt_num = inter_event,
  }};

  if (inter_event < 0)
    intbind.enable = 0;

  intbind.vec_sel = GPIO_INT_BIND__VEC_SEL_VAL_PIN_ASSERTION_INTS;

  while (on_assert)
  {
    intbind.bind_sel = __builtin_ctzll(on_assert);
    cfg_wr(gs->shim_pos.word, gs->shim_chan, GPIO_INT_BIND, intbind.word);
    on_assert &= on_assert - 1;
  }

  intbind.vec_sel = GPIO_INT_BIND__VEC_SEL_VAL_PIN_DEASSERTION_INTS;

  while (on_deassert)
  {
    intbind.bind_sel = __builtin_ctzll(on_deassert);
    cfg_wr(gs->shim_pos.word, gs->shim_chan, GPIO_INT_BIND, intbind.word);
    on_deassert &= on_deassert - 1;
  }

  return 0;
}


/** Configure shim interrupts for a pollable FD, and remember its
 *  characteristics so that the interrupts can be reset later.
 * @param gs Driver state.
 * @param svc_dom Service domain number.
 * @param inter_x X coordinate of target tile.
 * @param inter_y Y coordinate of target tile.
 * @param inter_ipi Target IPI register set, typically the PL.
 * @param inter_event Target IPI event number.
 * @param on_assert Bitmap of pins whose transition from deasserted to
 *  asserted should cause an interrupt.
 * @param on_deassert Bitmap of pins whose transition from asserted to
 *  deasserted should cause an interrupt.
 * @return A non-negative interrupt cookie on success, or a negative error
 *  code.
 */
int
handle_gxio_gpio_cfg_pollfd(gpio_state_t* gs, int svc_dom,
                            int inter_x, int inter_y,
                            int inter_ipi, int inter_event,
                            uint64_t on_assert, uint64_t on_deassert)
{
  //
  // We use no interrupt bits set as a flag to mean "free interrupt table
  // entry", so we can't allow that.  Plus, it's pointless.
  //
  if (!(on_assert | on_deassert))
    return GXIO_ERR_INVAL;

  //
  // Find a free entry in this domain's pollable interrupt table.
  //
  int cookie = -1;

  for (int i = 0; i < GPIO_POLLFD_INTR_PER_SD; i++)
    if (!(gs->pollfd_intrs[svc_dom][i].on_assert |
          gs->pollfd_intrs[svc_dom][i].on_deassert))
    {
      cookie = i;
      break;
    }

  if (cookie < 0)
    return GXIO_ERR_BUSY;

  //
  // Configure the interrupt.  Note that this routine already checks for
  // attempts to use unattached pins, so we don't need to.
  //
  int err = handle_gxio_gpio_cfg_interrupt(gs, svc_dom, inter_x, inter_y,
                                           inter_ipi, inter_event,
                                           on_assert, on_deassert);
  if (err)
    return err;

  //
  // Remember the interrupt properties for future arm/close calls.
  //
  gs->pollfd_intrs[svc_dom][cookie].on_assert = on_assert;
  gs->pollfd_intrs[svc_dom][cookie].on_deassert = on_deassert;

  return cookie;
}


/** Arm GPIO interrupts.
 * @param gs Driver state.
 * @param svc_dom Service domain.
 * @param cookie Interrupt cookie returned from
 *  handle_gxio_gpio_cfg_pollfd.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_gpio_arm_pollfd(gpio_state_t* gs, int svc_dom, int cookie)
{
  //
  // Make sure the cookie is in-range and the table entry isn't free.
  //
  if (cookie < 0 || cookie >= GPIO_POLLFD_INTR_PER_SD ||
      !(gs->pollfd_intrs[svc_dom][cookie].on_assert |
        gs->pollfd_intrs[svc_dom][cookie].on_deassert))
    return GXIO_ERR_INVAL;

  //
  // Clear the relevant interrupt status bits to enable new interrupts.
  //
  cfg_wr(gs->shim_pos.word, gs->shim_chan, GPIO_INT_VEC0_W1TC,
         gs->pollfd_intrs[svc_dom][cookie].on_assert);
  cfg_wr(gs->shim_pos.word, gs->shim_chan, GPIO_INT_VEC1_W1TC,
         gs->pollfd_intrs[svc_dom][cookie].on_deassert);

  return 0;
}


/** Deconfigure GPIO interrupts.
 * @param gs Driver state.
 * @param svc_dom Service domain.
 * @param cookie Interrupt cookie returned from
 *  handle_gxio_gpio_cfg_pollfd.
 * @return Zero on success, or a negative error code.
 */
int
handle_gxio_gpio_close_pollfd(gpio_state_t* gs, int svc_dom, int cookie)
{
  //
  // Make sure the cookie is in-range and the table entry isn't free.
  //
  if (cookie < 0 || cookie >= GPIO_POLLFD_INTR_PER_SD ||
      !(gs->pollfd_intrs[svc_dom][cookie].on_assert |
        gs->pollfd_intrs[svc_dom][cookie].on_deassert))
    return GXIO_ERR_INVAL;

  //
  // Configure the interrupt with an event of -1 to disable it.
  //
  handle_gxio_gpio_cfg_interrupt(gs, svc_dom, 0, 0, 0, -1,
                                 gs->pollfd_intrs[svc_dom][cookie].on_assert,
                                 gs->pollfd_intrs[svc_dom][cookie].on_deassert);

  //
  // Free the interrupt table entry.
  //
  gs->pollfd_intrs[svc_dom][cookie].on_assert = 0;
  gs->pollfd_intrs[svc_dom][cookie].on_deassert = 0;

  return 0;
}


//
// Note that the pinset lookup routines have sub-optimal performance;
// looking up a name and then retrieving the associated pin masks traverses
// the BIB twice, and enumerating all pinsets is O(n^2) since we traverse
// the BIB up to the target set on every call.  You could put in a cache of
// the last-used values to fix this, but given how infrequently these
// routines are used, it's not worth the trouble.
//

/** Translate a pinset name to an index.
 * @param gs Driver state.
 * @param svc_dom Service domain.
 * @param name Pointer to name to look up.
 * @param name_size Name length in bytes.
 * @return Index on success, or a negative error code.
 */
int
handle_gxio_gpio_get_pinset_aux(gpio_state_t* gs, int svc_dom, void* name,
                                size_t name_size)
{
  bi_ptr_t bp;
  uint32_t desc;
  int bibpos = 0;
  int idx = 0;
  const char* cname = name;

  while ((desc = bi_getparam(BI_TYPE_GPIO_NAME, -1, &bp, &bibpos)) != BI_NULL)
  {
    struct bi_gpio_name* gn = bp;

    int bib_name_len = (BI_WDS(desc) * sizeof (uint32_t)) - sizeof (*gn);
    bib_name_len = min(strnlen(gn->name, bib_name_len),
                       GXIO_GPIO_PINSET_NAME_LEN - 1);
    if (name_size == bib_name_len && !strncmp(cname, gn->name, name_size))
      return idx;

    idx++;
  }

  return GXIO_ERR_NO_DEVICE;
}


/** Translate a pinset index to a name and pin masks.
 * @param gs Driver state.
 * @param svc_dom Service domain.
 * @param idx Index of desired set, starting at 0.
 * @param input_pins Pointer to returned input pin mask.
 * @param output_pins Pointer to returned output pin mask.
 * @param output_od_pins Pointer to returned open-drain output pin mask.
 * @param inverted_pins Pointer to returned inverted pin mask.
 * @param name Pointer to buffer in which to return name.
 * @param name_size Name buffer length in bytes.
 * @return 0 on success, or a negative error code.
 */
int
handle_gxio_gpio_enumerate_pinset_aux(gpio_state_t* gs, int svc_dom,
                                      unsigned int idx,
                                      uint64_t *input_pins,
                                      uint64_t *output_pins,
                                      uint64_t *output_od_pins,
                                      uint64_t *inverted_pins,
                                      void* name, size_t name_size)
{
  bi_ptr_t bp;
  uint32_t desc = BI_NULL;
  int bibpos = 0;
  char* cname = name;

  for (int i = 0; i <= idx; i++)
    if ((desc = bi_getparam(BI_TYPE_GPIO_NAME, -1, &bp, &bibpos)) == BI_NULL)
      return GXIO_ERR_NO_DEVICE;

  struct bi_gpio_name* gn = bp;

  *input_pins = gn->input;
  *output_pins = gn->output;
  *output_od_pins = gn->output_od;
  *inverted_pins = gn->invert;

  int bib_name_len = (BI_WDS(desc) * sizeof (uint32_t)) - sizeof (*gn);
  bib_name_len = min(strnlen(gn->name, bib_name_len),
                     GXIO_GPIO_PINSET_NAME_LEN - 1);
  strncpy(cname, gn->name, min(bib_name_len, name_size));
  if (bib_name_len < name_size)
    cname[bib_name_len] = '\0';

  return 0;
}


/** GPIO driver probe routine. */
int
gpiodrv_probe(const char* drvname, int instance, pos_t tile,
              const struct dev_info* info)
{
  if (instance >= MAX_GPIOS)
    return HV_ENODEV;

  gpio_shims[instance] = info;
  return 0;
}


/** GPIO driver initialization routine. */
static int
gpiodrv_init(const char* drvname, void** statepp, int instance, int tileno,
             pos_t tile, const struct dev_info* info, const char* args)
{
  gpio_state_t* gs;

  if (instance >= MAX_GPIOS)
    return HV_ENODEV;

  drv_spin_lock(&gpio_alloc_lock);
  gs = gpio_state[instance];
  if (gs == NULL)
  {
    gs = drv_shared_state_zalloc(sizeof(*gs), 0);
    if (gs == NULL)
    {
      drv_spin_unlock(&gpio_alloc_lock);
      return HV_ENOMEM;
    }
    gpio_state[instance] = gs;
    gs->shim_pos = info->idn_ports[0];
    gs->shim_chan = info->channel;

    //
    // Get the pin constraints from the BIB.  By default, we don't export
    // access to any pins.  We OR together all of the pin_cfg entries so
    // that GPIO access can be added via AIBs.
    //
    gs->input_pins = 0;
    gs->output_pins = 0;
    gs->output_od_pins = 0;

    bi_ptr_t bp;
    int bibpos = 0;
    while (bi_getparam(BI_TYPE_GPIO_PIN_CFG, -1, &bp, &bibpos) != BI_NULL)
    {
      struct bi_gpio_pin_cfg* gpc = bp;

      gs->input_pins |= gpc->input;
      gs->output_pins |= gpc->output;
      gs->output_od_pins |= gpc->output_od;
    }
  }
  drv_spin_unlock(&gpio_alloc_lock);

  *statepp = gs;

  return 0;
}


/** GPIO driver open routine. */
static int
gpiodrv_open(int devhdl, void* statep, const char* suffix, uint32_t flags,
             pos_t tile)
{
  DEVICE_TRACE("gpio_open: devhdl %#x suffix \"%s\" flags %#x "
               "tile %#x\n", devhdl, suffix, flags, tile.word);

  gpio_state_t* gs = statep;
  int svc_dom = -1;

  if (!strcmp(suffix, "/iorpc"))
  {
    drv_spin_lock(&gs->lock);
    svc_dom = gpio_reserve_svc_dom(gs);
    drv_spin_unlock(&gs->lock);

    if (svc_dom < 0)
      return svc_dom;

    //
    // Remove access to all pins and set the PL.
    //
    cfg_wr(gs->shim_pos.word, gs->shim_chan, GPIO_MMIO_INIT_CTL, svc_dom << 1);
    cfg_wr(gs->shim_pos.word, gs->shim_chan, GPIO_MMIO_INIT_DAT, 0);
    cfg_wr(gs->shim_pos.word, gs->shim_chan, GPIO_MMIO_INIT_DAT, 0);
    gs->attached_pins[svc_dom] = 0;

    /* Permit MMIO access */
    int err = drv_permit_mmio_access(gs->shim_pos,
                                     HV_GPIO_MMIO_OFFSET(svc_dom),
                                     HV_GPIO_MMIO_SIZE, 0);

    if (err != 0)
    {
      drv_spin_lock(&gs->lock);
      gpio_unreserve_svc_dom(gs, svc_dom);
      drv_spin_unlock(&gs->lock);
      return err;
    }

    return svc_dom;
  }

  return HV_ENODEV;
}


/** GPIO driver close routine. */
static int
gpiodrv_close(int devhdl, void* statep, pos_t tile)
{
  DEVICE_TRACE("gpio_close: devhdl %#x\n", devhdl);

  gpio_state_t* gs = statep;
  unsigned int svc_dom = DRV_HDL2BITS(devhdl);

  drv_spin_lock(&gs->lock);

  if (!gpio_svc_dom_is_open(gs, svc_dom))
  {
    drv_spin_unlock(&gs->lock);
    return GXIO_ERR_INVAL_SVC_DOM;
  }

  //
  // Clear any interrupts that may have been requested.
  //
  GPIO_INT_BIND_t intbind = {{ .enable = 0 }};

  uint64_t pins_to_clear = gs->attached_pins[svc_dom];

  while (pins_to_clear)
  {
    intbind.bind_sel = __builtin_ctzll(pins_to_clear);

    intbind.vec_sel = GPIO_INT_BIND__VEC_SEL_VAL_PIN_ASSERTION_INTS;
    cfg_wr(gs->shim_pos.word, gs->shim_chan, GPIO_INT_BIND, intbind.word);

    intbind.vec_sel = GPIO_INT_BIND__VEC_SEL_VAL_PIN_DEASSERTION_INTS;
    cfg_wr(gs->shim_pos.word, gs->shim_chan, GPIO_INT_BIND, intbind.word);

    pins_to_clear &= pins_to_clear - 1;
  }

  //
  // Forget about any pollfds.  We already disabled all of the interrupts,
  // so we just clear the status bits.
  //
  for (int i = 0; i < GPIO_POLLFD_INTR_PER_SD; i++)
  {
    gs->pollfd_intrs[svc_dom][i].on_assert = 0;
    gs->pollfd_intrs[svc_dom][i].on_deassert = 0;
  }

  //
  // We're no longer attached to any pins.
  //
  gs->attached_pins[svc_dom] = 0;

  //
  // Can't hold a lock while calling drv_deny_mmio_access.
  //
  drv_spin_unlock(&gs->lock);

  //
  // Remove access to the registers.
  //
  drv_deny_mmio_access(gs->shim_pos,
                       HV_GPIO_MMIO_OFFSET(svc_dom),
                       HV_GPIO_MMIO_SIZE, 0);

  //
  // Make the service domain available for another user.
  //
  drv_spin_lock(&gs->lock);
  gpio_unreserve_svc_dom(gs, svc_dom);
  drv_spin_unlock(&gs->lock);

  return 0;
}


/** GPIO driver close_all routine. */
static int
gpiodrv_close_all(int dev_idx, void *statep)
{
  gpio_state_t* gs = statep;

  DEVICE_TRACE("gpio_close_all: dev_idx %d\n", dev_idx);

  for (int svc_dom_mask = gs->reserved_svc_dom_bitmask; svc_dom_mask;
       svc_dom_mask &= svc_dom_mask - 1)
  {
    int svc_dom = __builtin_ctz(svc_dom_mask);
    int devhdl = MK_HDL(dev_idx, svc_dom);

    gpiodrv_close(devhdl, statep, my_pos);
  }

  return  0;
}


/** GPIO driver read routine. */
static int
gpiodrv_pread(int devhdl, void* statep, uint32_t flags, char* va,
              uint32_t len, uint64_t offset, pos_t tile)
{
  gpio_state_t* gs = statep;

  DEVICE_TRACE("gpio_pread: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  char buf[1024];
  unsigned int svc_dom = DRV_HDL2BITS(devhdl);

  if (len > sizeof(buf))
    return HV_EINVAL;

  drv_spin_lock(&gs->lock);

  if (!gpio_svc_dom_is_open(gs, svc_dom))
  {
    drv_spin_unlock(&gs->lock);
    return GXIO_ERR_INVAL_SVC_DOM;
  }

  int result = dispatch_gxio_gpio_read(offset, buf, len, gs, svc_dom);

  drv_spin_unlock(&gs->lock);

  if (drv_copy_to_client(va, buf, len, flags))
    result = HV_EFAULT;

  return result;
}


/** GPIO driver write routine. */
static int
gpiodrv_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
               uint32_t len, uint64_t offset, pos_t tile)
{
  gpio_state_t* gs = statep;

  DEVICE_TRACE("gpio_pwrite: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  char buf[1024];
  unsigned int svc_dom = DRV_HDL2BITS(devhdl);

  if (len > sizeof(buf))
    return HV_EINVAL;

  if (drv_copy_from_client(buf, va, len, flags))
    return HV_EFAULT;

  drv_spin_lock(&gs->lock);

  if (!gpio_svc_dom_is_open(gs, svc_dom))
  {
    drv_spin_unlock(&gs->lock);
    return GXIO_ERR_INVAL_SVC_DOM;
  }

  int result = dispatch_gxio_gpio_write(offset, buf, len, gs, svc_dom);

  drv_spin_unlock(&gs->lock);

  return result;
}


/** GPIO driver operations vector. */
static struct drv_ops gpio_ops = {
  .probe       = gpiodrv_probe,
  .init        = gpiodrv_init,
  .open        = gpiodrv_open,
  .close       = gpiodrv_close,
  .close_all   = gpiodrv_close_all,
  .pread       = gpiodrv_pread,
  .pwrite      = gpiodrv_pwrite
};


/** Add a new "driver" entry. */
static const __DRIVER_ATTR driver_t driver_gpio = {
  .shim_type  = GPIO_DEV_INFO__TYPE_VAL_GPIO,
  .name       = "gpio",
  .desc       = "General Purpose I/O",
  .ops        = &gpio_ops,
};
