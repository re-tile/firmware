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
 * TRIO driver.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "board_info.h"
#include "cfg.h"
#include "debug.h"
#include "devices.h"
#include "drvintf.h"
#include "hv.h"
#include "hw_config.h"
#include "mapping.h"
#include "mshim_acc.h"

#include "trio.h"

#include "trio_rpc_dispatch.h"

#include <arch/trio_pcie_ep.h>
#include <arch/trio_shm.h>
#include <arch/serdes.h>
#include <arch/sim.h>

#include <hv/pagesize.h>

#include <arch/trio_pcie_rc.h>

/** A convenient macro for printing warnings in standard format. */
#define WARN(...) tprintf("hv_warning: trio: " __VA_ARGS__)

/** Tracing infrastructure for debug. */
#if 0
#define TRACE(...) tprintf("trio: " __VA_ARGS__)
#else
#define TRACE(...)
#endif

/** Some APIs require a client number, but I don't know how to get
    one, so for now just use a static one. */
#define CLIENTNO 0

/** The largest RPC buffer we're willing to put on the stack. */
#define MAX_STACK_BYTES 4096

/** Lock used to make sure that only one tile allocates shared state. */
static spinlock_t trio_alloc_lock _SHARED = SPINLOCK_INIT;

/** Address of the shared state object. */
trio_state_t* trio_state[MAX_PCIES] _SHARED = { 0 };

/** Structure to keep the interrupt states. */
static trio_intr_t trio_intr[TILEGX_TRIO_PCIES];

/** Flag indicating if chip-wide TRIO handler has been registered. */
static int trio_cleanup_handler_registered _SHARED;

/** Map fence operations. */
static void 
map_fence(trio_state_t *ts)
{
  if (!sim_is_simulator())
  {
    // Setup the FENCE bit.
    TRIO_MAP_MEM_CTL_t ctl;

    ctl.word = cfg_rd(ts->shim_pos.word, 0, TRIO_MAP_MEM_CTL);
    ctl.fence = 1;
    cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_MEM_CTL, ctl.word);

    __insn_mf();

    // 
    // Wait until no new transactions arrive, no TLB miss and all older writes
    // are completed from this map mem or SQ region.
    //
    do
    {
      ctl.word = cfg_rd(ts->shim_pos.word, 0, TRIO_MAP_MEM_CTL);
    } while (ctl.fence);

    TRIO_MAP_DIAG_FSM_STATE_t state;
    do
    {
      state.word = cfg_rd(ts->shim_pos.word, 0, TRIO_MAP_DIAG_FSM_STATE);
    } while (state.rdq);

    // Ensure all reads have completed.
    TRIO_PUSH_DMA_CTL_t push_dma_ctl;

    push_dma_ctl.word = cfg_rd(ts->shim_pos.word, 0, TRIO_PUSH_DMA_CTL);
    push_dma_ctl.fence = 1;
    cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_CTL, push_dma_ctl.word);

    __insn_mf();

    do
    {
      push_dma_ctl.word = cfg_rd(ts->shim_pos.word, 0, TRIO_PUSH_DMA_CTL);
    } while (push_dma_ctl.fence);
  }
}

/** Disable MAC interrupts. */
static void
disable_mac_intrs(trio_state_t* ts)
{
  TRIO_INT_BIND_t binding_setup;

  binding_setup.word = 0;
  binding_setup.vec_sel = TRIO_INT_BIND__VEC_SEL_VAL_MAC;

  for (int mac = 0; mac < TILEGX_TRIO_PCIES; mac++)
  {
    for (int intr = 0; intr < 32; intr++)
    {
      binding_setup.bind_sel = mac * 32 + intr;
      cfg_wr(ts->shim_pos.word, 0, TRIO_INT_BIND, binding_setup.word);
    }
  }
}

/** Invalidate all IOTLB entries with a given ASID. */
static void
inv_iotlb(trio_state_t* ts, unsigned int asid)
{
  for (int entry = 0; entry < TRIO_NUM_TLBS_PER_ASID; entry++)
  {
    TRIO_TLB_TABLE_t table = {{
        .entry = entry,
        .asid = asid,
      }};

    TRIO_TLB_ENTRY_ATTR_t attr = {{
        .vld = 0,
      }};
    cfg_wr(ts->shim_pos.word, 0,
           TRIO_TLB_ENTRY_ATTR__FIRST_WORD + table.word, attr.word);
  }
}

/** Flush the TRIO micro-TLBs. */
static void
flush_micro_tlbs(trio_state_t* ts)
{
  if (!sim_is_simulator())
  {
    TRIO_TLB_CTL_t tlb_ctl = {{ .mtlb_flush = 1 }};

    cfg_wr(ts->shim_pos.word, 0, TRIO_TLB_CTL, tlb_ctl.word);
  }
}

/** Flush a Pull DMA ring. */
static void
flush_pull_dma(trio_state_t* ts, uint32_t ring)
{
  //
  // First, prevent additional descriptors from being fetched and processed.
  //
  TRIO_PULL_DMA_DM_INIT_CTL_t dm_ctl = {{
      .idx = ring,
      .struct_sel = TRIO_PULL_DMA_DM_INIT_CTL__STRUCT_SEL_VAL_SETUP,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_PULL_DMA_DM_INIT_CTL, dm_ctl.word);

  TRIO_PULL_DMA_DM_INIT_DAT_SETUP_t setup = {{
      .freeze = 1,
      .flush = 1,
      .stall = 1,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_PULL_DMA_DM_INIT_DAT, setup.word);

  __insn_mf();

  //
  // Wait untill all outstanding requests have been completed.
  //
  if (!sim_is_simulator())
  {
    TRIO_PULL_DMA_CTL_t pull_dma_ctl;
    do
    {
      pull_dma_ctl.word = cfg_rd(ts->shim_pos.word, 0, TRIO_PULL_DMA_CTL);
    } while (pull_dma_ctl.flush_pnd);
  }

  dm_ctl.struct_sel = TRIO_PULL_DMA_DM_INIT_CTL__STRUCT_SEL_VAL_DESC_STATE0;
  cfg_wr(ts->shim_pos.word, 0, TRIO_PULL_DMA_DM_INIT_CTL, dm_ctl.word);
  cfg_wr(ts->shim_pos.word, 0, TRIO_PULL_DMA_DM_INIT_DAT, 1);
  
  dm_ctl.struct_sel = TRIO_PULL_DMA_DM_INIT_CTL__STRUCT_SEL_VAL_DESC_STATE1;
  cfg_wr(ts->shim_pos.word, 0, TRIO_PULL_DMA_DM_INIT_CTL, dm_ctl.word);
  cfg_wr(ts->shim_pos.word, 0, TRIO_PULL_DMA_DM_INIT_DAT, 0);
}

/** Flush a Push DMA ring. */
static void
flush_push_dma(trio_state_t* ts, uint32_t ring)
{
  //
  // First, prevent additional descriptors from being fetched and processed.
  // This will also flush already-fetched descriptors and buffer data.
  //
  TRIO_PUSH_DMA_DM_INIT_CTL_t dm_ctl = {{
      .idx = ring,
      .struct_sel = TRIO_PUSH_DMA_DM_INIT_CTL__STRUCT_SEL_VAL_SETUP,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_DM_INIT_CTL, dm_ctl.word);

  TRIO_PUSH_DMA_DM_INIT_DAT_SETUP_t setup = {{
      .freeze = 1,
      .flush = 1,
      .stall = 1,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_DM_INIT_DAT, setup.word);

  __insn_mf();

  //
  // Wait untill all descriptors have been flushed.
  //
  if (!sim_is_simulator())
  {
    TRIO_PUSH_DMA_CTL_t push_dma_ctl;
    do
    {
      push_dma_ctl.word = cfg_rd(ts->shim_pos.word, 0, TRIO_PUSH_DMA_CTL);
    } while (push_dma_ctl.flush_pnd);

    //
    // Initiate a coherence fence on outstanding push DMA data reads.
    //
    push_dma_ctl.fence = 1;
    cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_CTL, push_dma_ctl.word);

    __insn_mf();

    //
    // Wait for all outstanding requests to complete.
    //
    do
    {
      push_dma_ctl.word = cfg_rd(ts->shim_pos.word, 0, TRIO_PUSH_DMA_CTL);
    } while (push_dma_ctl.fence);

    //
    // Insure that buffer flush has completed.
    //
    do
    {
      push_dma_ctl.word = cfg_rd(ts->shim_pos.word, 0, TRIO_PUSH_DMA_CTL);
    } while (push_dma_ctl.flush_pnd);
  }

  //
  // Zero out the descriptor ring's Current Head/GNUM.
  //
  dm_ctl.struct_sel = TRIO_PUSH_DMA_DM_INIT_CTL__STRUCT_SEL_VAL_HEAD,
  cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_DM_INIT_CTL, dm_ctl.word);
  cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_DM_INIT_DAT, 0);

  dm_ctl.struct_sel = TRIO_PUSH_DMA_DM_INIT_CTL__STRUCT_SEL_VAL_DESC_STATE0;
  cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_DM_INIT_CTL, dm_ctl.word);
  cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_DM_INIT_DAT, 1);

  dm_ctl.struct_sel = TRIO_PUSH_DMA_DM_INIT_CTL__STRUCT_SEL_VAL_DESC_STATE1;
  cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_DM_INIT_CTL, dm_ctl.word);
  cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_DM_INIT_DAT, 0);

  //
  // Flush any outstanding interrupts to prevent interrupts from
  // the old process from being delivered to the new process.
  //
  cfg_rd(ts->shim_pos.word, 0, TRIO_INT_VEC0_RTC);
  cfg_rd(ts->shim_pos.word, 0, TRIO_INT_VEC1_RTC);
  cfg_rd(ts->shim_pos.word, 0, TRIO_INT_VEC2_RTC);
  cfg_rd(ts->shim_pos.word, 0, TRIO_INT_VEC3_RTC);

  __insn_mf();
}

/** Flush a SQ region. */
static void
flush_sq(trio_state_t *ts, uint32_t queue)
{
  if (sim_is_simulator())
    return;

  // There's a four word stride for each scatter queue register set.
  size_t reg_offset = 4 * sizeof(uint64_t) * queue;

  // Disable the SQ region's intr binding if it has one.
  if (ts->resources.sq_intr_mask & (1 << queue))
  {
    TRIO_INT_BIND_t binding_setup =
      {{
        .bind_sel = queue,
        .vec_sel = TRIO_INT_BIND__VEC_SEL_VAL_MAP_SQ,
        .enable = 0,
      }};

    cfg_wr(ts->shim_pos.word, 0, TRIO_INT_BIND, binding_setup.word);

    ts->resources.sq_intr_mask &= ~(1 << queue);
  }

  // Dequeue all the left-over descriptors in the SQ FIFO.
  TRIO_MAP_SQ_REGION_READ_VAL_t sq_read;
  sq_read.word = cfg_rd(ts->shim_pos.word, 0, HV_TRIO_SQ_OFFSET(queue));

  TRIO_MAP_SQ_CTL_t sq_ctl = {{
      .sq_sel = queue,
      .pop = 1,
    }};

  for (int i = 0; i < sq_read.curr_count; i++)
    cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_SQ_CTL, sq_ctl.word);

  TRIO_MAP_SQ_SETUP_t setup;
  setup.word = cfg_rd(ts->shim_pos.word, 0, 
                      TRIO_MAP_SQ_SETUP__FIRST_WORD + reg_offset);
  setup.mac_ena = 0;
  cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_SQ_SETUP__FIRST_WORD + reg_offset,
         setup.word);

  __insn_mf();

  map_fence(ts);
}

/** Flush a map mem region. */
static void
flush_map_mem(trio_state_t *ts, uint32_t map)
{
  // There's a four word stride for each map-mem register set.
  size_t reg_offset = 4 * sizeof(uint64_t) * map;

  // Disable this Map Memory Region's interrupt binding
  // if it is enabled.
  TRIO_MAP_MEM_SETUP_t setup;

  setup.word = cfg_rd(ts->shim_pos.word, 0,
                      TRIO_MAP_MEM_SETUP__FIRST_WORD + reg_offset);
  if (setup.int_ena)
  {
    TRIO_INT_BIND_t binding_setup =
      {{
        .bind_sel = map,
        .vec_sel = TRIO_INT_BIND__VEC_SEL_VAL_MAP_MEM,
        .enable = 0,
      }};

    cfg_wr(ts->shim_pos.word, 0, TRIO_INT_BIND, binding_setup.word);
  }

  setup.mac_ena = 0,
  cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_MEM_SETUP__FIRST_WORD + reg_offset,
         setup.word);

  __insn_mf();

  map_fence(ts);
}

/** A helper routine for validating the service domain bits in a
    client device file handle. */
static bool
is_open_svc_dom(unsigned int index, trio_state_t* ts)
{
  return ((index < TRIO_NUM_SVC_DOM) &&
          !(ts->svc_dom_avail_mask & (1ull << index)));
}

/** trio delayed interrupt handler routine. */
static void
trio_delayed_intr(void* intarg, void* msg, int len)
{
  trio_intr_t* trio_intr = intarg;
  trio_state_t* ts = trio_intr->ts;
  unsigned int mac = trio_intr->mac;

  TRIO_PCIE_INTFC_PORT_STATUS_t port_status;
  size_t reg_offset;

  reg_offset =
    (TRIO_PCIE_INTFC_PORT_STATUS <<
    TRIO_CFG_REGION_ADDR__REG_SHIFT) |
    (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE
    << TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
    (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

  port_status.word = cfg_rd(ts->shim_pos.word, 0, reg_offset);

  if (!port_status.dl_up)
    handle_gxio_trio_force_ep_link_up(ts, 0, mac);

  // Clear the intr status.
  reg_offset = (TRIO_PCIE_INTFC_MAC_INT_STS <<
    TRIO_CFG_REGION_ADDR__REG_SHIFT) |
    (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
    TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
    (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

  cfg_wr(ts->shim_pos.word, 0, reg_offset,
         TRIO_PCIE_INTFC_MAC_INT_STS__DL_STATE_RMASK <<
         TRIO_PCIE_INTFC_MAC_INT_STS__DL_STATE_SHIFT);
}

/*
 * Reset registers TRIO_MAP_RSH_BASE and TRIO_PCIE_INTFC_RX_BAR0_ADDR_MASK.
 * This needs to be done when the host is going down prior to a reboot.
 * Otherwise, these two registers will retain previous values that don't
 * match the new BAR0 address that is assigned to the Gx PCIe ports in the
 * new Linux boot session, causing host MMIO access to RSHIM to fail.
 */
static void
trio_reset_bar_mask(trio_state_t *ts)
{
  size_t reg_offset;

  cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_RSH_BASE, 0);

  for (int mac = 0; mac < TILEGX_TRIO_PCIES; mac++)
  {
    reg_offset =
      (TRIO_PCIE_INTFC_RX_BAR0_ADDR_MASK << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
      (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE
      << TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
      (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

    cfg_wr(ts->shim_pos.word, 0, reg_offset, 0x7fffff);
  }
}

/*
 * Reset register TRIO_PCIE_EP_SUBSYS_ID_SUBSYS_VEN_ID.
 * This needs to be done when the host is going down prior to a reboot,
 * because the Linux EP port driver sets it to signal chip readiness.
 * This register is cleared whenever the PCIe MAC is reset but needs to be
 * reset explicitly when trio_quiesce is called.
 */
static void
trio_reset_subsys_id(trio_state_t *ts)
{
  size_t reg_offset;

  for (int mac = 0; mac < TILEGX_TRIO_PCIES; mac++)
  {
    reg_offset =
      (TRIO_PCIE_EP_SUBSYS_ID_SUBSYS_VEN_ID <<
       TRIO_CFG_REGION_ADDR__REG_SHIFT) |
      (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_STANDARD <<
       TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
      (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

    cfg_wr32(ts->shim_pos.word, 0, reg_offset, 0);
  }
}

/**
 * Quiesce the TRIO prior to the chip reset.
 * This is based largely on trio_close(), but without the calls to
 * drv_deny_mmio_access() which would make HV unhappy if called in
 * interrupt handler. This should be safe considering the low probability
 * of some tiles configuring the TRIO right before chip reset.
 */
static void
trio_quiesce(trio_state_t *ts)
{
  int i;

  spin_lock(&ts->lock);

  if (ts->quiesced)
  {
    spin_unlock(&ts->lock);
    return;
  }

  for (i = 0; i < TRIO_NUM_MAP_MEM_REGIONS; i++)
  {
    flush_map_mem(ts, i);
  }

  for (i = 0; i < TRIO_NUM_MAP_SQ_REGIONS; i++)
  {
    flush_sq(ts, i);
  }

  for (i = 0; i < TRIO_NUM_TPIO_REGIONS; i++)
  {
    // Disable this PIO region.
    cfg_wr(ts->shim_pos.word, 0, TRIO_TILE_PIO_REGION_SETUP__FIRST_WORD +
      sizeof(TRIO_TILE_PIO_REGION_SETUP_t) * i, 0);
  }

  for (i = 0; i < TRIO_NUM_PUSH_DMA_RINGS; i++)
  {
    flush_push_dma(ts, i);
  }

  for (i = 0; i < TRIO_NUM_PULL_DMA_RINGS; i++)
  {
    flush_pull_dma(ts, i);
  }

  for (i = 0; i < TRIO_NUM_ASIDS; i++)
  {
    // Invalidate all IOTLB entries with this ASID.
    inv_iotlb(ts, i);
  }

  // Flush any cached TLB entries.
  flush_micro_tlbs(ts);

  disable_mac_intrs(ts);

  trio_reset_bar_mask(ts);

  trio_reset_subsys_id(ts);

  ts->quiesced = 1;

  spin_unlock(&ts->lock);

  return;
}

/** trio delayed interrupt handler routine for resource cleanup. */
static void
trio_cleanup_delayed_intr(void* intarg, void* msg, int len)
{
  trio_state_t **trio_states = intarg;

  for (int trio = 0; trio < MAX_PCIES; trio++)
  {
    trio_state_t *ts = trio_states[trio];

    if (ts == NULL)
      continue;

    trio_quiesce(ts);
  }
}

/** TRIO driver init routine. */
static int
trio_init(const char* drvname, void** statepp, int instance,
           int tileno, pos_t tile, const struct dev_info* info,
           const char* args)
{
  if (instance >= MAX_PCIES)
    return (HV_ENODEV);
  trio_state_t* ts;
  spin_lock(&trio_alloc_lock);
  ts = trio_state[instance];

  // First core to call trio_init allocates the shared state object.
  if (ts == NULL)
  {
    ts = drv_shared_state_zalloc(sizeof(*ts), 0);
    if (ts == NULL)
    {
      spin_unlock(&trio_alloc_lock);
      return (HV_EFAULT);
    }
    trio_state[instance] = ts;

    // Initialize the new object.
    spin_lock_init(&ts->lock);
    ts->svc_dom_avail_mask = (1ull << TRIO_NUM_SVC_DOM) - 1;
    ts->shim_pos = info->idn_ports[0];
    ts->os_svc_dom = -1;
    ts->instance = instance;

    union virt_inst {
      bi_inst_t instance;
      struct bi_clock_inst clock;
    } virt_inst = {
      .clock.type = BI_CLOCK_INST_TYPE__VAL_TRIO,
      .clock.shim = ts->instance,
    };

    bi_ptr_t bp;
    if (bi_getparam(BI_TYPE_SHIM_VIRT_INST, virt_inst.instance, &bp, NULL) !=
        BI_NULL)
    {
      struct bi_shim_virt_inst* bsvi = (struct bi_shim_virt_inst*) bp;
      ts->virt_instance = bsvi->virt_inst;
    }
    else
      ts->virt_instance = instance;

    // Skip this if running the simulator.
    if (!sim_is_simulator())
    {
      //
      // Get the Tile Revision.
      //
      RSH_REV_ID_t rev_id;
      rev_id.word = cfg_rd(rshims[0]->idn_ports[0].word, 0, RSH_REV_ID);
      ts->is_gx72 =
        ((rev_id.chip_rev_id & 0xF0) == RSH_REV_ID__CHIP_REV_ID_VAL_TILEGX72);

      for (int mac = 0; mac < TILEGX_TRIO_PCIES; mac++)
      {
        TRIO_PCIE_INTFC_PORT_CONFIG_t port_config;
        size_t port_config_reg_offset;

        port_config_reg_offset = (TRIO_PCIE_INTFC_PORT_CONFIG <<
          TRIO_CFG_REGION_ADDR__REG_SHIFT) |
          (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
          TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
          (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);
        port_config.word = cfg_rd(ts->shim_pos.word, 0,
                                  port_config_reg_offset);

        // For endpoint ports, install the interrupt handlers
        // in order to bring the PCIe link up after it goes down.
        if (port_config.strap_state ==
          TRIO_PCIE_INTFC_PORT_CONFIG__STRAP_STATE_VAL_AUTO_CONFIG_ENDPOINT ||
          port_config.strap_state ==
          TRIO_PCIE_INTFC_PORT_CONFIG__STRAP_STATE_VAL_AUTO_CONFIG_ENDPOINT_G1)
        {
          int intchan = drv_alloc_intchan();
          if (intchan < 0)
          {
            printf("hv_warning: trio_init couldn't allocate interrupt");
            spin_unlock(&trio_alloc_lock);
            return (HV_EFAULT);
          }
          trio_intr[mac].ts = ts;
          trio_intr[mac].mac = mac;

          if (drv_register_intr(trio_delayed_intr, (void*) &trio_intr[mac],
                                DRV_INTR_DELAYED, intchan))
          {
            printf("hv_warning: trio_init couldn't register interrupt");
            drv_free_intchan(intchan);
            trio_intr[mac].ts = NULL;
            trio_intr[mac].mac = 0;
            spin_unlock(&trio_alloc_lock);
            return (HV_EFAULT);
          }

          TRIO_INT_BIND_t binding_setup =
          {{
            .vec_sel = TRIO_INT_BIND__VEC_SEL_VAL_MAC,
            .bind_sel = mac * 32 + TRIO_PCIE_INTFC_MAC_INT_STS__DL_STATE_SHIFT,
            .int_num = HV_PL,
            .evt_num = intchan,
            .tileid = DRV_COORDS_TO_TILE_ID(my_pos.bits.x, my_pos.bits.y),
            .mode = 0,              /* not used for MAC interrupts */
            .enable = 1,
          }};

          cfg_wr(ts->shim_pos.word, 0, TRIO_INT_BIND, binding_setup.word);
        }

        // For root complex ports, assert PERST and keep it asserted while we
        // do all of the controller and link manipulation.
        if (port_config.strap_state ==
            TRIO_PCIE_INTFC_PORT_CONFIG__STRAP_STATE_VAL_AUTO_CONFIG_RC ||
            port_config.strap_state ==
            TRIO_PCIE_INTFC_PORT_CONFIG__STRAP_STATE_VAL_AUTO_CONFIG_RC_G1)
        {
          union port_inst {
            bi_inst_t instance;
            struct bi_port_inst port;
          } port_inst = {
            .port.port = mac,
            .port.shim = ts->virt_instance,
          };

          bi_ptr_t bp;
          if (bi_getparam(BI_TYPE_PCIE_PORT_CFG, port_inst.instance,
                          &bp, NULL) != BI_NULL)
          {
            struct bi_pcie_port_cfg* port_cfg = bp;

            if (port_cfg->allow_rc)
              drv_set_signal(port_cfg->perst_sig,
                             DRV_SIGNAL_INIT | DRV_SIGNAL_ASSERT);
          }
        }
      }
    }

    // Register two cachelines worth of memory to be used by ingress
    // requests that don't match any MapMem or scatter queue regions.
    // The first cacheline is used to respond to reads; fill it with -1.
    size_t panic_mem_size = CHIP_L2_LINE_SIZE() * 2;
    void* panic_mem = drv_state_alloc(panic_mem_size, CHIP_L2_LINE_SIZE());
    memset(panic_mem, 0xff, panic_mem_size);

    TRIO_PANIC_MODE_CTL_t panic_mode_ctl =
      { .word = cfg_rd(ts->shim_pos.word, 0, TRIO_PANIC_MODE_CTL) };
    panic_mode_ctl.panic_pa = vtop((VA)panic_mem) >> CHIP_L2_LOG_LINE_SIZE();
    panic_mode_ctl.hfh = 0;
    panic_mode_ctl.tileid = DRV_COORDS_TO_TILE_ID(tile.bits.x, tile.bits.y);
    cfg_wr(ts->shim_pos.word, 0, TRIO_PANIC_MODE_CTL, panic_mode_ctl.word);

    // Increase the PIO timeout.  11 gets us an timeout of about 745 ms
    // (with a 720 MHz TRIO), which is still less than our MMIO timeout.
    TRIO_TILE_PIO_CTL_t ttpc = 
      { .word = cfg_rd(ts->shim_pos.word, 0, TRIO_TILE_PIO_CTL) };
    ttpc.cpl_timer = 11;
    cfg_wr(ts->shim_pos.word, 0, TRIO_TILE_PIO_CTL, ttpc.word);

    // Clear the MMU table, which is unimplemented by the simulator.
    if (!sim_is_simulator())
    {
      for (int i = 0; i < (1 << TRIO_MMU_TABLE__ENTRY_SEL_WIDTH); i++)
      {
        TRIO_MMU_TABLE_t pte = {{
            .entry_sel = i,
          }};

        cfg_wr(ts->shim_pos.word, 0, TRIO_MMU_TABLE, pte.word);
      }
    }

    // Work-around HW bug 14375.
    if (!sim_is_simulator())
    {
      TRIO_PUSH_DMA_DIAG_CTL_t push_dma_diag_ctl;

      push_dma_diag_ctl.word = cfg_rd(ts->shim_pos.word, 0,
                                      TRIO_PUSH_DMA_DIAG_CTL);
      push_dma_diag_ctl.req_hwm_reduce = 1;
      cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_DIAG_CTL,
             push_dma_diag_ctl.word);

    }
  }

  // First core to call trio_init registers the RSHIM SWINT handler
  // which cleans up the TRIO resources prior to a chip reset.
    // Skip this if running the simulator.
  if (!sim_is_simulator() && trio_cleanup_handler_registered == 0)
  {
    int intchan = drv_alloc_intchan();
    if (intchan < 0)
    {
      printf("hv_warning: trio_init couldn't allocate interrupt "
             "for RSHIM SWINT");
      spin_unlock(&trio_alloc_lock);
      return (HV_EFAULT);
    }

    if (drv_register_intr(trio_cleanup_delayed_intr, (void*) trio_state,
                          DRV_INTR_DELAYED, intchan))
    {
      printf("hv_warning: trio_init couldn't register interrupt "
             "for RSHIM SWINT");
      drv_free_intchan(intchan);
      spin_unlock(&trio_alloc_lock);
      return (HV_EFAULT);
    }

    //
    // Use RSHIM SWINT to signal the TRIO cleanup prior to the chip reset.
    //
    RSH_INT_BIND_t int_bind =
    {{
      .enable = 1,
      .mode = 0,
      .tileid = DRV_COORDS_TO_TILE_ID(my_pos.bits.x, my_pos.bits.y),
      .dev_sel = RSH_INT_BIND__DEV_SEL_VAL_CH0,
      .int_num = HV_PL,
      .evt_num = intchan,
    }};

    int_bind.bind_sel = RSH_INT_BIND__BIND_SEL_VAL_SWINT3;
    cfg_wr(rshims[0]->idn_ports[0].word, 0, RSH_INT_BIND, int_bind.word);

    trio_cleanup_handler_registered = 1;
  }
  spin_unlock(&trio_alloc_lock);

  *statepp = ts;

  return (0);
}


/** TRIO driver open routine - a new context number for each open. */
static int
trio_open(int devhdl, void* statep, const char* suffix,
           uint32_t flags, pos_t tile)
{
  trio_state_t* ts = statep;

  DEVICE_TRACE("trio_open: devhdl %#x suffix \"%s\" flags %#x "
               "tile %#x\n", devhdl, suffix, flags, tile.word);

  if (!strcmp(suffix, "/iorpc"))
  {
    spin_lock(&ts->lock);

    if (ts->svc_dom_avail_mask == 0)
    {
      spin_unlock(&ts->lock);
      return (GXIO_ERR_NO_SVC_DOM);
    }

    int svc_dom = ffs(ts->svc_dom_avail_mask) - 1;

    /* Commit the service domain allocation. */
    ts->svc_dom_avail_mask &= ~(1ull << svc_dom);

    /* Ensure only one client (kernel) gets the MMIO mapping. */
    if (!ts->cfg_mmio_mapped)
    {
      ts->cfg_mmio_mapped = 1;
      spin_unlock(&ts->lock);

      /* Permit MMIO access to config space. */
      int err = drv_permit_mmio_access(ts->shim_pos, HV_TRIO_CONFIG_OFFSET,
                                       HV_TRIO_CONFIG_SIZE, CLIENTNO);

      if (err != 0)
      {
        spin_lock(&ts->lock);
        ts->svc_dom_avail_mask |= (1ull << svc_dom);
        ts->cfg_mmio_mapped = 0;
        spin_unlock(&ts->lock);
        return err;
      }

      spin_lock(&ts->lock);
      ts->os_svc_dom = svc_dom;
    }

    spin_unlock(&ts->lock);

    /* Use the service domain as part of the opaque fd number. */
    return svc_dom;
  }
  return (HV_ENODEV);
}


/** TRIO driver close routine. */
static int
trio_close(int devhdl, void* statep, pos_t tile)
{
  trio_state_t* ts = statep;
  unsigned int svc_dom = DRV_HDL2BITS(devhdl);
  trio_resources_t* svc_dom_resources = &ts->svc_dom_resources[svc_dom];

  DEVICE_TRACE("trio_close: devhdl %#x\n", devhdl);

  spin_lock(&ts->lock);

  if (!is_open_svc_dom(svc_dom, ts))
  {
    spin_unlock(&ts->lock);
    return GXIO_ERR_INVAL_SVC_DOM;
  }

  // Reset all service domain state.
  if (svc_dom_resources->map_mem_mask)
  {
    do
    {
      int pos = ffs(svc_dom_resources->map_mem_mask) - 1;
      svc_dom_resources->map_mem_mask &= ~(1 << pos);
      ts->resources.map_mem_mask &= ~(1 << pos);
      flush_map_mem(ts, pos);

    } while (svc_dom_resources->map_mem_mask);
  }

  if (svc_dom_resources->sq_mask)
  {
    do
    {
      int pos = ffs(svc_dom_resources->sq_mask) - 1;
      svc_dom_resources->sq_mask &= ~(1 << pos);
      ts->resources.sq_mask &= ~(1 << pos);
      flush_sq(ts, pos);
      spin_unlock(&ts->lock);

      if (drv_deny_mmio_access(ts->shim_pos, HV_TRIO_SQ_OFFSET(pos),
                               HV_TRIO_SQ_SIZE, CLIENTNO))
        WARN("Unexpected sq deny_mmio_access() failure\n");

      spin_lock(&ts->lock);
    } while (svc_dom_resources->sq_mask);
  }

  if (svc_dom_resources->pio_mask)
  {
    do
    {
      int pos = ffs(svc_dom_resources->pio_mask) - 1;
      svc_dom_resources->pio_mask &= ~(1 << pos);
      ts->resources.pio_mask &= ~(1 << pos);

      // Disable this PIO region.
      cfg_wr(ts->shim_pos.word, 0,
         TRIO_TILE_PIO_REGION_SETUP__FIRST_WORD +
         sizeof(TRIO_TILE_PIO_REGION_SETUP_t) * pos, 0);
      spin_unlock(&ts->lock);

      if (drv_deny_mmio_access(ts->shim_pos, HV_TRIO_PIO_OFFSET(pos),
                               HV_TRIO_PIO_SIZE, CLIENTNO))
        WARN("Unexpected sq deny_mmio_access() failure\n");

      spin_lock(&ts->lock);
    } while (svc_dom_resources->pio_mask);
  }

  if (svc_dom_resources->push_dma_mask)
  {
    do
    {
      int pos = ffs(svc_dom_resources->push_dma_mask) - 1;
      svc_dom_resources->push_dma_mask &= ~(1 << pos);
      ts->resources.push_dma_mask &= ~(1 << pos);
      flush_push_dma(ts, pos);
      spin_unlock(&ts->lock);

      if (drv_deny_mmio_access(ts->shim_pos, HV_TRIO_PUSH_DMA_OFFSET(pos),
                               HV_TRIO_DMA_REGION_SIZE, CLIENTNO))
        WARN("Unexpected push_dma deny_mmio_access() failure\n");

      spin_lock(&ts->lock);
    } while (svc_dom_resources->push_dma_mask);
  }

  if (svc_dom_resources->pull_dma_mask)
  {
    do
    {
      int pos = ffs(svc_dom_resources->pull_dma_mask) - 1;
      svc_dom_resources->pull_dma_mask &= ~(1 << pos);
      ts->resources.pull_dma_mask &= ~(1 << pos);
      flush_pull_dma(ts, pos);
      spin_unlock(&ts->lock);

      if (drv_deny_mmio_access(ts->shim_pos, HV_TRIO_PULL_DMA_OFFSET(pos),
                               HV_TRIO_DMA_REGION_SIZE, CLIENTNO))
        WARN("Unexpected pull_dma deny_mmio_access() failure\n");

      spin_lock(&ts->lock);
    } while (svc_dom_resources->pull_dma_mask);
  }

  if (svc_dom_resources->asid_mask)
  {
    do
    {
      int pos = ffs(svc_dom_resources->asid_mask) - 1;
      ts->iotlb_entries_used[pos] = 0;
      svc_dom_resources->asid_mask &= ~(1 << pos);
      ts->resources.asid_mask &= ~(1 << pos);

      // Invalidate all IOTLB entries with this ASID.
      inv_iotlb(ts, pos);

    } while (svc_dom_resources->asid_mask);

    // Flush any cached TLB entries.
    flush_micro_tlbs(ts);
  }

  // Allow some future call to reuse the service domain.
  ts->svc_dom_avail_mask |= (1ull << svc_dom);

  if (ts->os_svc_dom == svc_dom)
  {
    assert(ts->svc_dom_avail_mask == (1ull << TRIO_NUM_SVC_DOM) - 1);

    // Disable all MAC interrupts, even though only some of the interrupts
    // are enabled because this is easier than going through them one by one.
    // NOTE: this works because so far all MAC interrupts are handled by
    // the OS only.
    disable_mac_intrs(ts);

    ts->os_svc_dom = -1;
    spin_unlock(&ts->lock);

    if (drv_deny_mmio_access(ts->shim_pos, HV_TRIO_CONFIG_OFFSET,
                             HV_TRIO_CONFIG_SIZE, CLIENTNO))
      DEVICE_TRACE("Unexpected deny_mmio_access() failure at close\n");

    spin_lock(&ts->lock);
    ts->cfg_mmio_mapped = 0;
    for (int node = 0; node < TILE_MAX_MSHIMS; node++)
      ts->rc_phys_mem_mapped[node] = 0;

#ifdef USE_IOMMU_FOR_RC
    ts->mmu_page_size_set = 0;
#endif
  }

  spin_unlock(&ts->lock);

  return (0);
}


/** TRIO driver close_all routine. */
static int
trio_close_all(int dev_idx, void* statep)
{
  DEVICE_TRACE("trio_close_all: dev_idx %d\n", dev_idx);

  for (int svc_dom = 0; svc_dom < TRIO_NUM_SVC_DOM; svc_dom++)
  {
    int devhdl = MK_HDL(dev_idx, svc_dom);

    trio_close(devhdl, statep, my_pos);
  }

  return (0);
}


/** TRIO driver read routine. */
static int
trio_pread(int devhdl, void* statep, uint32_t flags, char* va,
            uint32_t len, uint64_t offset, pos_t tile)
{
  char buf[MAX_STACK_BYTES];
  trio_state_t* ts = statep;
  unsigned int index = DRV_HDL2BITS(devhdl);
  int result;

  DEVICE_TRACE("trio_pread: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  if (len > sizeof(buf))
    return (HV_EINVAL);

  spin_lock(&ts->lock);

  if (!is_open_svc_dom(index, ts))
  {
    spin_unlock(&ts->lock);
    return (GXIO_ERR_INVAL_SVC_DOM);
  }

  result = dispatch_gxio_trio_read(offset, buf, len, ts, index);

  spin_unlock(&ts->lock);

  if (drv_copy_to_client(va, buf, len, flags))
    result = HV_EFAULT;

  return result;
}


/** TRIO driver write routine. */
static int
trio_pwrite(int devhdl, void* statep, uint32_t flags, char* va,
             uint32_t len, uint64_t offset, pos_t tile)
{
  char buf[MAX_STACK_BYTES];
  trio_state_t* ts = statep;
  unsigned int index = DRV_HDL2BITS(devhdl);
  int result;

  DEVICE_TRACE("trio_pwrite: devhdl %#x flags %#x va %p len %d "
               "offset %#llx tile %#x\n", devhdl, flags, va, len, offset,
               tile.word);

  if (len > sizeof(buf))
    return (HV_EINVAL);

  spin_lock(&ts->lock);

  if (!is_open_svc_dom(index, ts))
  {
    spin_unlock(&ts->lock);
    return (GXIO_ERR_INVAL_SVC_DOM);
  }

  spin_unlock(&ts->lock);

  if (drv_copy_from_client(buf, va, len, flags))
    return (HV_EFAULT);

  spin_lock(&ts->lock);

  result = dispatch_gxio_trio_write(offset, buf, len, ts, index);

  spin_unlock(&ts->lock);

  return result;
}


/** Get the current setting for the TRIO PLL. */
static long
trio_get_cur_freq(const struct dev_info* info, int clock_index)
{
  TRIO_CLOCK_CONTROL_t tcc = 
  {
    .word = cfg_rd(info->idn_ports[0].word, info->channel, TRIO_CLOCK_CONTROL)
  };

  return pll_to_freq(!tcc.ena, tcc.pll_m, tcc.pll_n, tcc.pll_q, REFCLK);
}


/** Get the desired setting for the TRIO PLL. */
static long
trio_get_desired_freq(const struct dev_info* info, int clock_index)
{
  //
  // If it's set in the .hvc, use that value.
  //
  if (info->speeds[clock_index])
    return info->speeds[clock_index];

  //
  // See if there's a board default in the BIB, and if so, use it.
  //
  TRIO_DEV_INFO_t tdi = 
  {
    .word = cfg_rd(info->idn_ports[0].word, info->channel, TRIO_DEV_INFO)
  };

  union
  {
    bi_inst_t inst;
    struct bi_clock_inst bci;
  }
  ci =
  {
    .bci.type = BI_CLOCK_INST_TYPE__VAL_TRIO,
    .bci.shim = tdi.instance,
  };

  bi_ptr_t bp;

  if (bi_getparam(BI_TYPE_SHIM_CLOCK, ci.inst, &bp, NULL) != BI_NULL)
  {
    struct bi_shim_clock* sc = bp;
    return sc->freq;
  }

  //
  // If we haven't found a value, we want to run as fast as we can subject
  // to the voltage required by other shims.
  //
  return DRV_DESIRED_FREQ_MAX;
}


/** Set the TRIO PLL frequency. */
static int
trio_set_freq(const struct dev_info* info, int clock_index, long freq)
{
  unsigned int m, n, q, range;

  freq_to_pll(freq, &m, &n, &q, &range, REFCLK, 0);

  TRIO_CLOCK_CONTROL_t tcc = 
  {{
    .ena = 1,
    .pll_m = m,
    .pll_n = n,
    .pll_q = q,
    .pll_range = range,
  }};

  cfg_wr(info->idn_ports[0].word, info->channel, TRIO_CLOCK_CONTROL, tcc.word);
  __insn_mf();

  do
  {
    tcc.word = cfg_rd(info->idn_ports[0].word, info->channel,
                      TRIO_CLOCK_CONTROL);
  }
  while (!tcc.clock_ready);

  return 0;
}


/** TRIO driver operations vector */
static struct drv_ops trio_ops = {
  .init             = trio_init,
  .open             = trio_open,
  .close            = trio_close,
  .close_all        = trio_close_all,
  .pread            = trio_pread,
  .pwrite           = trio_pwrite,
  .get_cur_freq     = trio_get_cur_freq,
  .get_desired_freq = trio_get_desired_freq,
  .set_freq         = trio_set_freq,
};


//! Add a new "driver" entry.
static const __DRIVER_ATTR driver_t driver_trio = {
  .shim_type  = TRIO_DEV_INFO__TYPE_VAL_TRIO,
  .name       = "trio",
  .desc       = "trio Driver",
  .ops        = &trio_ops,
};


///////////////////////////////////////////////////////////////////
//                        Global Methods                         //
///////////////////////////////////////////////////////////////////

/** A generic function for checking a range of resources in a bitmask.
 *
 * @param res Resource number.
 * @param in_use_mask Currently allocated resource bits.
 * @param bitmask_bits Number of valid bits that could be in bitmask.
 *
 * @return true if the resource is illegal or unallocated, else false.
 */
static bool
bad_resource(unsigned int res,
             uint64_t in_use_mask,
             unsigned int bitmask_bits)
{
  return (res >= bitmask_bits || (in_use_mask & (1ULL << res)) == 0);
}

/** Return the base PTE that the client should use to access our
    shim's MMIO registers. */
int
handle_gxio_trio_get_mmio_base(trio_state_t* ts, int svc_dom, HV_PTE *base)
{
  PA pa = 0;
  HV_PTE pte = { 0 };
  pte = hv_pte_set_mode(pte, HV_PTE_MODE_MMIO);
  pte = hv_pte_set_lotar(pte, HV_XY_TO_LOTAR(ts->shim_pos.bits.x,
                                             ts->shim_pos.bits.y));
  pte = hv_pte_set_pa(pte, pa);

  *base = pte;
  return 0;
}

/** Check to see whether a client is allowed to map a range of addresses. */
int
handle_gxio_trio_check_mmio_offset(trio_state_t* ts, int svc_dom,
                                   unsigned long offset, unsigned long size)
{
  TRIO_MMIO_ADDRESS_SPACE_t start_space, end_space;
  start_space.word = offset;
  end_space.word = offset + size - 1;

  // Never allow mappings to span regions.
  if (start_space.region == end_space.region)
  {
    // Check for scatter queue permissions.
    if (start_space.region == TRIO_MMIO_ADDRESS_SPACE__REGION_VAL_MAP_SQ)
    {
      TRIO_MAP_SQ_REGION_ADDR_t addr;
      addr.word = offset;
      int sq = addr.sq_sel;
      if (ts->svc_dom_resources[svc_dom].sq_mask & (1 << sq))
      {
        TRACE("Allowing MMIO map of scatter queue %d\n", sq);
        return 0;
      }
    }
    // Check for PIO region permissions.
    else if (start_space.region >= HV_TRIO_FIRST_PIO_REGION &&
        end_space.region < HV_TRIO_FIRST_PIO_REGION + TRIO_NUM_TPIO_REGIONS)
    {
      int pio_region = start_space.region - HV_TRIO_FIRST_PIO_REGION;
      if (ts->svc_dom_resources[svc_dom].pio_mask & (1 << pio_region))
      {
        TRACE("Allowing MMIO map of pio_region %d\n", pio_region);
        return 0;
      }
    }
    // Check for push DMA region permissions.
    else if (start_space.region ==
             TRIO_MMIO_ADDRESS_SPACE__REGION_VAL_PUSH_DMA)
    {
      TRIO_PUSH_DMA_REGION_ADDR_t addr;
      addr.word = offset;
      int ring = addr.ring_sel;
      if (ts->svc_dom_resources[svc_dom].push_dma_mask & (1 << ring))
      {
        TRACE("Allowing MMIO map of ring %d\n", ring);
        return 0;
      }
    }
    // Check for pull DMA region permissions.
    else if (start_space.region ==
             TRIO_MMIO_ADDRESS_SPACE__REGION_VAL_PULL_DMA)
    {
      TRIO_PULL_DMA_REGION_ADDR_t addr;
      addr.word = offset;
      int ring = addr.ring_sel;
      if (ts->svc_dom_resources[svc_dom].pull_dma_mask & (1 << ring))
      {
        TRACE("Allowing MMIO map of ring %d\n", ring);
        return 0;
      }
    }
    // Check for TRIO/MAC configuration space permissions.
    else if (start_space.region ==
             TRIO_MMIO_ADDRESS_SPACE__REGION_VAL_CFG)
    {
      if (size <= HV_TRIO_CONFIG_SIZE)
      {
        TRACE("Allowing MMIO map of TRIO/MAC configuration space\n");
        return 0;
      }
    }
    // Check for Mem Map intr register space permissions.
    else if (start_space.region ==
             TRIO_MMIO_ADDRESS_SPACE__REGION_VAL_MAP_MEM)
    {
      TRIO_MAP_MEM_REGION_ADDR_t addr;
      addr.word = offset;
      int mem_map = addr.map_sel;
      if (ts->svc_dom_resources[svc_dom].map_mem_mask & (1 << mem_map))
      {
        TRACE("Allowing MMIO map of Mem Map %d\n", mem_map);
        return 0;
      }
    }
  }
  
  TRACE("check_mmio_offset() failed\n");
  return GXIO_ERR_MMIO_ADDRESS;
}

/** Map some memory into a particular IOTLB. */
int
handle_gxio_trio_register_client_memory(trio_state_t* ts, int svc_dom,
                                   unsigned int iotlb, HV_PTE pte,
                                   unsigned int flags)
{
  trio_resources_t* svc_dom_resources = &ts->svc_dom_resources[svc_dom];
  unsigned int asid = iotlb;
 
  // Verify "asid" is legal and has been allocated. 
  if (bad_resource(asid, svc_dom_resources->asid_mask, TRIO_NUM_ASIDS))
    return GXIO_TRIO_ERR_BAD_ASID;

  // Verify all IOTLB entries are available.
  if (ts->iotlb_entries_used[asid] != 0)
    return GXIO_ERR_IOTLB_ENTRY;

  int err = drv_map_cpa_space_to_iotlb(ts->shim_pos, asid, pte,
                                       TRIO_TLB_ENTRY_ADDR__FIRST_WORD,
                                       flags);
  if (err != 0)
    return err;

  ts->iotlb_entries_used[asid] = TRIO_NUM_TLBS_PER_ASID;

  flush_micro_tlbs(ts);

  return 0;
}

/** Read interrupt status from TRIO_INT_VEC0 to 4. */
int 
handle_gxio_trio_read_isr_status_aux(trio_state_t *ts, int svc_dom,
                                     unsigned int vec_num)
{
  TRIO_INT_VEC0_W1TC_t result;
  size_t reg_offset;
  svc_dom = svc_dom;

  // Access the TRIO_INT_VECx_W1TC register.
  switch (vec_num)
  {
    case 0:
    {
      reg_offset = TRIO_INT_VEC0;
      break; 
    }
  
    case 1:
    {
      reg_offset = TRIO_INT_VEC1_W1TC;
      break;
    }

    case 2:
    {
      reg_offset = TRIO_INT_VEC2_W1TC;
      break;
    }

    case 3:
    {
      reg_offset = TRIO_INT_VEC3_W1TC;
      break;
    }

    case 4:
    {
      reg_offset = TRIO_INT_VEC4_W1TC;
      break;
    }

    // Due to the error checking outside, this will never be accessed.
    default:
    {
      reg_offset = TRIO_INT_VEC0;
      break;
    }
  }

  result.word = cfg_rd(ts->shim_pos.word, 0, reg_offset); 
  
  return (result.word & 0xFFFF); 
}

/** Write interrupt status to TRIO_INT_VEC0 to 4 to clear particular
    interrupts. */
int
handle_gxio_trio_write_isr_status_aux(trio_state_t *ts, int svc_dom,
                                      unsigned int vec_num, 
                                      uint32_t bits_to_clear)
{
  size_t reg_offset;
  svc_dom = svc_dom;

  // Access the TRIO_INT_VECx_W1TC register.
  switch (vec_num)
  {
    case 0:
    {
      reg_offset = TRIO_INT_VEC0;
      break;
    }

    case 1:
    {
      reg_offset = TRIO_INT_VEC1_W1TC;
      break;
    }

    case 2:
    {
      reg_offset = TRIO_INT_VEC2_W1TC;
      break;
    }

    case 3:
    {
      reg_offset = TRIO_INT_VEC3_W1TC;
      break;
    }

    case 4:
    {
      reg_offset = TRIO_INT_VEC4_W1TC;
      break;
    }

    // Due to the error checking outside, this will never be accessed.
    default:
    {
      reg_offset = TRIO_INT_VEC0;
      break;
    }
  }

  cfg_wr(ts->shim_pos.word, 0, reg_offset, bits_to_clear);

  return 0;
}


///////////////////////////////////////////////////////////////////
//                    Resource De-allocation                     //
///////////////////////////////////////////////////////////////////


/** Helpful macro for creating a range of contiguous bits. */
#define BIT_RANGE(LOW_BIT, HIGH_BIT)                            \
  ((-1ULL >> (64 - ((HIGH_BIT) - (LOW_BIT) + 1))) << (LOW_BIT))


int
handle_gxio_trio_dealloc_asid(trio_state_t* ts, int svc_dom, unsigned int asid)
{
  trio_resources_t* svc_dom_resources = &ts->svc_dom_resources[svc_dom];

  // Verify "asid" is legal and has been allocated.
  if (bad_resource(asid, svc_dom_resources->asid_mask, TRIO_NUM_ASIDS))
    return GXIO_TRIO_ERR_BAD_ASID;

  svc_dom_resources->asid_mask &= ~(1 << asid);
  ts->resources.asid_mask &= ~(1 << asid);

  // Invalidate all IOTLB entries with this ASID.
  inv_iotlb(ts, asid);

  // Flush any cached TLB entries.
  flush_micro_tlbs(ts);

  ts->iotlb_entries_used[asid] = 0;

  return 0;
}

int
handle_gxio_trio_unregister_page_aux(trio_state_t* ts, int svc_dom,
                                     PA page_pa, size_t page_size,
                                     struct iorpc_mem_attr page_attr,
                                     unsigned int asid,
                                     uint64_t vpn)
{
  trio_resources_t* svc_dom_resources = &ts->svc_dom_resources[svc_dom];
  TRIO_TLB_ENTRY_ADDR_t addr;

  // Verify "asid" is legal and has been allocated.
  if (bad_resource(asid, svc_dom_resources->asid_mask, TRIO_NUM_ASIDS))
    return GXIO_TRIO_ERR_BAD_ASID;

  // Verify there is at least a filled IOTLB entry.
  if (ts->iotlb_entries_used[asid] == 0)
    return GXIO_ERR_IOTLB_ENTRY;

  // Find out the IOTLB entry which contains the target page.
  for (int entry = 0; entry < TRIO_NUM_TLBS_PER_ASID; entry++)
  {
    TRIO_TLB_TABLE_t table = {{
        .is_attr = 0, // Select TLB_ENTRY_ADDR.
        .entry = entry,
        .asid = asid,
      }};

    addr.word = cfg_rd(ts->shim_pos.word, 0,
                       TRIO_TLB_TABLE__FIRST_WORD + table.word);

    // Find the registered IOTLB entry.
    if (addr.pfn == page_pa >> HV_TRIO_PAGE_SHIFT && addr.vpn == vpn)
    {
      ts->iotlb_entries_used[asid]--;

      // First write to TRIO_TLB_ENTRY_ADDR.
      cfg_wr(ts->shim_pos.word, 0, TRIO_TLB_TABLE__FIRST_WORD + table.word,
             addr.word);

      // Write to clear TRIO_TLB_ENTRY_ATTR.
      table.is_attr = 1; // Select TLB_ENTRY_ATTR.

      TRIO_TLB_ENTRY_ATTR_t attr;
      attr.word = 0;
      cfg_wr(ts->shim_pos.word, 0, TRIO_TLB_TABLE__FIRST_WORD + table.word,
             attr.word);

      flush_micro_tlbs(ts);

      return 0;
    }
  }

  // No IOTLB entry found. 
  return GXIO_ERR_IOTLB_ENTRY;
}


///////////////////////////////////////////////////////////////////
//                      Resource Allocation                      //
///////////////////////////////////////////////////////////////////


/** A generic function for allocating a range of resources out of a bitmask.
 *
 * @param count Number of resources being allocated.
 * @param in_use_mask Currently allocated resource bits.
 * @param bitmask_bits Number of valid bits that could be in bitmask.
 * @param first_res First resource number, if explicitly requested by client.
 * @param flags If HV_TRIO_ALLOC_FIXED is set, start at first_res.
 * @param new_bits_out Filled with the newly allocated bits.
 *
 * @return The first allocated resource number, or -1 on failure.
 */
static int
alloc_trio_resources(unsigned int count, uint64_t in_use_mask,
                     unsigned int bitmask_bits, unsigned int first_res,
                     unsigned int flags, uint64_t* new_bits_out)
{
  uint64_t new_bits;

  if (flags & HV_TRIO_ALLOC_FIXED)
  {
    // The client requested a particular range of resources; convert
    // that to mask bits and see if they're available.
    unsigned int first_bit = first_res;
    unsigned int last_bit = (first_res + (count - 1));
    new_bits = BIT_RANGE(first_bit, last_bit);

    if ((last_bit >= bitmask_bits) ||
        (in_use_mask & new_bits))
    {
      TRACE("Fixed resource allocation failed\n");
      return -1;
    }
  }
  else
  {
    // Any range of resources will do; see how many bits need to be
    // allocated and scan for that many contiguous bits.
    // With resources being returned, the available bitmask could
    // be fragmented and getting contiguous bits could fail. Then
    // the resources can be allocated one at a time.
    unsigned int num_bits = count;
    if (num_bits > bitmask_bits)
      return -1;

    unsigned int first_bit = 0;
    new_bits = ((1ULL << num_bits) - 1) << first_bit;

    while (first_bit <= bitmask_bits - num_bits)
    {
      if ((in_use_mask & new_bits) == 0)
        break;

      first_bit++;
      new_bits <<= 1;
    }
    if (first_bit > bitmask_bits - num_bits)
    {
      TRACE("Resource allocation failed\n");
      return -1;
    }

    first_res = first_bit;
  }

  // Apply the new bits to the bitmask and return the first allocated
  // resource number.
  TRACE("Resource allocation succeeded: first_res = %d, new_bits = %#llx\n",
        first_res, new_bits);
  
  *new_bits_out = new_bits;
  return first_res;
}

int
handle_gxio_trio_alloc_asids(trio_state_t* ts, int svc_dom,
                             unsigned int count, unsigned int first,
                             unsigned int flags)
{
  trio_resources_t* svc_dom_resources = &ts->svc_dom_resources[svc_dom];
  uint64_t new_bits = 0;
  int result;

  if (count == 0)
    return GXIO_ERR_INVAL;

  result = alloc_trio_resources(count, ts->resources.asid_mask,
                                TRIO_NUM_ASIDS, first, flags,
                                &new_bits);
  if (result < 0)
    return (GXIO_TRIO_ERR_NO_ASID);

  svc_dom_resources->asid_mask |= new_bits;
  ts->resources.asid_mask |= new_bits;

  // Zero out the valid bits before allowing the user to access the
  // IOTLB; the hardware comes out of reset with the first entry as a
  // valid, PA=VA mapping.
  for (int asid = result; asid < result + count; asid++)
  {
    for (int entry = 0; entry < TRIO_NUM_TLBS_PER_ASID; entry++)
    {
      TRIO_TLB_TABLE_t table = {{
          .entry = entry,
          .asid = asid,
        }};
      
      TRIO_TLB_ENTRY_ATTR_t attr = {{
          .vld = 0,
        }};
      cfg_wr(ts->shim_pos.word, 0,
             TRIO_TLB_ENTRY_ATTR__FIRST_WORD + table.word, attr.word);
    }
  }

  return result;

}

int
handle_gxio_trio_alloc_memory_maps(trio_state_t* ts, int svc_dom,
                                   unsigned int count, unsigned int first,
                                   unsigned int flags)
{
  trio_resources_t* svc_dom_resources = &ts->svc_dom_resources[svc_dom];
  uint64_t new_bits = 0;
  int result;

  if (count == 0)
    return GXIO_ERR_INVAL;

  result = alloc_trio_resources(count, ts->resources.map_mem_mask,
                                TRIO_NUM_MAP_MEM_REGIONS, first, flags,
                                &new_bits);
  if (result < 0)
    return (GXIO_TRIO_ERR_NO_MEMORY_MAP);

  svc_dom_resources->map_mem_mask |= new_bits;
  ts->resources.map_mem_mask |= new_bits;

  return result;
}

/** Allocate scatter queue regions. */
int
handle_gxio_trio_alloc_scatter_queues(trio_state_t* ts, int svc_dom,
                                      unsigned int count, unsigned int first,
                                      unsigned int flags)
{
  trio_resources_t* svc_dom_resources = &ts->svc_dom_resources[svc_dom];
  uint64_t new_bits = 0;
  int result;
  int err;

  if (count == 0)
    return GXIO_ERR_INVAL;

  result = alloc_trio_resources(count, ts->resources.sq_mask,
                                TRIO_NUM_MAP_SQ_REGIONS, first, flags,
                                &new_bits);
  if (result < 0)
    return (GXIO_TRIO_ERR_NO_SCATTER_QUEUE);

  svc_dom_resources->sq_mask |= new_bits;
  ts->resources.sq_mask |= new_bits;
  
  //
  // Can't hold spinlocks during a permit_access() call.
  // 
  spin_unlock(&ts->lock);

  err = drv_permit_mmio_access(ts->shim_pos, HV_TRIO_SQ_OFFSET(result),
                               HV_TRIO_SQ_SIZE * count, CLIENTNO);
  spin_lock(&ts->lock);
  
  if (err != 0)
  {
    WARN("Unexpected SQ alloc permit_mmio_access() failure\n");
    svc_dom_resources->sq_mask &= ~new_bits;
    ts->resources.sq_mask &= ~new_bits;
    return err;
  }

  return result;
}

/** Allocate a PIO region. */
int
handle_gxio_trio_alloc_pio_regions(trio_state_t* ts, int svc_dom,
                                   unsigned int count, unsigned int first,
                                   unsigned int flags)
{
  trio_resources_t* svc_dom_resources = &ts->svc_dom_resources[svc_dom];
  uint64_t new_bits = 0;
  int result;
  int err;

  if (count == 0)
    return GXIO_ERR_INVAL;

  result = alloc_trio_resources(count, ts->resources.pio_mask,
                                TRIO_NUM_TPIO_REGIONS, first, flags,
                                &new_bits);
  if (result < 0)
    return (GXIO_TRIO_ERR_NO_PIO);

  svc_dom_resources->pio_mask |= new_bits;
  ts->resources.pio_mask |= new_bits;

  //
  // Can't hold spinlocks during a permit_access() call.
  //
  spin_unlock(&ts->lock);

  err = drv_permit_mmio_access(ts->shim_pos, HV_TRIO_PIO_OFFSET(result),
                               HV_TRIO_PIO_SIZE * count, CLIENTNO);
  spin_lock(&ts->lock);
  
  if (err != 0)
  {
    WARN("Unexpected PIO alloc permit_mmio_access() failure\n");
    svc_dom_resources->pio_mask &= ~new_bits;
    ts->resources.pio_mask &= ~new_bits;
    return err;
  }

  return result;
}

/** Free a push DMA region. */
int
handle_gxio_trio_free_push_dma_ring_aux(trio_state_t* ts, int svc_dom,
                                        unsigned int ring)
{
  trio_resources_t* svc_dom_resources = &ts->svc_dom_resources[svc_dom];

  if (bad_resource(ring, ts->svc_dom_resources[svc_dom].push_dma_mask,
                   TRIO_NUM_PUSH_DMA_RINGS))
    return GXIO_TRIO_ERR_BAD_PUSH_DMA_RING;
  
  svc_dom_resources->push_dma_mask &= ~(1 << ring);
  ts->resources.push_dma_mask &= ~(1 << ring);
  flush_push_dma(ts, ring);
  spin_unlock(&ts->lock);

  if (drv_deny_mmio_access(ts->shim_pos, HV_TRIO_PUSH_DMA_OFFSET(ring),
                           HV_TRIO_DMA_REGION_SIZE, CLIENTNO))
    WARN("Unexpected push_dma deny_mmio_access() failure\n");

  spin_lock(&ts->lock);

  return 0;
}

/** Allocate a push DMA region. */
int
handle_gxio_trio_alloc_push_dma_ring(trio_state_t* ts, int svc_dom,
                                     unsigned int count, unsigned int first,
                                     unsigned int flags)
{
  trio_resources_t* svc_dom_resources = &ts->svc_dom_resources[svc_dom];
  uint64_t new_bits = 0;
  int result;
  int err;

  if (count == 0)
    return GXIO_ERR_INVAL;

  result = alloc_trio_resources(count, ts->resources.push_dma_mask,
                                TRIO_NUM_PUSH_DMA_RINGS, first, flags,
                                &new_bits);
  if (result < 0)
    return (GXIO_TRIO_ERR_NO_PUSH_DMA_RING);
  
  svc_dom_resources->push_dma_mask |= new_bits;
  ts->resources.push_dma_mask |= new_bits;
  
  //
  // Can't hold spinlocks during a permit_access() call.
  //
  spin_unlock(&ts->lock);
  
  err = drv_permit_mmio_access(ts->shim_pos, HV_TRIO_PUSH_DMA_OFFSET(result),
                               HV_TRIO_DMA_REGION_SIZE * count, CLIENTNO);
  spin_lock(&ts->lock);
  
  if (err != 0)
  {
    WARN("Unexpected push DMA permit_mmio_access() failure\n");
    svc_dom_resources->push_dma_mask &= ~new_bits;
    ts->resources.push_dma_mask &= ~new_bits;
    return err;
  }

  return result;
}

/** Free a pull DMA region. */
int
handle_gxio_trio_free_pull_dma_ring_aux(trio_state_t* ts, int svc_dom,
                                        unsigned int ring)
{
  trio_resources_t* svc_dom_resources = &ts->svc_dom_resources[svc_dom];

  if (bad_resource(ring, ts->svc_dom_resources[svc_dom].pull_dma_mask,
                   TRIO_NUM_PULL_DMA_RINGS))
    return GXIO_TRIO_ERR_BAD_PULL_DMA_RING;
  
  svc_dom_resources->pull_dma_mask &= ~(1 << ring);
  ts->resources.pull_dma_mask &= ~(1 << ring);
  flush_pull_dma(ts, ring);
  spin_unlock(&ts->lock);

  if (drv_deny_mmio_access(ts->shim_pos, HV_TRIO_PULL_DMA_OFFSET(ring),
                           HV_TRIO_DMA_REGION_SIZE, CLIENTNO))
    WARN("Unexpected pull_dma deny_mmio_access() failure\n");

  spin_lock(&ts->lock);

  return 0;
}

int
handle_gxio_trio_alloc_pull_dma_ring(trio_state_t* ts, int svc_dom,
                                     unsigned int count, unsigned int first,
                                     unsigned int flags)
{
  trio_resources_t* svc_dom_resources = &ts->svc_dom_resources[svc_dom];
  uint64_t new_bits = 0;
  int result;
  int err;

  if (count == 0)
    return GXIO_ERR_INVAL;

  result = alloc_trio_resources(count, ts->resources.pull_dma_mask,
                                TRIO_NUM_PULL_DMA_RINGS, first, flags,
                                &new_bits);
  if (result < 0)
    return (GXIO_TRIO_ERR_NO_PULL_DMA_RING);
  
  svc_dom_resources->pull_dma_mask |= new_bits;
  ts->resources.pull_dma_mask |= new_bits;

  //
  // Can't hold spinlocks during a permit_access() call.
  //
  spin_unlock(&ts->lock);

  err = drv_permit_mmio_access(ts->shim_pos, HV_TRIO_PULL_DMA_OFFSET(result),
                               HV_TRIO_DMA_REGION_SIZE * count, CLIENTNO);
  spin_lock(&ts->lock);
  
  if (err != 0)
  {
    WARN("Unexpected pull DMA permit_mmio_access() failure\n");
    svc_dom_resources->pull_dma_mask &= ~new_bits;
    ts->resources.pull_dma_mask &= ~new_bits;
    return err;
  }

  return result;
}


///////////////////////////////////////////////////////////////////
//                    Resource Initialization                    //
///////////////////////////////////////////////////////////////////

int
handle_gxio_trio_register_page_aux(trio_state_t* ts, int svc_dom,
                                   PA page_pa, size_t page_size,
                                   struct iorpc_mem_attr page_attr,
                                   unsigned int asid,
                                   uint64_t vpn)
{
  trio_resources_t* svc_dom_resources = &ts->svc_dom_resources[svc_dom];
  TRIO_TLB_TABLE_t table = {{
      .is_attr = 1,  // Select TLB_ENTRY_ATTR.
      .asid = asid,
    }};

  // Verify page size.
  int log2_page_size = __builtin_ctzl(page_size);
  if (log2_page_size < 12 || log2_page_size > CHIP_PA_WIDTH())
    return GXIO_ERR_IOTLB_ENTRY;

  // Verify "asid" is legal and has been allocated.
  if (bad_resource(asid, svc_dom_resources->asid_mask, TRIO_NUM_ASIDS))
    return GXIO_TRIO_ERR_BAD_ASID;

  // Verify there is at least an empty IOTLB entry.
  if (ts->iotlb_entries_used[asid] == TRIO_NUM_TLBS_PER_ASID)
    return GXIO_ERR_IOTLB_ENTRY;

  // Find out the 1st empty IOTLB entry.
  for (int entry = 0; entry < TRIO_NUM_TLBS_PER_ASID; entry++)
  {
    table.entry = entry;

    TRIO_TLB_ENTRY_ATTR_t attr;
    attr.word = cfg_rd(ts->shim_pos.word, 0,
                       TRIO_TLB_TABLE__FIRST_WORD + table.word);
    if (attr.vld == 0)
    {
      ts->iotlb_entries_used[asid]++;
      break;
    }
  }

  table.is_attr = 0; // Select TLB_ENTRY_ADDR.

  TRIO_TLB_ENTRY_ADDR_t addr = {{
      .pfn = page_pa >> 12,
      .vpn = vpn,
    }};
  cfg_wr(ts->shim_pos.word, 0,
         TRIO_TLB_TABLE__FIRST_WORD + table.word, addr.word);

  table.is_attr = 1; // Select TLB_ENTRY_ATTR.

  TRIO_TLB_ENTRY_ATTR_t attr = {{
      .vld = 1,
      .ps = log2_page_size - 12,
      .home_mapping = !page_attr.hfh,
      .pin = page_attr.io_pin,
      .nt_hint = page_attr.nt_hint,
      .loc_y_or_offset = page_attr.lotar_y,
      .loc_x_or_mask = page_attr.lotar_x,
      //.lru = UNUSED
    }};
  cfg_wr(ts->shim_pos.word, 0,
         TRIO_TLB_TABLE__FIRST_WORD + table.word, attr.word);

  flush_micro_tlbs(ts);

  return 0;
}

int
handle_gxio_trio_free_memory_map_aux(trio_state_t* ts, int svc_dom,
                                     unsigned int map)
{
  trio_resources_t* svc_dom_resources = &ts->svc_dom_resources[svc_dom];

  if (bad_resource(map, ts->svc_dom_resources[svc_dom].map_mem_mask,
                   TRIO_NUM_MAP_MEM_REGIONS))
    return GXIO_TRIO_ERR_BAD_MEMORY_MAP;

  svc_dom_resources->map_mem_mask &= ~(1 << map);
  ts->resources.map_mem_mask &= ~(1 << map);
  flush_map_mem(ts, map);

  return 0;
}

int
handle_gxio_trio_init_memory_map_aux(trio_state_t* ts, int svc_dom,
                                     unsigned int map, uint64_t vpn,
                                     uint64_t size, unsigned int asid,
                                     unsigned int mac, uint64_t bus_address,
                                     unsigned int order_mode)
{
  if (bad_resource(map, ts->svc_dom_resources[svc_dom].map_mem_mask,
                   TRIO_NUM_MAP_MEM_REGIONS))
    return GXIO_TRIO_ERR_BAD_MEMORY_MAP;
  if (bad_resource(asid, ts->svc_dom_resources[svc_dom].asid_mask,
                   TRIO_NUM_ASIDS))
    return GXIO_TRIO_ERR_BAD_ASID;

  if (bus_address & (HV_TRIO_PAGE_SIZE - 1) ||
      size & (HV_TRIO_PAGE_SIZE - 1))
    return GXIO_ERR_ALIGNMENT;

  if (order_mode != TRIO_MAP_MEM_SETUP__ORDER_MODE_VAL_UNORDERED &&
      order_mode != TRIO_MAP_MEM_SETUP__ORDER_MODE_VAL_STRICT &&
      order_mode != TRIO_MAP_MEM_SETUP__ORDER_MODE_VAL_REL_ORD)
    return GXIO_ERR_INVAL;

  // There's a four word stride for each map-mem register set.
  size_t reg_offset = 4 * sizeof(uint64_t) * map;

  // Specifies the starting bus address (4KB aligned).
  TRIO_MAP_MEM_BASE_t base = {{
      .addr = bus_address >> HV_TRIO_PAGE_SHIFT,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_MEM_BASE__FIRST_WORD + reg_offset,
         base.word);

  // Specifies the last address (4KB - 1 aligned).
  TRIO_MAP_MEM_LIM_t lim = {{
      .addr = (bus_address + size - 1) >> HV_TRIO_PAGE_SHIFT,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_MEM_LIM__FIRST_WORD + reg_offset,
         lim.word);

  // Specifies the other attributes, and makes the whole set of
  // register writes take effect (via .mac_ena).  
  TRIO_MAP_MEM_SETUP_t setup = {{
      .mac_ena = (1 << mac),
      .order_mode = order_mode,
      .int_ena = 0,
      .use_mmu = 0,
      .va = vpn,
      .asid = asid,
      .int_mode = 0,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_MEM_SETUP__FIRST_WORD + reg_offset,
         setup.word);

  return 0;
}

// This is used to map all the physical memory under one memory controller to
// the PCI bus space, by configuring I/O MMU or TLB to cover the CPA range of
// the memory. Each MMU PTE or TLB entry has the hash-for-home attribute set.
// If non-HFH pages are to be supported, a separate Mem Map region needs to be
// used to map the bus range [CPA + fixed_offset, size) and I/O TLB entries
// with explicit homes are used for the CPA-to-PA address translation.
// The I/O TLB entries will be allocated upon DMA setup and deallocated
// upon DMA teardown on a per transfer basis.
// For each MAC, two Mem-Map regions are allocated to support the 32-bit
// devices and 64-bit devices, respectively.
// FIXME: share Mem Maps among all RC ports in the same TRIO instance.
int
handle_gxio_trio_init_memory_map_mmu_aux(trio_state_t* ts, int svc_dom,
                                         unsigned int m_nodes, unsigned long va,
                                         uint64_t size, unsigned int asid,
                                         unsigned int mac, uint64_t bus_address,
                                         unsigned int node,
                                         unsigned int order_mode)
{
  if (bad_resource(asid, ts->svc_dom_resources[svc_dom].asid_mask,
                   TRIO_NUM_ASIDS))
    return GXIO_TRIO_ERR_BAD_ASID;

  if (bus_address & (HV_TRIO_PAGE_SIZE - 1) ||
      size & (HV_TRIO_PAGE_SIZE - 1))
    return GXIO_ERR_ALIGNMENT;

  if (order_mode != TRIO_MAP_MEM_SETUP__ORDER_MODE_VAL_UNORDERED &&
      order_mode != TRIO_MAP_MEM_SETUP__ORDER_MODE_VAL_STRICT &&
      order_mode != TRIO_MAP_MEM_SETUP__ORDER_MODE_VAL_REL_ORD)
    return GXIO_ERR_INVAL;

  // We need to use a separate mem-map region for an inbound
  // window to direct-map the low 4GB, to enable 32-bit PCI devices
  // access the physical memory that doesn't overlap with the PCI MMIO
  // space below 4GB.
  if (node == 0)
  {
    int direct_map;

    direct_map = handle_gxio_trio_alloc_memory_maps(ts, svc_dom, 1, 0, 0);
    if (direct_map < 0)
      return (GXIO_TRIO_ERR_NO_MEMORY_MAP);

    // There's a four word stride for each map-mem register set.
    size_t reg_offset = 4 * sizeof(uint64_t) * direct_map;

    TRIO_MAP_MEM_BASE_t base = {{
        .addr = 0,
      }};
    cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_MEM_BASE__FIRST_WORD + reg_offset,
           base.word);

    // Strictly speaking, the limit should be set to the base of the 
    // PCI MMIO space, but this is ok to include it because
    // there won't be ingress access in this range, i.e. the inbound
    // window doesn't overlap with the PCI MMIO address range.
    TRIO_MAP_MEM_LIM_t lim = {{
        .addr = ((1ULL << 32) - 1) >> HV_TRIO_PAGE_SHIFT,
      }};
    cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_MEM_LIM__FIRST_WORD + reg_offset,
           lim.word);

    // Specifies the other attributes, and makes the whole set of
    // register writes take effect (via .mac_ena).  
    TRIO_MAP_MEM_SETUP_t setup = {{
        .mac_ena = (1 << mac),
        .order_mode = order_mode,
        .int_ena = 0,
#ifndef USE_IOMMU_FOR_RC
        .use_mmu = 0,
#else
        .use_mmu = 1,
#endif
        .va = 0,
        .asid = asid,
        .int_mode = 0,
      }};
    cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_MEM_SETUP__FIRST_WORD + reg_offset,
           setup.word);

    // Record the base bus address and VA for the mem-map region that maps the
    // whole CPA space, to be used below. 
    ts->cpa_map_va_base = va;
    ts->cpa_map_bus_base = bus_address;
  }

  //
  // When the last memory controller info is passed in, we know
  // how large the CPA space is and can use this info to set up
  // the mapping window, after allocating a mem-map region here.
  //
  if (node == m_nodes - 1)
  {
    int cpa_map;

    cpa_map = handle_gxio_trio_alloc_memory_maps(ts, svc_dom, 1, 0, 0);
    if (cpa_map < 0)
      return (GXIO_TRIO_ERR_NO_MEMORY_MAP);

    // There's a four word stride for each map-mem register set.
    size_t reg_offset = 4 * sizeof(uint64_t) * cpa_map;

    // Specifies the starting bus address (4KB aligned).
    TRIO_MAP_MEM_BASE_t base = {{
        .addr = ts->cpa_map_bus_base >> HV_TRIO_PAGE_SHIFT,
      }};
    cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_MEM_BASE__FIRST_WORD + reg_offset,
           base.word);

    // Specifies the last address (4KB - 1 aligned).
    TRIO_MAP_MEM_LIM_t lim = {{
        .addr = (bus_address + size - 1) >> HV_TRIO_PAGE_SHIFT,
      }};
    cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_MEM_LIM__FIRST_WORD + reg_offset,
           lim.word);

    // Specifies the other attributes, and makes the whole set of
    // register writes take effect (via .mac_ena).  
    TRIO_MAP_MEM_SETUP_t setup = {{
        .mac_ena = (1 << mac),
        .order_mode = order_mode,
        .int_ena = 0,
#ifndef USE_IOMMU_FOR_RC
        .use_mmu = 0,
#else
        .use_mmu = 1,
#endif
        .va = ts->cpa_map_va_base >> HV_TRIO_PAGE_SHIFT,
        .asid = asid,
        .int_mode = 0,
      }};
    cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_MEM_SETUP__FIRST_WORD + reg_offset,
           setup.word);
  }

  if (!ts->rc_phys_mem_mapped[node])
  {
#ifndef USE_IOMMU_FOR_RC
    // Verify there is an IOTLB entry available.
    if (ts->iotlb_entries_used[asid] == TRIO_NUM_TLBS_PER_ASID)
      return GXIO_ERR_IOTLB_ENTRY;

    // Get next available entry for this asid.
    int entry = ts->iotlb_entries_used[asid]++;

    // NOTE: We implicitly modify "is_attr" below.
    // ISSUE: We could instead use "TRIO_TLB_TABLE__FIRST_WORD" below,
    // and explicitly modify "is_attr" between the two uses.

    TRIO_TLB_TABLE_t table = {{
        .is_attr = 0,
        .entry = entry,
        .asid = asid,
      }};

    // Use a single TLB entry to map all the PA under one mshim.
    // Make sure the TLB entry can cover the memory size.
    assert(MSH_MAX_SIZE_SHIFT - 12 <= 28);

    PA phys_addr;
    uint32_t overlap = drv_cpa2pa(va, HV_PAGE_SIZE_LARGE, &phys_addr);
    if (overlap)
    {
      panic("I/O TLB: drv_cpa2pa() err %#x, CPA %#lx\n", overlap, va);
    }

    TRIO_TLB_ENTRY_ADDR_t addr = {{
        .pfn = phys_addr >> 12,
        .vpn = va >> 12,
      }};
    cfg_wr(ts->shim_pos.word, 0,
           TRIO_TLB_ENTRY_ADDR__FIRST_WORD + table.word, addr.word);

    TRIO_TLB_ENTRY_ATTR_t attr = {{
        .vld = 1,
        .ps = MSH_MAX_SIZE_SHIFT - 12,
        .home_mapping = 0,
        .loc_y_or_offset = 0,
        // Use all the AMT.
        .loc_x_or_mask = TRIO_MMU_TABLE__LOC_X_OR_MASK_RMASK,
        //.lru = UNUSED
      }};
    cfg_wr(ts->shim_pos.word, 0,
           TRIO_TLB_ENTRY_ATTR__FIRST_WORD + table.word, attr.word);
#else
    // Set up the I/O MMU entries mapping this region.
    // Set the page size for all entries in the MMU table.
    if (!ts->mmu_page_size_set)
    {
      TRIO_MMU_CTL_t setup = {{
          .ps = TRIO_MMU_PG_SIZE_ORDER - 12,
        }};
      cfg_wr(ts->shim_pos.word, 0, TRIO_MMU_CTL, setup.word);

      ts->mmu_page_size_set = 1;
    }

    unsigned long va_base = va;
    unsigned long va_limit = va + size;
    while (va_base < va_limit)
    {
      PA phys_addr;
      uint32_t overlap;

      overlap = drv_cpa2pa(va_base, HV_PAGE_SIZE_LARGE, &phys_addr);
      if (overlap)
      {
        panic("I/O MMU: drv_cpa2pa() failure, CPA %#lx\n", va_base);
      }

      TRIO_MMU_TABLE_t pte = {{
          .vld = 1,
          .pfn = phys_addr >> 12,
          .entry_sel = va_base >> TRIO_MMU_PG_SIZE_ORDER,
          .loc_y_or_offset = 0,
          // Use all the AMT.
          .loc_x_or_mask = TRIO_MMU_TABLE__LOC_X_OR_MASK_RMASK,
        }};
      
      cfg_wr(ts->shim_pos.word, 0, TRIO_MMU_TABLE, pte.word);

      va_base += 1 << TRIO_MMU_PG_SIZE_ORDER;
    }

#ifdef IOMMU_DEBUG
    for (int i = 0; i < (1 << TRIO_MMU_TABLE__ENTRY_SEL_WIDTH); i++)
    {
      TRIO_MMU_TABLE_t pte = {{
          .nw = 1,
          .entry_sel = i,
        }};

      cfg_wr(ts->shim_pos.word, 0, TRIO_MMU_TABLE, pte.word);

      pte.word = cfg_rd(ts->shim_pos.word, 0, TRIO_MMU_TABLE);

      if (pte.vld)
        printf("MMU entry %d: %#lx\n", i, (unsigned long)pte.word);
    }
#endif
#endif
    ts->rc_phys_mem_mapped[node] = 1;
  }

  return 0;
}

int
handle_gxio_trio_enable_mmi(trio_state_t* ts, int svc_dom, 
                            int bind_cpu_x, int bind_cpu_y, 
                            int bind_interrupt, int bind_event,
                            unsigned int map, unsigned int mode)
{
  // Validate event.
  if (bind_event < 0 || bind_event > 31)
    return GXIO_ERR_INVAL;

  // Verify "map" is legal and has been allocated.
  if (bad_resource(map, ts->svc_dom_resources[svc_dom].map_mem_mask,
                   TRIO_NUM_MAP_MEM_REGIONS))    
    return GXIO_TRIO_ERR_BAD_MEMORY_MAP;
 
  // Verify legal interrupt mode.
  if (mode != TRIO_MAP_MEM_SETUP__INT_MODE_VAL_LEVEL &&
      mode != TRIO_MAP_MEM_SETUP__INT_MODE_VAL_EDGE &&
      mode != TRIO_MAP_MEM_SETUP__INT_MODE_VAL_ASSERT &&
      mode != TRIO_MAP_MEM_SETUP__INT_MODE_VAL_DEASSERT)
    return GXIO_ERR_INVAL;

  // Setup TRIO_INT_BIND register in the MMIO space for MAP_MEM only.
  unsigned int tileid = DRV_COORDS_TO_TILE_ID(bind_cpu_x, bind_cpu_y);  

  TRIO_INT_BIND_t binding_setup = {{
      .enable = 1,
      .mode = (mode == TRIO_MAP_MEM_SETUP__INT_MODE_VAL_LEVEL) ? 0: 1,
      .tileid = tileid,
      .int_num = bind_interrupt,
      .evt_num = bind_event,
      .vec_sel = TRIO_INT_BIND__VEC_SEL_VAL_MAP_MEM,
      .bind_sel = map,
      .nw = 0,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_INT_BIND, binding_setup.word);

  // Access TRIO_MAP_MEM_SETUP register to enable the MAP_MEM interrupts and 
  // setup interrupt trigger mode, leave other bits un-touched.
  // Stride in four-word step into particular mem_map region.
  size_t reg_offset = 4 * sizeof(uint64_t) * map;
  
  TRIO_MAP_MEM_SETUP_t setup;
  setup.word = cfg_rd(ts->shim_pos.word, 0, 
                      TRIO_MAP_MEM_SETUP__FIRST_WORD + reg_offset);
 
  setup.int_ena = 1;
  setup.int_mode = mode;

  cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_MEM_SETUP__FIRST_WORD + reg_offset,
         setup.word);

  // NOTE that we can still get the MAP_MEM interrupts dispatched here because
  // TRIO_MAP_MEM_REG_INT3 is reset as 0x0000, which means all those 16-bit
  // interrupt vectors are masked.
 
  return 0;
}

int
handle_gxio_trio_mask_mmi_aux(trio_state_t* ts, int svc_dom, 
                              unsigned int map, unsigned int mask)
{
  // Verify "map" is legal and has been allocated.
  if (bad_resource(map, ts->svc_dom_resources[svc_dom].map_mem_mask,
                   TRIO_NUM_MAP_MEM_REGIONS))    
    return GXIO_TRIO_ERR_BAD_MEMORY_MAP;

  // Verify "mask" is legal.
  if (mask > TRIO_MAP_MEM_REG_INT3__INT_VEC_MASK)
    return GXIO_ERR_INVAL;

  // Setup the target MMIO address for interrupt vector mask register, i.e.
  // TRIO_MAP_MEM_REG_INT3. 
  TRIO_MAP_MEM_REGION_ADDR_t reg_offset;
  reg_offset.region = TRIO_MMIO_ADDRESS_SPACE__REGION_VAL_MAP_MEM;
  reg_offset.reg_sel = 3;
  reg_offset.map_sel = map;

  // Mask MAP_MEM interrupts.
  TRIO_MAP_MEM_REG_INT3_t setup;
  setup.int_vec = ~mask & TRIO_MAP_MEM_REG_INT3__INT_VEC_MASK;

  cfg_wr(ts->shim_pos.word, 0, reg_offset.word, setup.word);

  return 0;
}

int
handle_gxio_trio_unmask_mmi_aux(trio_state_t* ts, int svc_dom, 
                                unsigned int map, unsigned int mask)
{
  // Verify "map" is legal and has been allocated.
  if (bad_resource(map, ts->svc_dom_resources[svc_dom].map_mem_mask,
                   TRIO_NUM_MAP_MEM_REGIONS))    
    return GXIO_TRIO_ERR_BAD_MEMORY_MAP;

  // Verify "mask" is legal.
  if (mask > TRIO_MAP_MEM_REG_INT3__INT_VEC_MASK)
    return GXIO_ERR_INVAL;

  // Setup the target MMIO address for interrupt vector mask register, i.e.
  // TRIO_MAP_MEM_REG_INT3. 
  TRIO_MAP_MEM_REGION_ADDR_t reg_offset;
  reg_offset.region = TRIO_MMIO_ADDRESS_SPACE__REGION_VAL_MAP_MEM;
  reg_offset.reg_sel = 3;
  reg_offset.map_sel = map;

  // Unmask MAP_MEM interrupts.
  TRIO_MAP_MEM_REG_INT3_t setup;
  setup.int_vec = mask & TRIO_MAP_MEM_REG_INT3__INT_VEC_MASK;

  cfg_wr(ts->shim_pos.word, 0, reg_offset.word, setup.word);
  
  return 0;
} 

int
handle_gxio_trio_read_mmi_bits_aux(trio_state_t* ts, int svc_dom, 
                                   unsigned int map)
{
  // Verify "map" is legal and has been allocated.
  if (bad_resource(map, ts->svc_dom_resources[svc_dom].map_mem_mask,
                   TRIO_NUM_MAP_MEM_REGIONS))    
    return GXIO_TRIO_ERR_BAD_MEMORY_MAP;
 
  // Setup the target MMIO address for interrupt vector state register, i.e.
  // TRIO_MAP_MEM_REG_INT0. 
  TRIO_MAP_MEM_REGION_ADDR_t reg_offset;
  reg_offset.region = TRIO_MMIO_ADDRESS_SPACE__REGION_VAL_MAP_MEM;
  reg_offset.reg_sel = 0;
  reg_offset.map_sel = map;

  // Access the TRIO_MAP_MEM_REG_INT0 register of this memory map region.
  TRIO_MAP_MEM_REG_INT0_t result;
  result.word = cfg_rd(ts->shim_pos.word, 0, reg_offset.word); 
  
  return result.int_vec; 
}

int
handle_gxio_trio_write_mmi_bits_aux(trio_state_t* ts, int svc_dom, 
                                    unsigned int map, unsigned int bits,
                                    unsigned int mode)
{
  // Verify "map" is legal and has been allocated.
  if (bad_resource(map, ts->svc_dom_resources[svc_dom].map_mem_mask,
                   TRIO_NUM_MAP_MEM_REGIONS))    
    return GXIO_TRIO_ERR_BAD_MEMORY_MAP;

  // Verify "bits" is legal. Since all INT0 to INT3 has the same format,
  // we use INT0 structure simply.
  if (bits > TRIO_MAP_MEM_REG_INT0__INT_VEC_MASK)
    return GXIO_ERR_INVAL;
  
  // Verify "mode" is legal.
  if (mode > 2)
    return GXIO_ERR_INVAL;

  // Setup the target MMIO address for interrupt vector registers.
  TRIO_MAP_MEM_REGION_ADDR_t reg_offset;
  reg_offset.region = TRIO_MMIO_ADDRESS_SPACE__REGION_VAL_MAP_MEM;
  reg_offset.map_sel = map;
  reg_offset.reg_sel = mode;

  // Write MAP_MEM interrupt register. Since all INT0 to INT3 has the same
  // format, we sue INT0 structure simply.
  TRIO_MAP_MEM_REG_INT0_t setup;
  setup.int_vec = bits & TRIO_MAP_MEM_REG_INT0__INT_VEC_MASK;

  cfg_wr(ts->shim_pos.word, 0, reg_offset.word, setup.word);
  
  return 0;
}

int
handle_gxio_trio_free_scatter_queue_aux(trio_state_t* ts, int svc_dom,
                                        unsigned int queue)
{
  trio_resources_t* svc_dom_resources = &ts->svc_dom_resources[svc_dom];

  if (bad_resource(queue, ts->svc_dom_resources[svc_dom].sq_mask,
                   TRIO_NUM_MAP_SQ_REGIONS))
    return GXIO_TRIO_ERR_BAD_SCATTER_QUEUE;

  svc_dom_resources->sq_mask &= ~(1 << queue);
  ts->resources.sq_mask &= ~(1 << queue);
  flush_sq(ts, queue);
  spin_unlock(&ts->lock);

  if (drv_deny_mmio_access(ts->shim_pos, HV_TRIO_SQ_OFFSET(queue),
                           HV_TRIO_SQ_SIZE, CLIENTNO))
    WARN("Unexpected sq deny_mmio_access() failure\n");

  spin_lock(&ts->lock);
  
  return 0;
}

int
handle_gxio_trio_init_scatter_queue_aux(trio_state_t* ts, int svc_dom,
                                        unsigned int queue, uint64_t size,
                                        unsigned int asid, unsigned int mac,
                                        uint64_t bus_address,
                                        unsigned int order_mode)
{
  if (bad_resource(queue, ts->svc_dom_resources[svc_dom].sq_mask,
                   TRIO_NUM_MAP_SQ_REGIONS))
    return GXIO_TRIO_ERR_BAD_SCATTER_QUEUE;
  if (bad_resource(asid, ts->svc_dom_resources[svc_dom].asid_mask,
                   TRIO_NUM_ASIDS))
    return GXIO_TRIO_ERR_BAD_ASID;

  if (bus_address & (HV_TRIO_PAGE_SIZE - 1) ||
      size & (HV_TRIO_PAGE_SIZE - 1))
    return GXIO_ERR_ALIGNMENT;

  if (order_mode != TRIO_MAP_SQ_SETUP__ORDER_MODE_VAL_UNORDERED &&
      order_mode != TRIO_MAP_SQ_SETUP__ORDER_MODE_VAL_STRICT &&
      order_mode != TRIO_MAP_SQ_SETUP__ORDER_MODE_VAL_REL_ORD)
    return GXIO_ERR_INVAL;

  // There's a four word stride for each scatter queue register set.
  size_t reg_offset = 4 * sizeof(uint64_t) * queue;

  // Specifies the starting bus address (4KB aligned).
  TRIO_MAP_SQ_BASE_t base = {{
      .addr = bus_address >> HV_TRIO_PAGE_SHIFT,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_SQ_BASE__FIRST_WORD + reg_offset,
         base.word);

  // Specifies the last address (4KB - 1 aligned).
  TRIO_MAP_SQ_LIM_t lim = {{
      .addr = (bus_address + size - 1) >> HV_TRIO_PAGE_SHIFT,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_SQ_LIM__FIRST_WORD + reg_offset,
         lim.word);

  // Specifies the other attributes, and makes the whole set of
  // register writes take effect (via .mac_ena).  
  TRIO_MAP_SQ_SETUP_t setup = {{
      .mac_ena = (1 << mac),
      .order_mode = order_mode,
      .asid = asid,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_SQ_SETUP__FIRST_WORD + reg_offset,
         setup.word);

  return 0;
}

int
handle_gxio_trio_enable_sqi(trio_state_t* ts, int svc_dom,
                            int bind_cpu_x,
                            int bind_cpu_y,
                            int bind_interrupt,
                            int bind_event,
                            unsigned int queue)
{
  // Validate event.
  if (bind_event < 0 || bind_event > 31)
    return GXIO_ERR_INVAL;

  // Verify "queue" is legal and has been allocated.
  if (bad_resource(queue, ts->svc_dom_resources[svc_dom].sq_mask,
                   TRIO_NUM_MAP_SQ_REGIONS))
    return GXIO_TRIO_ERR_BAD_SCATTER_QUEUE;

  // Setup TRIO_INT_BIND register in the MMIO space for scatter queue's
  // doorbell interrupts.
  // NOTE that for doorbell interrupts, no need to setup INT_ENA bit in a
  // particular SQ region.
  unsigned int tileid = DRV_COORDS_TO_TILE_ID(bind_cpu_x, bind_cpu_y);

  TRIO_INT_BIND_t binding_setup = {{
      .enable = 1,
      .mode = 1,    // One time trigger for SQ doorbell interrupts.
      .tileid = tileid,
      .int_num = bind_interrupt,
      .evt_num = bind_event,
      .vec_sel = TRIO_INT_BIND__VEC_SEL_VAL_MAP_SQ,
      .bind_sel = queue,
      .nw = 0,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_INT_BIND, binding_setup.word);

  ts->resources.sq_intr_mask |= (1 << queue);

  return 0;
}

int
handle_gxio_trio_free_pio_region_aux(trio_state_t* ts, int svc_dom,
                                     unsigned int pio_region)
{
  trio_resources_t* svc_dom_resources = &ts->svc_dom_resources[svc_dom];

  if (bad_resource(pio_region, ts->svc_dom_resources[svc_dom].pio_mask,
                   TRIO_NUM_TPIO_REGIONS))
    return GXIO_TRIO_ERR_BAD_PIO;

  spin_unlock(&ts->lock);

  if (drv_deny_mmio_access(ts->shim_pos, HV_TRIO_PIO_OFFSET(pio_region),
                           HV_TRIO_PIO_SIZE, CLIENTNO))
    WARN("Unexpected sq deny_mmio_access() failure\n");

  spin_lock(&ts->lock);

  svc_dom_resources->pio_mask &= ~(1 << pio_region);
  ts->resources.pio_mask &= ~(1 << pio_region);

  // Disable this PIO region.
  cfg_wr(ts->shim_pos.word, 0, TRIO_TILE_PIO_REGION_SETUP__FIRST_WORD +
         sizeof(TRIO_TILE_PIO_REGION_SETUP_t) * pio_region, 0);

  return 0;
}

/** Initialize a PIO region. */
int
handle_gxio_trio_init_pio_region_aux(trio_state_t* ts, int svc_dom,
                                     unsigned int pio_region, unsigned int mac,
                                     uint32_t bus_address_hi,
                                     unsigned int flags)
{  
  if (bad_resource(pio_region, ts->svc_dom_resources[svc_dom].pio_mask,
                   TRIO_NUM_TPIO_REGIONS))
    return GXIO_TRIO_ERR_BAD_PIO;

  // Default configuration is an unordered memory space region.
  TRIO_TILE_PIO_REGION_SETUP_t setup = {{
      .ena = 1,
      .type = TRIO_TILE_PIO_REGION_SETUP__TYPE_VAL_MEM,
      .ord = 0,
      .mac = mac,
      .tc = 0,
      .vfunc_ena = 0,
      .vfunc = 0,
      .addr = bus_address_hi
    }};

  // Handle ordered, address space, traffic class, and vfunc flags.
  if (flags & HV_TRIO_PIO_FLAG_ORDERED)
    setup.ord = 1;

  switch(flags & HV_TRIO_PIO_FLAG_SPACE_MASK)
  {
  case HV_TRIO_PIO_FLAG_CONFIG_SPACE:
    setup.type = TRIO_TILE_PIO_REGION_SETUP__TYPE_VAL_CFG;
    break;
  case HV_TRIO_PIO_FLAG_IO_SPACE:
    setup.type = TRIO_TILE_PIO_REGION_SETUP__TYPE_VAL_IO;
    break;
  }

  int traffic_class =
    (flags >> HV_TRIO_FLAG_TC_SHIFT) & HV_TRIO_FLAG_TC_RMASK;
  if (traffic_class)
    setup.tc = traffic_class - 1;

  int vfunc =
    (flags >> HV_TRIO_FLAG_VFUNC_SHIFT) & HV_TRIO_FLAG_VFUNC_RMASK;
  if (vfunc)
  {
    setup.vfunc = vfunc - 1;
    setup.vfunc_ena = 1;
  }

  // Commit the configuration.
  cfg_wr(ts->shim_pos.word, 0,
         TRIO_TILE_PIO_REGION_SETUP__FIRST_WORD + sizeof(setup) * pio_region,
         setup.word);
  
  return 0;
}

/** Initialize a push DMA region. */
int
handle_gxio_trio_init_push_dma_ring_aux(trio_state_t* ts, int svc_dom,
                                        PA mem_pa, size_t mem_size,
                                        struct iorpc_mem_attr mem_attr,
                                        unsigned int ring, unsigned int mac,
                                        unsigned int asid,
                                        unsigned int flags)
{
  if (bad_resource(ring, ts->svc_dom_resources[svc_dom].push_dma_mask,
                   TRIO_NUM_PUSH_DMA_RINGS))
    return GXIO_TRIO_ERR_BAD_PUSH_DMA_RING;
  if (bad_resource(asid, ts->svc_dom_resources[svc_dom].asid_mask,
                   TRIO_NUM_ASIDS))
    return GXIO_TRIO_ERR_BAD_ASID;

  int per = sizeof(TRIO_DMA_DESC_t);
  // Aka "gxio_mpipe_edma_ring_entries_t".
  int entries_enum;
  if (mem_size == 512 * per)
    entries_enum = 0;
  else if (mem_size == 2048 * per)
    entries_enum = 1;
  else if (mem_size == 8192 * per)
    entries_enum = 2;
  else if (mem_size == 65536 * per)
    entries_enum = 3;
  else
    return GXIO_ERR_INVAL_MEMORY_SIZE;

  // Zero out the descriptor ring's Current Head/GNUM.
  TRIO_PUSH_DMA_DM_INIT_CTL_t dm_ctl = {{
      .idx = ring,
      .struct_sel = TRIO_PUSH_DMA_DM_INIT_CTL__STRUCT_SEL_VAL_HEAD,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_DM_INIT_CTL, dm_ctl.word);
  cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_DM_INIT_DAT, 0);

  // Configure the descriptor ring's memory address.
  dm_ctl.struct_sel = TRIO_PUSH_DMA_DM_INIT_CTL__STRUCT_SEL_VAL_SETUP;
  cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_DM_INIT_CTL, dm_ctl.word);




  TRIO_PUSH_DMA_DM_INIT_DAT_SETUP_t setup = {{

      .base_pa = mem_pa >> 10,
      .hfh = mem_attr.hfh,
      .tileid = DRV_COORDS_TO_TILE_ID(mem_attr.lotar_x, mem_attr.lotar_y),
      .ring_size = entries_enum,
      .hunt = 1,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_DM_INIT_DAT, setup.word);

  // Assign asid.
  TRIO_PUSH_DMA_RG_INIT_CTL_t rg_ctl = {{
      .struct_sel = TRIO_PUSH_DMA_RG_INIT_CTL__STRUCT_SEL_VAL_ASID,
      .idx = ring
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_RG_INIT_CTL, rg_ctl.word);
  



  TRIO_PUSH_DMA_RG_INIT_DAT_ASID_t dat_asid = {{

      .asid = asid,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_RG_INIT_DAT, dat_asid.word);

  // Assign mac, traffic class, and virtual function.  The latter two
  // are specified by flag bits that are either 0 for "use default",
  // or val + 1 to indicate a desired valued.
  rg_ctl.struct_sel = TRIO_PUSH_DMA_RG_INIT_CTL__STRUCT_SEL_VAL_MAP;
  cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_RG_INIT_CTL, rg_ctl.word);
  
  int traffic_class =
    (flags >> HV_TRIO_FLAG_TC_SHIFT) & HV_TRIO_FLAG_TC_RMASK;
  int vfunc =
    (flags >> HV_TRIO_FLAG_VFUNC_SHIFT) & HV_TRIO_FLAG_VFUNC_RMASK;
  TRIO_PUSH_DMA_RG_INIT_DAT_MAP_t map = {{
      .mac = mac,
      .tc = traffic_class ? traffic_class - 1 : 0,
      .vfunc = vfunc ? vfunc - 1 : 0,
      .vfunc_ena = vfunc ? 1 : 0,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_PUSH_DMA_RG_INIT_DAT, map.word);
  
  return 0;
}

int
handle_gxio_trio_enable_push_dma_isr(trio_state_t* ts, int svc_dom,
                                     int bind_cpu_x, int bind_cpu_y,
                                     int bind_interrupt,
                                     int bind_event,
                                     unsigned int ring)
{
  // Validate event.
  if (bind_event < 0 || bind_event > 31)
    return GXIO_ERR_INVAL;

  // Verify "ring" is legal and has been allocated.
  if (bad_resource(ring, ts->svc_dom_resources[svc_dom].push_dma_mask,
                   TRIO_NUM_PUSH_DMA_RINGS))
    return GXIO_TRIO_ERR_BAD_PUSH_DMA_RING;

  // Setup TRIO_INT_BIND register in the MMIO space for push dma only.
  unsigned int tileid = DRV_COORDS_TO_TILE_ID(bind_cpu_x, bind_cpu_y);

  TRIO_INT_BIND_t binding_setup = {{
      .enable = 1,
      .mode = 1,
      .tileid = tileid,
      .int_num = bind_interrupt,
      .evt_num = bind_event,
      .vec_sel = TRIO_INT_BIND__VEC_SEL_VAL_PUSH_DMA,
      .bind_sel = ring,
      .nw = 0,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_INT_BIND, binding_setup.word);

  return 0;
}

int
handle_gxio_trio_init_pull_dma_ring_aux(trio_state_t* ts, int svc_dom,
                                        PA mem_pa, size_t mem_size,
                                        struct iorpc_mem_attr mem_attr,
                                        unsigned int ring, unsigned int mac,
                                        unsigned int asid,
                                        unsigned int flags)
{
  if (bad_resource(ring, ts->svc_dom_resources[svc_dom].pull_dma_mask,
                   TRIO_NUM_PULL_DMA_RINGS))
    return GXIO_TRIO_ERR_BAD_PULL_DMA_RING;
  if (bad_resource(asid, ts->svc_dom_resources[svc_dom].asid_mask,
                   TRIO_NUM_ASIDS))
    return GXIO_TRIO_ERR_BAD_ASID;

  int per = sizeof(TRIO_DMA_DESC_t);
  // Aka "gxio_mpipe_edma_ring_entries_t".
  int entries_enum;
  if (mem_size == 512 * per)
    entries_enum = 0;
  else if (mem_size == 2048 * per)
    entries_enum = 1;
  else if (mem_size == 8192 * per)
    entries_enum = 2;
  else if (mem_size == 65536 * per)
    entries_enum = 3;
  else
    return GXIO_ERR_INVAL_MEMORY_SIZE;

  // Zero out the descriptor ring's Current Head/GNUM.
  TRIO_PULL_DMA_DM_INIT_CTL_t dm_ctl = {{
      .idx = ring,
      .struct_sel = TRIO_PULL_DMA_DM_INIT_CTL__STRUCT_SEL_VAL_HEAD,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_PULL_DMA_DM_INIT_CTL, dm_ctl.word);
  cfg_wr(ts->shim_pos.word, 0, TRIO_PULL_DMA_DM_INIT_DAT, 0);

  // Configure the descriptor ring's memory address.
  dm_ctl.struct_sel = TRIO_PULL_DMA_DM_INIT_CTL__STRUCT_SEL_VAL_SETUP;
  cfg_wr(ts->shim_pos.word, 0, TRIO_PULL_DMA_DM_INIT_CTL, dm_ctl.word);




  TRIO_PULL_DMA_DM_INIT_DAT_SETUP_t setup = {{

      .base_pa = mem_pa >> 10,
      .hfh = mem_attr.hfh,
      .tileid = DRV_COORDS_TO_TILE_ID(mem_attr.lotar_x, mem_attr.lotar_y),
      .ring_size = entries_enum,
      .hunt = 1,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_PULL_DMA_DM_INIT_DAT, setup.word);

  // Assign asid, mac, traffic class, and virtual function.  The
  // latter two are specified by flag bits that are either 0 for "use
  // default", or val + 1 to indicate a desired valued.
  TRIO_PULL_DMA_RG_INIT_CTL_t rg_ctl = {{
      .idx = ring
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_PULL_DMA_RG_INIT_CTL, rg_ctl.word);
  
  int traffic_class =
    (flags >> HV_TRIO_FLAG_TC_SHIFT) & HV_TRIO_FLAG_TC_RMASK;
  int vfunc =
    (flags >> HV_TRIO_FLAG_VFUNC_SHIFT) & HV_TRIO_FLAG_VFUNC_RMASK;
  TRIO_PULL_DMA_RG_INIT_DAT_t rg_dat = {{
      .asid = asid,
      .mac = mac,
      .mrs = 6, 
      .tc = traffic_class ? traffic_class - 1 : 0,
      .vfunc = vfunc ? vfunc - 1 : 0,
      .vfunc_ena = vfunc ? 1 : 0,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_PULL_DMA_RG_INIT_DAT, rg_dat.word);

  return 0;
}

int
handle_gxio_trio_enable_pull_dma_isr(trio_state_t* ts, int svc_dom,
                                     int bind_cpu_x, int bind_cpu_y,
                                     int bind_interrupt,
                                     int bind_event,
                                     unsigned int ring)
{
  // Validate event.
  if (bind_event < 0 || bind_event > 31)
    return GXIO_ERR_INVAL;

  // Verify "ring" is legal and has been allocated.
  if (bad_resource(ring, ts->svc_dom_resources[svc_dom].pull_dma_mask,
                   TRIO_NUM_PULL_DMA_RINGS))
    return GXIO_TRIO_ERR_BAD_PULL_DMA_RING;

  // Setup TRIO_INT_BIND register in the MMIO space for pull dma only.
  unsigned int tileid = DRV_COORDS_TO_TILE_ID(bind_cpu_x, bind_cpu_y);

  TRIO_INT_BIND_t binding_setup = {{
      .enable = 1,
      .mode = 1,
      .tileid = tileid,
      .int_num = bind_interrupt,
      .evt_num = bind_event,
      .vec_sel = TRIO_INT_BIND__VEC_SEL_VAL_PULL_DMA,
      .bind_sel = ring,
      .nw = 0,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_INT_BIND, binding_setup.word);

  return 0;
}

int
handle_gxio_trio_get_port_property(trio_state_t* ts, int svc_dom,
				   struct pcie_trio_ports_property* trio_ports)
{
  memset(trio_ports, 0, sizeof (*trio_ports));

  for (int mac = 0; mac < TILEGX_TRIO_PCIES; mac++)
  {
    union port_inst {
      bi_inst_t instance;
      struct bi_port_inst port;
    } port_inst = {
      .port.port = mac,
      .port.shim = ts->virt_instance,
    };

    bi_ptr_t bp;

    if (bi_getparam(BI_TYPE_PCIE_PORT_CFG, port_inst.instance, &bp, NULL)
        != BI_NULL)
    {
      struct bi_pcie_port_cfg* port_cfg = bp;

      trio_ports->ports[mac].allow_rc = port_cfg->allow_rc;
      trio_ports->ports[mac].allow_ep = port_cfg->allow_ep;
      trio_ports->ports[mac].allow_sio = port_cfg->allow_sio;
      trio_ports->ports[mac].allow_x1 = port_cfg->allow_x1;
      trio_ports->ports[mac].allow_x2 = port_cfg->allow_x2;
      trio_ports->ports[mac].allow_x4 = port_cfg->allow_x4;
      trio_ports->ports[mac].allow_x8 = port_cfg->allow_x8;
      trio_ports->ports[mac].removable = port_cfg->removable;
    }
  }

  trio_ports->is_gx72 = ts->is_gx72;

  return 0;
}

int
handle_gxio_trio_config_legacy_intr(trio_state_t* ts, int svc_dom,
				    int inter_x, int inter_y,
				    int inter_ipi, int inter_event,
				    unsigned int mac, unsigned int intx)
{
  // Deny user-space access.
  if (inter_ipi < CLIENT_PL)
    return (HV_EINVAL);
  
  TRIO_INT_BIND_t binding_setup =
  {{
    .vec_sel = TRIO_INT_BIND__VEC_SEL_VAL_MAC,
    .evt_num = inter_event,
    .int_num = inter_ipi,
    .tileid = DRV_COORDS_TO_TILE_ID(inter_x, inter_y),
    .mode = 0,              /* not used for MAC interrupts */
    .enable = 1,
  }};

  binding_setup.bind_sel = mac * 32 +
      TRIO_PCIE_INTFC_MAC_INT_STS__INT_LEVEL_SHIFT + intx;

  cfg_wr(ts->shim_pos.word, 0, TRIO_INT_BIND, binding_setup.word);

  return 0;
}

/** Configure the MSI using a Map Memory region. */
static int
trio_config_msi_mem_map(trio_state_t* ts,
                        int inter_x, int inter_y,
                        int inter_ipi, int inter_event,
                        unsigned int mac, unsigned int mem_map,
			uint64_t mem_map_base,
			uint64_t mem_map_limit,
			unsigned int asid)
{
  TRIO_INT_BIND_t binding_setup =
  {{
    .bind_sel = mem_map,
    .vec_sel = TRIO_INT_BIND__VEC_SEL_VAL_MAP_MEM,
    .evt_num = inter_event,
    .int_num = inter_ipi,
    .tileid = DRV_COORDS_TO_TILE_ID(inter_x, inter_y),
    .mode = 1,
    .enable = 1,
  }};

  cfg_wr(ts->shim_pos.word, 0, TRIO_INT_BIND, binding_setup.word);

  // Configure the Mem-Map region.
  size_t reg_offset = 4 * sizeof(uint64_t) * mem_map;

  // Specifies the starting bus address (4KB aligned).
  TRIO_MAP_MEM_BASE_t base = {{
      .addr = mem_map_base >> HV_TRIO_PAGE_SHIFT,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_MEM_BASE__FIRST_WORD + reg_offset,
         base.word);
  
  // Specifies the last address (4KB - 1 aligned).
  TRIO_MAP_MEM_LIM_t lim = {{
      .addr = mem_map_limit >> HV_TRIO_PAGE_SHIFT,
    }};
  cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_MEM_LIM__FIRST_WORD + reg_offset,
         lim.word); 

  TRIO_MAP_MEM_SETUP_t mem_map_setup = {{
      .mac_ena = (1 << mac),
      .order_mode = TRIO_MAP_MEM_SETUP__ORDER_MODE_VAL_STRICT,
      .int_ena = 1,
      .asid = asid,
      .int_mode = TRIO_MAP_MEM_SETUP__INT_MODE_VAL_EDGE,
    }};

  cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_MEM_SETUP__FIRST_WORD + reg_offset,
         mem_map_setup.word);

  return 0;
}

/** Configure the MSI using a Scatter Queue region. */
static int
trio_config_msi_sq(trio_state_t* ts, int svc_dom,
                   int inter_x, int inter_y,
                   int inter_ipi, int inter_event,
                   unsigned int mac, unsigned int queue,
		   uint64_t sq_base, uint64_t sq_limit,
		   unsigned int asid)
{
  uint64_t sq_region_size;
  int ret;

  sq_region_size = sq_limit - sq_base + 1;
  ret = handle_gxio_trio_init_scatter_queue_aux(ts, svc_dom, queue,
        sq_region_size, asid, mac, sq_base,
        TRIO_MAP_SQ_SETUP__ORDER_MODE_VAL_STRICT);
  if (ret)
    return ret;

  return handle_gxio_trio_enable_sqi(ts, svc_dom, inter_x, inter_y,
                                     inter_ipi, inter_event, queue);
}

int
handle_gxio_trio_config_msi_intr(trio_state_t* ts, int svc_dom,
                                 int inter_x, int inter_y,
                                 int inter_ipi, int inter_event,
                                 unsigned int mac, unsigned int mem_map,
				 uint64_t mem_map_base,
				 uint64_t mem_map_limit,
				 unsigned int asid)
{
  // Deny user-space access.
  if (inter_ipi < CLIENT_PL)
    return (HV_EINVAL);

  if (mem_map < TRIO_NUM_MAP_MEM_REGIONS)
  {
    return trio_config_msi_mem_map(ts, inter_x, inter_y, inter_ipi, inter_event,
                                   mac, mem_map, mem_map_base, mem_map_limit,
                                   asid);
  }
  else
  {
    // We need to use a SQ region to set up a MSI vector.
    return trio_config_msi_sq(ts, svc_dom, inter_x, inter_y,
                              inter_ipi, inter_event, mac,
                              mem_map - TRIO_NUM_MAP_MEM_REGIONS,
                              mem_map_base, mem_map_limit, asid);
  }
}

int
handle_gxio_trio_config_char_intr(trio_state_t* ts, int svc_dom,
                                  int inter_x, int inter_y,
                                  int inter_ipi, int inter_event,
                                  unsigned int mac, 
			 	  unsigned int mem_map,
				  unsigned int push_dma_ring,
				  unsigned int pull_dma_ring,
				  pcie_stream_intr_config_sel_t conf)
{
  TRIO_INT_BIND_t binding_setup =
  {{
    .evt_num = inter_event,
    .int_num = inter_ipi,
    .tileid = DRV_COORDS_TO_TILE_ID(inter_x, inter_y),
    .mode = 1,
    .enable = 1,
  }};

  if (conf == MEM_MAP_SEL)
  {
    binding_setup.bind_sel = mem_map;
    binding_setup.vec_sel = TRIO_INT_BIND__VEC_SEL_VAL_MAP_MEM;
      
    // The memory map region is already init, just enable the interrupt.
    size_t reg_offset = 4 * sizeof(uint64_t) * mem_map;
  
    TRIO_MAP_MEM_SETUP_t setup;
    setup.word = cfg_rd(ts->shim_pos.word, 0, 
                        TRIO_MAP_MEM_SETUP__FIRST_WORD + reg_offset);

    setup.int_ena = 1;
    setup.int_mode = TRIO_MAP_MEM_SETUP__INT_MODE_VAL_EDGE;

    cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_MEM_SETUP__FIRST_WORD + reg_offset,
           setup.word);
  }
  else if (conf == PUSH_DMA_SEL)
  {
    binding_setup.bind_sel = push_dma_ring;
    binding_setup.vec_sel = TRIO_INT_BIND__VEC_SEL_VAL_PUSH_DMA;
  } 
  else if (conf == PULL_DMA_SEL)
  {
    binding_setup.bind_sel = pull_dma_ring;
    binding_setup.vec_sel = TRIO_INT_BIND__VEC_SEL_VAL_PULL_DMA;
  }

  cfg_wr(ts->shim_pos.word, 0, TRIO_INT_BIND, binding_setup.word);

  return 0;
}

int
handle_gxio_trio_set_mps_mrs(trio_state_t* ts, int svc_dom,
                             uint16_t mps, uint16_t mrs, unsigned int mac)
{
  TRIO_MAC_CONFIG_t trio_mac_config;
  size_t reg_offset;

  //
  // Static table of count/cycle ratios pre-calculated for various link
  // width and speed combinations; first array index is gen1/gen2, second
  // is x1/x2/x4/x8.  Minimum bandwidth is on a x1 gen1 link, maximum is 
  // on a x8 gen2 link.  
  //
  int pull_dma_bw_cnt_tbl[2][4] = { {1,  34,  34,  34},
                                    {34,  34,  34,  34} };
  int pull_dma_bw_cyc_tbl[2][4] = { {116,  1023,  511,  255},  
                                    {1023,   511,  255,  127} };
  int bw_speed_sel;
  int bw_width_sel;
  int add_tags=0;

  //
  // Although we're using the EP definitions, these registers are valid for 
  // both EP and RC modes.
  //
  unsigned int mac_reg_offset;
  TRIO_PCIE_EP_LINK_STATUS_t link_status;
  TRIO_PCIE_EP_DEVICE_CONTROL_t device_control;
  TRIO_PCIE_INTFC_PORT_STATUS_t port_status;

  //
  // Calculate PULL_DMA bandwidth limit based on link width and speed.
  //
  mac_reg_offset =
    (TRIO_PCIE_INTFC_PORT_STATUS << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
    (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE << 
     TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
    (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

  port_status.word = cfg_rd(ts->shim_pos.word, 0, mac_reg_offset);
 
  if (port_status.dl_up)
  {
    mac_reg_offset =
      (TRIO_PCIE_EP_LINK_CONTROL <<
       TRIO_CFG_REGION_ADDR__REG_SHIFT) |
      (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_STANDARD <<
       TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
      (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

    //
    // The link_status register is bits 31:16 of link_control since we
    // must do an 8-byte aligned read.
    //
    link_status.word = (cfg_rd(ts->shim_pos.word, 0, mac_reg_offset) >> 16);
     
    for (bw_width_sel = 0; bw_width_sel < 4; bw_width_sel++) 
    {
      if ((link_status.negotiated_link_width >> bw_width_sel) & 1)
        break;
    }
    bw_speed_sel = (link_status.link_speed - 1) & 1;

    //
    // Add tags for pull DMA if extended-tag mode has been enabled.
    //
    mac_reg_offset =
      (TRIO_PCIE_EP_DEVICE_CONTROL << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
      (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_STANDARD <<
       TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
      (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);
    device_control.word = cfg_rd(ts->shim_pos.word, 0, mac_reg_offset);
    add_tags = device_control.ext_tag_field_enable;

    //
    // Gx kernel driver has already disable the EP RX BAR0 Address
    // Mask so that the TRIO sees the full PCI address
    // (not just the BAR offset). This setting
    // applies to both the PF and the VFs. While there is no
    // particular reason this is done here, it is a convenient place
    // because this function is invoked when the EP port is probed.
    //
    // Now we need to apply the full PCIe bus address to RSHIM.
    //
    uint64_t base_addr;

    mac_reg_offset =
      (TRIO_PCIE_EP_BASE_ADDR0 << TRIO_CFG_REGION_ADDR__REG_SHIFT) |
      (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_STANDARD <<
       TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
      (mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);
    base_addr = cfg_rd(ts->shim_pos.word, 0, mac_reg_offset);

    //
    // Only the first probed MAC can reset the RSHIM base address
    // because all MACs on a single TRIO instance share the same
    // register, thus the latter one will overwrite the former value.
    //
    if (cfg_rd(ts->shim_pos.word, 0, TRIO_MAP_RSH_BASE) ==
        TRIO_MAP_RSH_BASE__ADDR_RESET_VAL)
    {
      TRIO_MAP_RSH_BASE_t rsh_base;
      rsh_base.word = 0;
      rsh_base.addr = base_addr >> TRIO_MAP_RSH_BASE__ADDR_SHIFT;

      cfg_wr(ts->shim_pos.word, 0, TRIO_MAP_RSH_BASE, rsh_base.word);
    }
  }
  else
  {
    //
    // If the link is down, just choose the x8/G2 (max BW) setting.
    //
    bw_speed_sel = 1;
    bw_width_sel = 3;
  }

  reg_offset = TRIO_MAC_CONFIG__FIRST_WORD + mac * 8;
  trio_mac_config.word = cfg_rd(ts->shim_pos.word, 0, reg_offset);
  trio_mac_config.mps = mps + 1;
  trio_mac_config.mrs = mrs + 1;

  trio_mac_config.pull_dma_tok_cnt = 
    pull_dma_bw_cnt_tbl[bw_speed_sel][bw_width_sel];
  trio_mac_config.pull_dma_tok_cyc = 
    pull_dma_bw_cyc_tbl[bw_speed_sel][bw_width_sel];
  trio_mac_config.pull_dma_tok_sz = 0;

  cfg_wr(ts->shim_pos.word, 0, reg_offset, trio_mac_config.word);

  if (add_tags && !sim_is_simulator())
  {
    TRIO_PULL_DMA_DIAG_CTL_t pull_dma_diag_ctl;
    TRIO_PULL_DMA_CTL_t pull_dma_ctl;
    uint64_t ctags;

    pull_dma_diag_ctl.word = cfg_rd(ts->shim_pos.word, 0, 
                                    TRIO_PULL_DMA_DIAG_CTL);
    pull_dma_diag_ctl.diag_ctr_sel = 
      TRIO_PULL_DMA_DIAG_CTL__DIAG_CTR_SEL_VAL_NUM_TAGS;
    pull_dma_diag_ctl.diag_ctr_idx = mac;
    cfg_wr(ts->shim_pos.word, 0, TRIO_PULL_DMA_DIAG_CTL, 
           pull_dma_diag_ctl.word);

    ctags = cfg_rd(ts->shim_pos.word, 0, 
                   TRIO_PULL_DMA_DIAG_STS);
    //
    // Only add tags if we're still at the reset value.
    //
    if (ctags == 27)
    {
      pull_dma_ctl.word = cfg_rd(ts->shim_pos.word, 0, TRIO_PULL_DMA_CTL);
      pull_dma_ctl.mac_tag_sel = mac;
      cfg_wr(ts->shim_pos.word, 0, TRIO_PULL_DMA_CTL, pull_dma_ctl.word);
      for (int t=32; t<223; t++)
        cfg_wr(ts->shim_pos.word, 0, TRIO_PULL_DMA_TAG_FREE, t);
    }
  }

  return 0;
}

static int
force_link_up(trio_state_t* ts, int svc_dom, unsigned int mac, int is_rc)
{
  unsigned int mac_pa =
    mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT;
  unsigned int mac_intf_pa = mac_pa |
    (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
     TRIO_CFG_REGION_ADDR__INTFC_SHIFT);
  unsigned int mac_std_pa = mac_pa |
    (TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_STANDARD <<
     TRIO_CFG_REGION_ADDR__INTFC_SHIFT);

  //
  // Get the port_cfg item from the BIB.
  //
  union port_inst {
    bi_inst_t instance;
    struct bi_port_inst port;
  } port_inst = {
    .port.port = mac,
    .port.shim = ts->virt_instance,
  };

  struct bi_pcie_port_cfg* port_cfg;
  bi_ptr_t bp;

  if (bi_getparam(BI_TYPE_PCIE_PORT_CFG, port_inst.instance, &bp, NULL)
      != BI_NULL)
  {
    port_cfg = bp;
  }
  else
  {
    panic("BIB does not have BI_TYPE_PCIE_PORT_CFG for shim %d mac %d\n",
          ts->virt_instance, mac);
  }

  //
  // For GX72, we need to bring the link up if it isn't up already.
  //
  if (ts->is_gx72)
  {
    TRIO_PCIE_INTFC_PORT_STATUS_t port_status =
      { .word = cfg_rd(ts->shim_pos.word, 0,
                       mac_intf_pa | TRIO_PCIE_INTFC_PORT_STATUS) };

    //
    // Bypass SERDES code if the link is already up.
    //
    if (port_status.dl_up)
    {
      //
      // Deassert PERST so the connected device comes out of reset.
      //
      if (is_rc)
        drv_set_signal(port_cfg->perst_sig, DRV_SIGNAL_DEASSERT);

      return 0;
    }
    else if (is_rc)
    {
      TRIO_PCIE_INTFC_PORT_CONFIG_t port_config =
        { .word = cfg_rd(ts->shim_pos.word, 0,
                         mac_intf_pa | TRIO_PCIE_INTFC_PORT_CONFIG) };

      //
      // For Gx72 RC ports that are not configured to train automatically,
      // the PERST was not asserted in trio_init(). So assert it here.
      //
      if (port_config.strap_state !=
          TRIO_PCIE_INTFC_PORT_CONFIG__STRAP_STATE_VAL_AUTO_CONFIG_RC &&
          port_config.strap_state !=
          TRIO_PCIE_INTFC_PORT_CONFIG__STRAP_STATE_VAL_AUTO_CONFIG_RC_G1)
        drv_set_signal(port_cfg->perst_sig,
                       DRV_SIGNAL_INIT | DRV_SIGNAL_ASSERT);
    }
  }

  //
  // Reset the link to clear out state (otherwise the port won't retrain
  // to Gen2 automatically).
  //
  TRIO_PCIE_INTFC_RESET_CTL_t reset_ctl =
  {{
    .auto_mode = 0,
    .reset_pmc = 1,
    .reset_mac = 1,
    .reset_phy = 1,
    .reset_sticky = 1,
    .reset_non_sticky = 1,
  }};

  cfg_wr(ts->shim_pos.word, 0, mac_intf_pa | TRIO_PCIE_INTFC_RESET_CTL,
         reset_ctl.word);

  //
  // Disable training, force the proper link mode.
  //
  TRIO_PCIE_INTFC_PORT_CONFIG_t port_config =
    { .word = cfg_rd(ts->shim_pos.word, 0,
                     mac_intf_pa | TRIO_PCIE_INTFC_PORT_CONFIG) };
  port_config.train_mode =
    TRIO_PCIE_INTFC_PORT_CONFIG__TRAIN_MODE_VAL_TRAIN_DIS;
  port_config.ovd_dev_type = 1;
  port_config.ovd_dev_type_val =
    is_rc ? TRIO_PCIE_INTFC_PORT_CONFIG__OVD_DEV_TYPE_VAL_VAL_RC
          : TRIO_PCIE_INTFC_PORT_CONFIG__OVD_DEV_TYPE_VAL_VAL_EP;
  cfg_wr(ts->shim_pos.word, 0, mac_intf_pa | TRIO_PCIE_INTFC_PORT_CONFIG,
         port_config.word);

  //
  // Re-enable auto-reset.
  //
  reset_ctl = (TRIO_PCIE_INTFC_RESET_CTL_t)
  {{
    .auto_mode = 1,
    .reset_pmc = 1,
    .reset_mac = 1,
    .reset_non_sticky = 1,
  }};

  cfg_wr(ts->shim_pos.word, 0, mac_intf_pa | TRIO_PCIE_INTFC_RESET_CTL,
         reset_ctl.word);

  //
  // Configure the link width.  The RC and EP port_link_control registers
  // have identical format and semantics, so we always use the RC version.
  //
  TRIO_PCIE_RC_PORT_LINK_CONTROL_t rc_port_link_ctl =
    { .word = cfg_rd(ts->shim_pos.word, 0,
                     mac_std_pa | TRIO_PCIE_RC_PORT_LINK_CONTROL) };

  if (port_cfg->allow_x8) 
    rc_port_link_ctl.link_mode_enable = 0xf;
  else if (port_cfg->allow_x4) 
    rc_port_link_ctl.link_mode_enable = 0x7;
  else if (port_cfg->allow_x2) 
    rc_port_link_ctl.link_mode_enable = 0x3;
  else if (port_cfg->allow_x1) 
    rc_port_link_ctl.link_mode_enable = 0x1;
  rc_port_link_ctl.scramble_disable = 0;

  cfg_wr(ts->shim_pos.word, 0, mac_std_pa | TRIO_PCIE_RC_PORT_LINK_CONTROL,
         rc_port_link_ctl.word);

  //
  // If the port might have been up or coming up, taking it down and
  // bringing it up immediately may confuse the other side.  So, we wait
  // for a bit, to make sure that whatever's at the other end of the link
  // has realized it's gone down.
  //
  drv_udelay(200 * 1000);

  //
  // Enable training.
  //
  port_config.word = cfg_rd(ts->shim_pos.word, 0,
                            mac_intf_pa | TRIO_PCIE_INTFC_PORT_CONFIG);
  port_config.train_mode =
    TRIO_PCIE_INTFC_PORT_CONFIG__TRAIN_MODE_VAL_TRAIN_ENA;
  cfg_wr(ts->shim_pos.word, 0, mac_intf_pa | TRIO_PCIE_INTFC_PORT_CONFIG,
         port_config.word);

  //
  // We need to wait for at least 30 us here to let the PLL spin up, but
  // if we wait longer than 1 ms, it'll be too late to synchronize the
  // clock dividers.
  //
  // Note that the BTK link bringup flow can't guarantee that it won't
  // be unduly delayed at this point, so what it does is keep the MAC
  // in reset until after it's done the synchronization.
  //
  drv_udelay(50);

  //
  // Make sure lanes are synchronized.  This needs to happen before we get
  // to Detect.Active but after PMA reset has completed.
  // 
  SERDES_PLL_F_SET_t spfs =
  {{
    .f = 4,           // Original value that we don't want to change.
    .div_mode0 = 0,   // Setting to 0 and back to 2 synchronizes the dividers.
  }};

  TRIO_PCIE_INTFC_SERDES_CONFIG_t tpisc =
  {{
    .send = 1,
    .read = 0,
    .lane_sel = 0xff, // Write all lanes (it's OK if this is a 4-lane MAC).
    .reg_addr = SERDES_PLL_F_SET,
    .reg_data = spfs.word,
  }};

  cfg_wr(ts->shim_pos.word, 0, mac_intf_pa | TRIO_PCIE_INTFC_SERDES_CONFIG,
         tpisc.word);

  //
  // This probably isn't necessary in practice, but the shim interface
  // claims that you should make sure the SERDES write completed before
  // you start another one.
  //
  TRIO_PCIE_INTFC_SERDES_CONFIG_t check_tpisc;
  do
  {
    check_tpisc.word = cfg_rd(ts->shim_pos.word, 0,
                              mac_intf_pa | TRIO_PCIE_INTFC_SERDES_CONFIG);
  }
  while (check_tpisc.send);

  spfs.div_mode0 = 2;
  tpisc.reg_data = spfs.word;

  cfg_wr(ts->shim_pos.word, 0, mac_intf_pa | TRIO_PCIE_INTFC_SERDES_CONFIG,
         tpisc.word);

  if (is_rc)
  {
    //
    // Deassert PERST so the connected device comes out of reset.
    //
    drv_set_signal(port_cfg->perst_sig, DRV_SIGNAL_DEASSERT);
  }
  else
  {
    //
    // Give the link a chance to come up, because the client is going to
    // check the link status as soon as we return.  (In the RC case, the
    // client does its own wait, so we don't need to.)
    //
    drv_udelay(200 * 1000);
  }

  return 0;
}

int
handle_gxio_trio_force_rc_link_up(trio_state_t* ts, int svc_dom,
                                  unsigned int mac)
{
  return force_link_up(ts, svc_dom, mac, 1);
}

int
handle_gxio_trio_force_ep_link_up(trio_state_t* ts, int svc_dom,
                                  unsigned int mac)
{
  return force_link_up(ts, svc_dom, mac, 0);
}

int
handle_gxio_trio_unconfig_sio_mac_err_intr(trio_state_t* ts, int svc_dom,
                                           unsigned int mac)
{
  TRIO_INT_BIND_t binding_setup;

  binding_setup.word = 0;
  binding_setup.vec_sel = TRIO_INT_BIND__VEC_SEL_VAL_MAC;
  binding_setup.bind_sel = mac * 32 +
      TRIO_PCIE_INTFC_MAC_INT_STS__STREAM_IO_ERROR_SHIFT;

  cfg_wr(ts->shim_pos.word, 0, TRIO_INT_BIND, binding_setup.word);

  return 0;
}

int
handle_gxio_trio_config_sio_mac_err_intr(trio_state_t* ts, int svc_dom,
                                         int inter_x, int inter_y,
                                         int inter_ipi, int inter_event,
                                         unsigned int mac)
{
  // Deny user-space access.
  if (inter_ipi < CLIENT_PL)
    return (HV_EINVAL);
  
  TRIO_INT_BIND_t binding_setup =
  {{
    .vec_sel = TRIO_INT_BIND__VEC_SEL_VAL_MAC,
    .evt_num = inter_event,
    .int_num = inter_ipi,
    .tileid = DRV_COORDS_TO_TILE_ID(inter_x, inter_y),
    .mode = 0,              /* not used for MAC interrupts */
    .enable = 1,
  }};

  // Select STREAM_IO_ERROR as the interrupt source.
  binding_setup.bind_sel = mac * 32 +
      TRIO_PCIE_INTFC_MAC_INT_STS__STREAM_IO_ERROR_SHIFT;

  cfg_wr(ts->shim_pos.word, 0, TRIO_INT_BIND, binding_setup.word);

  return 0;
}
