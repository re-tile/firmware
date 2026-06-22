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
 * Memory shim configuration.
 */

#include <arch/chip.h>
#include <arch/cycle.h>
#include <arch/ddr3.h>
#include <arch/msh.h>
#include <arch/sim.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>

#include "bits.h"
#include "board_info.h"
#include "boot_error.h"
#include "cfg.h"
#include "hv.h"
#include "hv_l1boot.h"
#include "hw_config.h"
#include "i2c_acc.h"
#include "mshim_acc.h"
#include "mshim_cfg.h"
#include "param.h"
#include "physacc.h"
#include "post/post_ram.h"


#ifdef MSH_DEBUG
/** Memory config debug output. */
#define DBG boot_printf
#else
/** Memory config debug output. */
#define DBG(...) do { } while (0)
#endif


uint64_t
mshim_diag_read(pos_t shimaddr, PA pa)
{
  while ((cfg_rd(shimaddr.word, 0, MSH_DIAG_CTL) &
          MSH_DIAG_CTL__ENABLE_DONE_MASK) == 0)
    ;

  cfg_wr(shimaddr.word, 0, MSH_DIAG_ADDR, pa);

  MSH_DIAG_CTL_t ctl  =
  {{
     .op = MSH_DIAG_CTL__OP_VAL_READ,
     .size = 0,
     .enable_done = 1,
  }};

  cfg_wr(shimaddr.word, 0, MSH_DIAG_CTL, ctl.word);

  int retries = 100000;
  while ((cfg_rd(shimaddr.word, 0, MSH_DIAG_CTL) &
          MSH_DIAG_CTL__ENABLE_DONE_MASK) == 0)
  {
    if (retries-- <= 0)
    {
      boot_printf("POST error: retries expired, diag read failed\n");
      return -1;
    }
  }

  return cfg_rd(shimaddr.word, 0, MSH_DIAG_RDATA);
}


void
mshim_diag_write(pos_t shimaddr, PA pa, uint64_t data)
{
  while ((cfg_rd(shimaddr.word, 0, MSH_DIAG_CTL) &
          MSH_DIAG_CTL__ENABLE_DONE_MASK) == 0)
    ;

  cfg_wr(shimaddr.word, 0, MSH_DIAG_ADDR, pa);

  cfg_wr(shimaddr.word, 0, MSH_DIAG_WDATA, data);

  MSH_DIAG_CTL_t ctl  =
  {{
     .op = MSH_DIAG_CTL__OP_VAL_WRITE,
     .size = 0,
     .enable_done = 1,
  }};

  cfg_wr(shimaddr.word, 0, MSH_DIAG_CTL, ctl.word);

  int retries = 100000;
  while ((cfg_rd(shimaddr.word, 0, MSH_DIAG_CTL) &
          MSH_DIAG_CTL__ENABLE_DONE_MASK) == 0)
  {
    if (retries-- <= 0)
    {
      boot_printf("POST error: retries expired, diag write failed\n");
      return;
    }
  }
}


/** Get an mshim's port number.
 * @param shimaddr Address of the shim.
 * @return Shim port number.
 */
static int
msh_port(pos_t shimaddr)
{
  MSH_DEV_INFO_t inforeg = { .word = cfg_rd(shimaddr.word, 0, MSH_DEV_INFO) };
  return inforeg.instance;
}


/** Run the sizing algorithm on a memory shim.
 * @param shimaddr Address of the shim.
 * @return log2 of the number of bytes available on the shim.
 */
static int
mshim_size_shim(pos_t shimaddr)
{
  //
  // Set up address range for the maximum memory size, so that we can do the
  // sizing.
  //
  cfg_wr(shimaddr.word, 0, MSH_ADDRESS_RANGE, 0x0);

  //
  // See how big the memory appears to be.  The algorithm we use is as
  // follows:
  //
  // - Start at what would be the last word of the largest possible
  //   memory configuration (512 GB), and write a unique value there.
  //
  // - Then go down by powers of two, writing a different unique value in
  //   each of those addresses until we write one in the last word of the
  //   smallest possible memory configuration (512 MB).
  //
  // - Now go up, reading the values back.  The first value which is not
  //   what we expect tells us that the size at which we did the previous
  //   read is the actual size of available memory.
  //
  // Note that this algorithm handles both missing DIMMs (which should
  // read back as zero) and address wraparound.  However, it does not
  // properly deal with the kind of discontiguous address space you
  // might see if you had a DIMM in the second slot, but not the first,
  // or you had two DIMMs.  Neither of those things will happen in the
  // cases where we use this routine, so they're not really an issue.
  //

  int log2addr = 0;
  const uint64_t marker = 0xAABBCCDDEEFF1100UL;

  PA addr;

  for (log2addr = MSH_MAX_SIZE_SHIFT, addr = 1UL << MSH_MAX_SIZE_SHIFT;
       log2addr >= MSH_MIN_SIZE_SHIFT;
       log2addr--, addr >>= 1)
  {
    uint64_t val = marker | log2addr;
    mshim_diag_write(shimaddr, addr - 64, val);
  }

  for (log2addr = MSH_MIN_SIZE_SHIFT, addr = 1UL << MSH_MIN_SIZE_SHIFT;
       log2addr <= MSH_MAX_SIZE_SHIFT;
       log2addr++, addr <<= 1)
  {
    uint64_t expval = marker | log2addr;
    uint64_t val = mshim_diag_read(shimaddr, addr - 64);

    if (val != expval)
      break;
  }
  log2addr--;

  return log2addr;
}


/** Configure memory on the simulator.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param rshimaddr Tile coordinates of the rshim.
 * @param board_flags Board flags.
 * @param speed User-requested memory speed, in MT/s, or 0 if no request
 *  made.
 * @return Amount of memory on the shim, in bytes, or -1 if the shim could
 *  not be configured.
 */
static int64_t
mshim_config_shim_sim(pos_t shimaddr, pos_t rshimaddr, uint32_t board_flags,
                      int speed)
{
  //
  // For now, always enable address hashing for banks & ranks.
  //
  cfg_wr(shimaddr.word, 0, MSH_CONTROL, MSH_CONTROL__ADDR_HASH_MASK);

  //
  // This is gsim, so nothing needs to be configured; we just enable the
  // shim.
  //
  cfg_wr(shimaddr.word, 0, MSH_BASELINE_CTL, MSH_BASELINE_CTL__ENABLE_MASK);

  //
  // gsim doesn't currently simulate the SPD PROMs, so we need to run the
  // sizing algorithm to figure out how much memory we have.
  //
  int64_t size = 1L << mshim_size_shim(shimaddr);

  // Test the DDR3 RAM attached to this shim.
  int post_failed = post_ram_quick(size, shimaddr);
  if (post_failed)
  {
    boot_error(POST_ERR_QUICK_DRAM);
  }

  //
  // If we couldn't successfully read or write _anything_, or if the POST
  // test of the RAM failed, disable the shim and return -1.
  //
  if (size < 1L << MSH_MIN_SIZE_SHIFT || post_failed)
  {
    cfg_wr(shimaddr.word, 0, MSH_BASELINE_CTL, 0);
    return -1;
  }

  //
  // The shim is OK, so return its size.  Note that our caller will
  // configure the shim's address range register.
  //
  return size;
}


/* Configure memory on the FPGA.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param rshimaddr Tile coordinates of the rshim.
 * @param board_flags Board flags.
 * @param speed User-requested memory speed, in MT/s, or 0 if no request
 *  made.
 * @return Amount of memory on the shim, in bytes, or -1 if the shim could
 *  not be configured.
 */
static int64_t
mshim_config_shim_fpga(pos_t shimaddr, pos_t rshimaddr, uint32_t board_flags,
                       int speed)
{
  //
  // Enable address hashing for banks & ranks, if appropriate.
  //
  // Not done on the FPGA right now since we only support 1 rank.
  //
#if 0
  cfg_wr(shimaddr.word, 0, MSH_CONTROL, MSH_CONTROL__ADDR_HASH_MASK);
#endif

  //
  // Somewhat hacky initialization for FPGA; see config_mphy_25() in
  // tools/gxbtk/mshim.py.
  //
  static const MSH_BASELINE_CTL_t mbc =
  {{
    .enable = 1,
  }};
  cfg_wr(shimaddr.word, 0, MSH_BASELINE_CTL, mbc.word);

  MSH_CONTROL_t mc;
  mc.word = cfg_rd(shimaddr.word, 0, MSH_CONTROL);
  mc.close_page = 1;
  cfg_wr(shimaddr.word, 0, MSH_CONTROL, mc.word);

  static const MSH_DDR3_USER_INIT_0_t mdui0 =
  {{
    .autoinit_dis = 1,
  }};
  cfg_wr(shimaddr.word, 0, MSH_DDR3_USER_INIT_0, mdui0.word);

  static const MSH_DDR3_MODE_CONTROL_t mdmc =
  {{
    .dll_rst = 1,
    .ptr_sync_n = 1,
    .dfi_init = 1,
  }};
  cfg_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc.word);

  // We're running 16 times slower than normal, so the refresh interval
  // needs to be decreased by a factor of 16.
  cfg_wr(shimaddr.word, 0, MSH_DDR3_REF_TIMING, 3125 / 16);

  MSH_DDR3_USER_INIT_1_t mdui1;

  for (int rank = 0; rank < 2; rank++)
  {
    // MR0 - write recovery 6, CAS latency 6, read burst nybble
    //       sequential, burst fixed 8
    mdui1.word = 0;
    mdui1.init_mr = 1;
    mdui1.init_mr_cs = 1 << rank;
    mdui1.init_mr_data = 0x420;
    cfg_wr(shimaddr.word, 0, MSH_DDR3_USER_INIT_1, mdui1.word);

    // MR1 - dll disable, rzq/4, additive latency 0
    mdui1.word = 0;
    mdui1.init_mr = 2;
    mdui1.init_mr_cs = 1 << rank;
    mdui1.init_mr_data = 0x7;
    cfg_wr(shimaddr.word, 0, MSH_DDR3_USER_INIT_1, mdui1.word);

    // MR2 - CAS write latency 6
    mdui1.word = 0;
    mdui1.init_mr = 4;
    mdui1.init_mr_cs = 1 << rank;
    mdui1.init_mr_data = 0x8;
    cfg_wr(shimaddr.word, 0, MSH_DDR3_USER_INIT_1, mdui1.word);

    // MR3 - just use defaults, but we do have to write it with something
    mdui1.word = 0;
    mdui1.init_mr = 8;
    mdui1.init_mr_cs = 1 << rank;
    mdui1.init_mr_data = 0x0;
    cfg_wr(shimaddr.word, 0, MSH_DDR3_USER_INIT_1, mdui1.word);
  }

  static const MSH_DDR3_PHY_DELAY_t mdpd =
  {{
    .wrlat = 2,
    .rdlat = 2,
    .rddata_en = 10,
  }};
  cfg_wr(shimaddr.word, 0, MSH_DDR3_PHY_DELAY, mdpd.word);

  MSH_DDR3_COL_TIMING_t mdct;
  mdct.word = cfg_rd(shimaddr.word, 0, MSH_DDR3_COL_TIMING);
  mdct.cl = 6;
  mdct.cwl = 6;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_COL_TIMING, mdct.word);

  // Clear the autoinit_dis bit, since it inhibits refresh
  cfg_wr(shimaddr.word, 0, MSH_DDR3_USER_INIT_0, 0);

  // We might need to wait here, or to check to see if some of the init
  // operations completed, before we do any testing.  Sleep for a second.

  uint_reg_t orig_cycle = get_cycle_count();
  while (get_cycle_count() - orig_cycle < (25000 * 1000))
    ;

  //
  // We could read the SPD on the FPGA board, but we don't really ever
  // expect to have different configurations, so we'll just return a fixed
  // value.
  //
  int64_t size = 1L << 31;  // 2 GB

  // Test the DDR3 RAM attached to this shim.
  int post_failed = post_ram_quick(size, shimaddr);
  if (post_failed)
    boot_error(POST_ERR_QUICK_DRAM);

  //
  // If we couldn't successfully read or write _anything_, or if the POST
  // test of the RAM failed, disable the shim and return -1.
  //
  if (size < 1L << MSH_MIN_SIZE_SHIFT || post_failed)
  {
    cfg_wr(shimaddr.word, 0, MSH_BASELINE_CTL, 0);
    return -1;
  }

  //
  // The shim is OK, so return its size.  Note that our caller will
  // configure the shim's address range register.
  //
  return size;
}


#if defined(MSH_DEBUG) || defined(MSH_TIMING)
/** Return the value of the rshim's uptime counter.
 * @return Uptime in REFCLK cycles since the last reset.
 */
static uint64_t
get_uptime()
{
  return cfg_rd(__insn_mfspr(SPR_RSHIM_COORD), 0, RSH_UPTIME);
}

/** Value of the rshim uptime counter at the start of memory configuration. */
uint_reg_t cfg_start_uptime;
#endif


/** Memory parameter information.  This is used to describe the
 *  characteristics of a DIMM, or of a set of DIMMs on one controller.
 */
struct mem_info
{
  /** SDRAM capacity in bits. */
  long sdram_capacity_bits;

  /** Module capacity in bytes. */
  long capacity_bytes;

  /** Rank size in bytes. */
  long rank_bytes;

  /** Minimum period, femtoseconds. */
  long tCKmin_fs;

  /** Supported CAS latencies.  Bitmap; bit x on if latency of x cycles
   *  supported.  (Low 4 bits, bits 0-3, are always off.) */
  long cas_latencies;

  /** Minimum CAS latency time, femtoseconds. */
  long tAAmin_fs;

  /** Minimum write recovery time, femtoseconds. */
  long tWRmin_fs;

  /** Minimum RAS# to CAS# delay time, femtoseconds. */
  long tRCDmin_fs;

  /** Minimum row active to row active delay time, femtoseconds. */
  long tRRDmin_fs;

  /** Minimum row precharge delay time, femtoseconds. */
  long tRPmin_fs;

  /** Minimum active to precharge delay time, femtoseconds. */
  long tRASmin_fs;

  /** Minimum active to active/refresh delay time, femtoseconds. */
  long tRCmin_fs;

  /** Minimum refresh recovery delay time, femtoseconds. */
  long tRFCmin_fs;

  /** Minimum internal write to read recovery delay time, femtoseconds. */
  long tWTRmin_fs;

  /** Minimum internal read to precharge command delay time, femtoseconds. */
  long tRTPmin_fs;

  /** Minimum four activate window delay time, femtoseconds. */
  long tFAWmin_fs;

  /** DQS skew relative to DQ, in picoseconds.  Only present in a coalesced
   *  mem_info structure, since this is really a controller property. */
  int dqs_offset_ps;

  /** Offset to DQS to improve setup time. */
  int8_t dqs_tweak;

  /** Offset to DQ to improve setup time. */
  int8_t dq_tweak;

  /** Address mirroring configuration.  Only present in a coalesced
   *  mem_info structure, since this is really a controller property. */
  uint16_t addr_mirror;

  /** Number of DIMMs.  Only present in a coalesced mem_info structure,
   *  since this is really a controller property. */
  int8_t numdimms;

  /** Chip selects per slot.  Only present in a coalesced mem_info structure,
   *  since this is really a controller property. */
  int8_t cs_per_slot;

  /** If nonzero, this is a registered DIMM. */
  int8_t rdimm;

  /** If nonzero, this is a load-reduced DIMM. */
  int8_t lrdimm;

  /** If nonzero, this is a SO-DIMM.  (Currently this is not set for
   *  registered SO-DIMMs, just unbuffered.) */
  int8_t sodimm;

  /** If nonzero, this memory is onboard */
  int8_t onboard;

  /** Number of bank address bits. */
  int8_t bank_bits;

  /** Number of row address bits. */
  int8_t row_bits;

  /** Number of column address bits. */
  int8_t col_bits;

  /** Memory voltages (bitmap of VOLT_xxx values). */
  int8_t voltages;

  /** Number of ranks per DIMM. */
  int8_t dimm_ranks;

  /** Number of ranks per controller. */
  int8_t ctl_ranks;

  /** Width of each SDRAM chip in bits. */
  int8_t sdram_width_bits;

  /** Nonzero if DIMM supports ECC. */
  int8_t ecc;

  /** Width of the total DIMM in bits. */
  int8_t bus_width_bits;

  /** If nonzero, rank 1 has mirrored address mapping.  Only relevant if
   *  rdimm is zero. */
  int8_t rank_1_mirrored;

  /** Registered DIMM config register 3 value. */
  uint8_t rc3_val;

  /** Registered DIMM config register 4 value. */
  uint8_t rc4_val;

  /** Registered DIMM config register 5 value. */
  uint8_t rc5_val;

  /** DIMM part number. */
  char part_no[19];

  /** Manufacturer's JEDEC code. */
  uint16_t mfg_code;
};

#define VOLT_1_5   0x1 /**< Flag for 1.5 V. */
#define VOLT_1_35  0x2 /**< Flag for 1.35 V. */
#define VOLT_1_2   0x4 /**< Flag for 1.2 V. */

#define VID_1_5   0x12 /**< DDR3 VID pins for 1.5 V. */
#define VID_1_35  0x2a /**< DDR3 VID pins for 1.35 V. */
#define VID_1_2   0x42 /**< DDR3 VID pins for 1.2 V. */

/** mem_info structs, one per shim.  We have more than one because we need
 *  to keep them around between the call to mshim_preconfig_shim() and the
 *  call to mshim_config_shim().
 */
struct mem_info minfos[MAX_MSHIMS];

/** Pointer to the mem_info struct describing the shim which is currently
 *  being configured.  It's cheesy that it's global, but it's used
 *  absolutely everywhere.  */
struct mem_info* minfo;

/** DIMM voltage being used (one of the VOLT_xxx codes).  There's only one
 *  of these since there's only one set of VID pins; all shims use the same
 *  voltage.
 */
static int dimm_voltage;

/** VID setting for the core. */
static int core_vid;

/** Core voltage in uV. */
static int core_voltage_uv;

/** Retrieve and verify the checksum of an SPD ROM.
 * @param bus I2C bus number.
 * @param devaddr I2C address of the ROM to read.
 * @param rshimaddr Address of the RShim.
 * @param spd buffer for returned data.
 * @param spdlen Length of buffer.
 * @return 0 if we can't read the SPD or it's bogus, 1 otherwise.
 */
static int
get_spd(int bus, int devaddr, pos_t rshimaddr, uint8_t* spd, int spdlen)
{
  //
  // An SPD must be at least this long for us to verify its CRC.
  //
  if (spdlen < 128)
    return 0;

  //
  // Zero out the provided SPD buffer, in case we end up reading fewer
  // bytes than requested.
  //
  memset(spd, 0, spdlen);

  //
  // First read one byte to see if the PROM is there.  We have to do this
  // because the hardware can't cope with a multi-byte read to a device
  // that doesn't respond.
  //
  if (i2c_rd(rshimaddr, I2CMS_CHAN(bus), devaddr, 0, 1, spd) != 1)
  {
    DBG("No response from SPD at address 0x%x\n", devaddr);
    return 0;
  }

  //
  // Figure out how much data we'll be reading, based on the byte we read.
  //
  switch (spd[0] & 0xF)
  {
  default:
  case 1:
    spdlen = min(spdlen, 128);
    break;
  case 2:
    spdlen = min(spdlen, 176);
    break;
  case 3:
    spdlen = min(spdlen, 256);
    break;
  }

  //
  // Now read the whole thing.
  //
  if (i2c_rd(rshimaddr, I2CMS_CHAN(bus), devaddr, 0, spdlen, spd) != spdlen)
    return 0;

  DBG("SPD at I2C address 0x%x:\n", devaddr);
  for (int i = 0; i < spdlen; i++)
  {
    if ((i & 15) == 0)
      DBG("%3d:", i);

    DBG(" %02x", spd[i]);

    if ((i & 15) == 15)
      DBG("\n");
  }

  if ((spdlen & 15) != 0)
    DBG("\n");

  //
  // Verify the CRC.
  //
  int crc_bytes = (spd[0] & 0x80) ? 117 : 126;
  uint16_t exp_crc = (spd[127] << 8) | spd[126];
  uint16_t act_crc = 0;

  for (int i = 0; i < crc_bytes; i++)
  {
      act_crc ^= spd[i] << 8;
      for (int j = 0; j < 8; j++)
          if (act_crc & 0x8000)
              act_crc = act_crc << 1 ^ 0x1021;
          else
              act_crc = act_crc << 1;
  }

  act_crc &= 0xFFFF;

  if (act_crc != exp_crc)
    return 0;

  return 1;
}

#define SCALE_MS (1000L)            /**< Milliseconds */
#define SCALE_US (SCALE_MS * 1000)  /**< Microseconds */
#define SCALE_NS (SCALE_US * 1000)  /**< Nanoseconds */
#define SCALE_PS (SCALE_NS * 1000)  /**< Picoseconds */
#define SCALE_FS (SCALE_PS * 1000)  /**< Femtoseconds */

/** Get DIMM information.
 * @param dme BIB DIMM map entry describing the DIMM.
 * @param mi mem_info struct to be loaded with DIMM information.
 * @param rshimaddr Address of the RShim.
 * @return 1 if the information was successfully retrieved, 0 otherwise.
 */
static int
get_dimm_info(struct bi_dimm_map_entry* dme, struct mem_info* mi,
              pos_t rshimaddr)
{
  uint8_t spd_buf[150];
  const uint8_t* spd = NULL;

  //
  // First, get an SPD, from the BIB for onboard memory or from the SPD ROM
  // on a DIMM.
  //
  if (dme->onboard)
  {
    bi_ptr_t resptr;
    uint32_t desc;
    desc = bi_getparam(BI_TYPE_SPD_DATA, dme->addr.onboard_inst, &resptr,
                       NULL);

    if (desc != BI_NULL)
      spd = (const uint8_t*) resptr;

    mi->onboard = 1;
  }
  else
  {
    int bus = dme->addr.i2c.bus;
    int spd_addr = dme->addr.i2c.dev_addr << 1;
    int switch_inst = dme->addr.i2c.switch_inst;
    int switch_chan = dme->addr.i2c.switch_chan;

    if (switch_inst != BI_I2C_ADDR_SWITCH_INST__VAL_NONE)
      i2c_switch_swing_boot(rshimaddr, I2CMS_CHAN(bus), bus, switch_inst,
                            switch_chan);

    //
    // Now that the I2C switch channel is enabled, read the SPD.
    //
    if (get_spd(bus, spd_addr, rshimaddr, spd_buf, sizeof (spd_buf)))
      spd = spd_buf;
  }

  if (!spd)
    return 0;

  //
  // Now parse and validate the SPD, and extract useful data into the
  // mem_info struct.
  //

  //
  // Verify revision.
  //
  if (spd[1] >> 4 != 1)
    return 0;

  //
  // Verify device type.
  //
  if (spd[2] != 0xB)
    return 0;

  //
  // Clear the mem_info structure.
  //
  memset(mi, 0, sizeof (*mi));

  //
  // Set module type flags.  We don't do CDIMMs or LRDIMMs yet.
  //
  switch (spd[3] & 0xF)
  {
  case 1:  // RDIMM
  case 5:  // Mini-RDIMM
  case 9:  // 72b-SO-RDIMM
    /* RDIMM */
    mi->rdimm = 1;
    //
    // Collect a couple of extra RDIMM-specific parameters.
    //
    mi->rc3_val = spd[70] >> 4;
    mi->rc4_val = spd[71] & 0xF;
    mi->rc5_val = spd[71] >> 4;
    break;

  case 3:  // SO_DIMM
  case 8:  // 72b-SO-UDIMM
    mi->sodimm = 1;
    break;

  case 2:  // UDIMM
  case 4:  // Micro-DIMM
  case 6:  // Mini-UDIMM
    /* UDIMM, so it's OK but do nothing */
    break;

  case 11:  // LRDIMM
    mi->lrdimm = 1;
    // Not fully implemented yet, so fail
    return 0;

  default:
      return 0;
  }

  //
  // Compute medium time base.  The value in the SPD is ns, but it's
  // fractional, so we do it in fs to keep things integral.
  //
  long mtb_fs = ((SCALE_FS / SCALE_NS) * spd[10]) / spd[11];

  //
  // Compute fine time base.  The value in the SPD is ps, but it's
  // fractional, so we do it in fs to keep things integral.
  //
  long ftb_fs = ((SCALE_FS / SCALE_PS) * (spd[9] >> 4)) / (spd[9] & 0xF);

  //
  // Extract capacity.
  //
  mi->sdram_capacity_bits = (256L * 1024 * 1024) << (spd[4] & 0xF);

  //
  // Extract bank/row/column address bits.
  //
  mi->bank_bits = 3 + ((spd[4] >> 4) & 0x7);
  mi->row_bits = 12 + ((spd[5] >> 3) & 0x7);
  mi->col_bits = 9 + (spd[5] & 0x7);

  //
  // Extract valid voltages.
  //
  mi->voltages = 0;
  if (!(spd[6] & 1))
    mi->voltages |= VOLT_1_5;
  if (spd[6] & 2)
    mi->voltages |= VOLT_1_35;
  if (spd[6] & 4)
    mi->voltages |= VOLT_1_2;

  //
  // Extract ranks, device width, bus width, ECC flag.
  //
  mi->dimm_ranks = mi->ctl_ranks = ((spd[7] >> 3) & 0x7) + 1;
  mi->sdram_width_bits = 4 << (spd[7] & 0x7);
  mi->ecc = ((spd[8] >> 3) & 0x3) != 0;
  mi->bus_width_bits = 8 << (spd[8] & 0x7);

  //
  // Compute module capacity.
  //
  mi->capacity_bytes =
    (mi->sdram_capacity_bits / 8) *
    (mi->bus_width_bits / mi->sdram_width_bits) * mi->dimm_ranks;

  mi->rank_bytes = mi->capacity_bytes / mi->dimm_ranks;

  //
  // Extract minimum period.
  //
  mi->tCKmin_fs = mtb_fs * spd[12] + ftb_fs * (int8_t) spd[34];

  //
  // Extract supported CAS latencies.
  //
  mi->cas_latencies = (((long) spd[15] << 8) | spd[14]) << 4;

  //
  // Extract minimum CAS latency time.
  //
  mi->tAAmin_fs = mtb_fs * spd[16] + ftb_fs * (int8_t) spd[35];

  //
  // Extract minimum write recovery time.
  //
  mi->tWRmin_fs = mtb_fs * spd[17];

  //
  // Extract minimum RAS# to CAS# delay time.
  //
  mi->tRCDmin_fs = mtb_fs * spd[18] + ftb_fs * (int8_t) spd[36];

  //
  // Extract minimum row active to row active delay time.
  //
  mi->tRRDmin_fs = mtb_fs * spd[19];

  //
  // Extract minimum row precharge delay time.
  //
  mi->tRPmin_fs = mtb_fs * spd[20] + ftb_fs * (int8_t) spd[37];

  //
  // Extract minimum active to precharge delay time.
  //
  mi->tRASmin_fs = (((spd[21] & 0xF) << 8) | spd[22]) * mtb_fs;

  //
  // Extract minimum active to active/refresh delay time.
  //
  mi->tRCmin_fs = (((((spd[21] >> 4) & 0xF) << 8) | spd[23]) * mtb_fs) +
               (int8_t) spd[38] * ftb_fs;

  //
  // Extract minimum refresh recovery delay time.
  //
  mi->tRFCmin_fs = ((spd[25] << 8) + spd[24]) * mtb_fs;

  //
  // Extract minimum internal write to read recovery delay time.
  //
  mi->tWTRmin_fs = spd[26] * mtb_fs;

  //
  // Extract minimum internal read to precharge command delay time.
  //
  mi->tRTPmin_fs = spd[27] * mtb_fs;

  //
  // Extract minimum four activate window delay time.
  //
  mi->tFAWmin_fs = (((spd[28] & 0xF) << 8) | spd[29]) * mtb_fs;

  //
  // For unbuffered DIMMs, extract the address mirroring flag.
  //
  if (!mi->rdimm)
    mi->rank_1_mirrored = spd[63] & 1;

  // 
  // Extract manufacturer's code.
  //
  mi->mfg_code = spd[118] << 8 | spd[117];

  //
  // Extract part number, trim trailing blanks.
  //
  memcpy(mi->part_no, &spd[128], 18);
  for (int i = 17; i >= 0; i++)
    if (mi->part_no[i] == ' ')
      mi->part_no[i] = '\0';
    else
      break;

  //
  // Print a brief description.
  //
  DBG("DIMM: %ld MB x%d %s-rank %s%s, %d mm, MFG %04x, P/N %s\n",
      mi->capacity_bytes >> 20,
      mi->sdram_width_bits,
      (mi->dimm_ranks == 4) ? "quad" :
                              (mi->dimm_ranks == 2) ? "dual" :
                                                      "single",
      (mi->onboard) ? "onboard" : (mi->rdimm) ? "RDIMM" : "UDIMM",
      mi->ecc ? ", ECC" : "",
      (spd[60] & 0x1F) + 15,
      mi->mfg_code,
      mi->part_no);

  return 1;
}

/** Check (and clear) any error bits pending in the shim.
 * @param shimaddr Tile coordinates of the memory shim.
 * @return < 0 if there were any errors which are always fatal (e.g.,
 *  address/command parity); > 0 if there were ECC errors (which aren't
 *  fatal if ECC is not enabled); and 0 if no errors are pending.
 */
static int
any_errors(pos_t shimaddr)
{
  uint_reg_t intrs = cfg_rd(shimaddr.word, 0, MSH_INT_VEC0_RTC);

  if (intrs & (MSH_INT_VEC0__PARITY0_ERR_MASK | MSH_INT_VEC0__PARITY1_ERR_MASK))
    return -1;

  if (intrs & (MSH_INT_VEC0__ECC_1BIT_MASK | MSH_INT_VEC0__ECC_2BIT_MASK))
    return 1;

  return 0;
}

//
// Macros used only within coalesce_mem_info(); defined outside it because
// otherwise Doxygen complains.
//

/** Find the maximum of all values. */
#define COALESCE_MAX(name) \
  mem_info->name = dimm_info[0].name; \
  for (int i = 1; i < numdimms; i++) \
    mem_info->name = max(mem_info->name, dimm_info[i].name);

/** AND all values together. */
#define COALESCE_AND(name) \
  mem_info->name = dimm_info[0].name; \
  for (int i = 1; i < numdimms; i++) \
    mem_info->name &= dimm_info[i].name;

/** Require all values to be equal; if not, return incompatible status. */
#define COALESCE_EQ(name) \
  mem_info->name = dimm_info[0].name; \
  for (int i = 1; i < numdimms; i++) \
    if (mem_info->name != dimm_info[i].name) \
    { \
      boot_printf("msh%d: DIMM parameter %s differs between DIMMs, " \
                  "shim ignored\n", portnum, #name); \
      return 1; \
    }

/** Require string values to be equal; if not, return incompatible status. */
#define COALESCE_EQ_STR(name) \
  strcpy(mem_info->name, dimm_info[0].name); \
  for (int i = 1; i < numdimms; i++) \
    if (strcmp(mem_info->name, dimm_info[i].name)) \
    { \
      boot_printf("msh%d: DIMM parameter %s differs between DIMMs, " \
                  "shim ignored\n", portnum, #name); \
      return 1; \
    }

/** Add all values together. */
#define COALESCE_SUM(name) \
  mem_info->name = dimm_info[0].name; \
  for (int i = 1; i < numdimms; i++) \
    mem_info->name += dimm_info[i].name;


/** Coalesce multiple mem_info structures describing DIMMs into one
 *  describing the memory as a whole.  Some parameters must match, and
 *  if they don't, we can't use this shim; others may differ, and if
 *  they do we take the maximum or minimum value; still others are added
 *  or ANDed together.
 *
 * @param portnum Shim port number, used for error messages.
 * @param dimm_info Input values.
 * @param numdimms Number of mem_info structs pointed to by dimm_info.
 *  These are assumed to be in order
 * @param mem_info Output values.
 * @return 1 if there was a fatal incompatibility (in which case an error
 *  message will have been printed), 0 otherwise.
 */
static int
coalesce_mem_info(int portnum, struct mem_info* dimm_info, int numdimms,
                  struct mem_info* mem_info)
{
  COALESCE_EQ(rdimm)
  COALESCE_EQ(sodimm)
  COALESCE_EQ(sdram_capacity_bits)
  COALESCE_EQ(bank_bits)
  COALESCE_EQ(row_bits)
  COALESCE_EQ(col_bits)
  COALESCE_AND(voltages)
  COALESCE_EQ(dimm_ranks)
  COALESCE_SUM(ctl_ranks)
  COALESCE_EQ(sdram_width_bits)
  COALESCE_AND(ecc)
  COALESCE_EQ(bus_width_bits)
  COALESCE_SUM(capacity_bytes)
  COALESCE_EQ(rank_bytes)
  COALESCE_MAX(tCKmin_fs)
  COALESCE_AND(cas_latencies)
  COALESCE_MAX(tAAmin_fs)
  COALESCE_MAX(tWRmin_fs)
  COALESCE_MAX(tRCDmin_fs)
  COALESCE_MAX(tRRDmin_fs)
  COALESCE_MAX(tRPmin_fs)
  COALESCE_MAX(tRASmin_fs)
  COALESCE_MAX(tRCmin_fs)
  COALESCE_MAX(tRFCmin_fs)
  COALESCE_MAX(tWTRmin_fs)
  COALESCE_MAX(tRTPmin_fs)
  COALESCE_MAX(tFAWmin_fs)
  COALESCE_EQ(rc3_val)
  COALESCE_EQ(rc4_val)
  COALESCE_EQ(rc5_val)
  COALESCE_EQ(mfg_code)
  COALESCE_EQ_STR(part_no)

  return 0;
}


//
// Variables and macros used in various functions below.
//

/** Clock period in femtoseconds.  This is the DDR clock period, not the
 *  mshim's internal clock period. */
static long tCK_fs;

/** Convert femtoseconds to cycles, given a clock.
 * @param val Number of femtoseconds.
 * @param clk Clock period in femtoseconds.
 */
#define FS_TO_CYCLES_CLK(val, clk) \
  (((val) + (clk) - 1) / (clk))

/** Convert femtoseconds to cycles, assuming tCK.
 * @param val Number of femtoseconds.
 */
#define FS_TO_CYCLES(val) \
  FS_TO_CYCLES_CLK((val), tCK_fs)

/** Compute time in cycles for a memory parameter from the SPD.
 * @param param Parameter name (a member of struct mem_info).
 * @param lower_bound Lower bound for the number of cycles returned.
 * @param upper_bound Upper bound for the number of cycles returned.
 * @param clk Clock period in femtoseconds.
 */
#define PARAM_TO_CYCLES_CLK(param, lower_bound, upper_bound, clk) \
  (min((upper_bound), max((lower_bound), \
                          FS_TO_CYCLES_CLK(minfo->param, (clk)))))

/** Compute time in cycles for a memory parameter from the SPD,
 *  assuming tCK.
 * @param param Parameter name (a member of struct mem_info).
 * @param lower_bound Lower bound for the number of cycles returned.
 * @param upper_bound Upper bound for the number of cycles returned.
 */
#define PARAM_TO_CYCLES(param, lower_bound, upper_bound) \
  PARAM_TO_CYCLES_CLK(param, lower_bound, upper_bound, tCK_fs)


/** Allow an mshim parameter value to be overriden by the BIB.
 * @param portnum Port number of shim being configured.
 * @param freq_hz DDR frequency in Hertz.
 * @param parameter Parameter being potentially overridden.
 * @param val Pointer to initial parameter value; this is unchanged if the
 *  parameter value is not overridden, or set to the override value if it is.
 */
static void
msh_reg_override(int portnum, long freq_hz, int parameter, int* val)
{
  bi_ptr_t resptr;
  uint32_t desc = bi_getparam(BI_TYPE_MSH_REG, portnum, &resptr, NULL);

  int speed_mts = (2 * freq_hz) / 1000000;

  if (desc != BI_NULL)
  {
    struct bi_msh_reg* mr = resptr;
    for (int i = 0; i < BI_WDS(desc) / 2; i++)
    {
      struct bi_msh_reg_entry* re = &mr->entries[i];

      //
      // Note that the VID is inversely related to the voltage, so
      // "core_vid <= min_voltage" is indeed checking to see if the voltage
      // is greater than or equal to the minimum.
      //
      if (parameter == re->parameter &&
          speed_mts >= re->min_speed &&
          minfo->numdimms >= re->min_dimm + 1 &&
          minfo->dimm_ranks >= re->min_rank + 1 &&
          core_vid <= re->min_voltage)
      {
        DBG("msh%d: BIB overriding parameter %d from %d to %d\n",
            portnum, parameter, *val, re->value);
        *val = re->value;
      }
    }
  }
}


//
// Some prototypes to cope with the fact that the BTK routines aren't
// always in the right order for C.  FIXME: either reorder the BTK code so
// that we can get rid of these, or pick a fixed order (e.g. alphabetical)
// for both bits of code and include prototypes for everything here.
//

static void config_rdimm(pos_t shimaddr, long freq_hz);
static int config_tx_delays(pos_t shimaddr, int first_lane, int num_lanes);
static int is_ecc_lane(int lane);
static void issue_mr(pos_t shimaddr, int mr, int mr_data, int mr_cs);
static int lane2dqs(int lane);
static int lane_width(void);
static void pulse_ptr_sync(pos_t shimaddr);
static uint64_t test_lane_mask(int lane);
static void write_mr(pos_t shimaddr, int addr, int data, int cs);

//
// These #defines are at least theoretically adjustable, but there may be
// cases where algorithms are tuned to the current values, so beware.
//

/** DLL steps per cycle. */
#define DLL_STEPS_PER_CYCLE 64

/** Smallest legal transmit cycle delay value. */
#define MIN_TX_DQS_CYCLE    1

/** Largest legal transmit cycle delay value. */
#define MAX_TX_DQS_CYCLE    3

/** Smallest legal transmit delay value. */
#define MIN_TX_DQS_DELAY    (MIN_TX_DQS_CYCLE * DLL_STEPS_PER_CYCLE)

/** Largest legal transmit delay value. */
#define MAX_TX_DQS_DELAY    ((MAX_TX_DQS_CYCLE * DLL_STEPS_PER_CYCLE) + \
                             (DLL_STEPS_PER_CYCLE - 1))

/** Minimum write latency value. */
#define MIN_ALLOWED_WRLAT   (MIN_TX_DQS_CYCLE - 1)

/** Maximum write latency value. */
#define MAX_ALLOWED_WRLAT   MAX_TX_DQS_CYCLE

/** Number of cycles of delay range to cover for candidate transmit delays. */
#define TX_DQS_DELAY_MAX_CYCLE (MAX_TX_DQS_CYCLE - MIN_TX_DQS_CYCLE + 1)

/** Step size for covering the Tx delay range. */
#define TX_RANGE_STEP 8

/** Start point for covering the Tx delay range. */
#define TX_RANGE_START 0

/** Maximum number of delays, used to size various arrays. */
#define MAX_DELAYS ((DLL_STEPS_PER_CYCLE / TX_RANGE_STEP) * \
                    TX_DQS_DELAY_MAX_CYCLE)

/** Range of phase values. */
#define PHASE_RANGE  (4 * 8)

/** Range of fine values. */
#define FINE_RANGE 1

/** Step size for fine adjustment. */
#define FINE_STEP 1

/** Number of lanes. */
#define NLANE 18

/** Maximum number of ranks. */
#define NRANK 16

/** Size of our BIST runs. */
#define BIST_SIZE (1024 * 1024)

/** Translate a lane into the associated delay control register number.
 *  Applies to data tx_control, rx_control, rx_control_gate.
 * @param lane Lane number.
 * @return Register number (0-8).
 */
#define REG_INDEX(lane) (lane2dqs(lane) % 9)

/** Translate a lane into the associated delay control field number.
 *  Applies to data tx_control, rx_control, rx_control_gate.
 * @param lane Lane number.
 * @return Delay control field (0 or 1).
 */
#define FIELD_INDEX(lane) (lane2dqs(lane) >= 9)

/** Delay for a certain number of tCK clock periods.
 * @param clocks Number of clock periods.
 */
#define WAIT_CLOCKS(clocks) \
  drv_udelay(((clocks) * tCK_fs * SCALE_US + SCALE_FS - 1) / SCALE_FS)

//
// Global variables.
//
/** Transmit DQS delays, indexed by lane. */
static uint8_t tx_dqs_delays[NLANE];

/** Last transmit DQS delay which resulted in a successful configuration. */
uint8_t good_tx_dqs_delay;

/** Last phase delay which resulted in a successful configuration. */
uint8_t good_phase_delay;

/** Transmit DQ offsets, indexed by lane. */
static int8_t tx_dq_delay_offsets[NLANE];

/** Receive gate phase delays (really cycle + phase), indexed by lane. */
static int8_t rx_gate_phase_delays[NLANE];

/** Receive gate fine delays, indexed by lane. */
static int8_t rx_gate_fine_delays[NLANE];

/** Receive DQS- delays. */
static uint8_t rx_dqsn_delays[NLANE];

/** Receive DQS+ delays. */
static uint8_t rx_dqsp_delays[NLANE];

/** Receive bit delays. */
static uint8_t rx_bit_delays[NLANE];

/** Target DQ offset. */
static int8_t target_dq_offset;

/** Nonzero if this is a rev A2 or later chip. */
static int is_a2_or_later = 0;

/** Nonzero if this is a rev A3 or later chip. */
static int is_a3_or_later = 0;

/** Nonzero if this chip has a separate slave DLL reset. */
static int rx_slave_reset_supported = 0;

/** Define this symbol to avoid using the RX DLL even on A2 chips. */
// #define HOLD_RX_DLL_IN_RESET

/** Zero if we are not using the RX DLL. */
#ifdef HOLD_RX_DLL_IN_RESET
static const int rx_dll_enabled = 0;
#else
static int rx_dll_enabled;
#endif

/////////////////////////////////////////////////////////////////////////////
// DDR3 RAM rank routines
/////////////////////////////////////////////////////////////////////////////

/** Saved mode register contents.  Since we can't actually read the
 *  hardware registers, we save the last value written here and return it
 *  when a read is performed. */
static uint16_t shadow_mr[4][NRANK];


/** Write an SDRAM mode register.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param addr Mode register number.
 * @param data Mode register data.
 * @param cs Chip select number.
 */
static void
write_mr(pos_t shimaddr, int addr, int data, int cs)
{
  DBG("Writing mode register %d, data = 0x%x, cs = %d\n", addr, data, cs);

  issue_mr(shimaddr, 1 << addr, data, 1 << cs);

  shadow_mr[addr][cs] = data;
}


/** Read an SDRAM mode register.  (We can't really read back from the
 *  hardware, so instead we return the value we last wrote.)
 * @param addr Mode register number.
 * @param cs Chip select number.
 * @return Mode register contents.
 */
static int
read_mr(int addr, int cs)
{
  return shadow_mr[addr][cs];
}


/** Rzq (value of external reference resistor in ohms) */
#define RZQ 240

/** Modify the target rank's MR1 to request the specified dynamic
 *  termination value.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param rtt_nom Nominal termination resistance in ohms; 0 means no
 *  dynamic termination.
 * @param rank Rank to modify.
 */
static void
set_rtt_nom(pos_t shimaddr, int rtt_nom, int rank)
{
  DDR3_MR1_t mr1 = { .word = read_mr(1, rank) };

  int enc_rtt_nom = 0;

  if (rtt_nom == 0)
    enc_rtt_nom = 0;
  else if (rtt_nom == (RZQ / 4))
    enc_rtt_nom = 1;
  else if (rtt_nom == (RZQ / 2))
    enc_rtt_nom = 2;
  else if (rtt_nom == (RZQ / 6))
    enc_rtt_nom = 3;
  else if (rtt_nom == (RZQ / 12))
    enc_rtt_nom = 4;
  else if (rtt_nom == (RZQ / 8))
    enc_rtt_nom = 5;
  else
    DBG("Invalid RTT value %d passed to save_rtt_nom for rank %d\n",
        rtt_nom, rank);

  mr1.rtt_nom_lsb = (enc_rtt_nom >> 0) & 1;
  mr1.rtt_nom_mid = (enc_rtt_nom >> 1) & 1;
  mr1.rtt_nom_msb = (enc_rtt_nom >> 2) & 1;

  write_mr(shimaddr, 1, mr1.word, rank);
}


/** Modify the target rank's MR2 to request the specified dynamic
 *  termination value for writes.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param rtt_nom Write termination resistance in ohms; 0 means no
 *  termination change for writes (i.e., we use the value set by
 *  save_rtt_nom()).
 * @param rank Rank to modify.
 */
static void
set_rtt_wr(pos_t shimaddr, int rtt_wr, int rank)
{
  DDR3_MR2_t mr2 = { .word = read_mr(2, rank) };

  if (rtt_wr == 0)
    mr2.rtt_wr = 0;
  else if (rtt_wr == (RZQ / 4))
    mr2.rtt_wr = 1;
  else if (rtt_wr == (RZQ / 2))
    mr2.rtt_wr = 2;
  else
    DBG("Invalid RTT value %d passed to save_rtt_wr for rank %d\n",
        rtt_wr, rank);

  write_mr(shimaddr, 2, mr2.word, rank);
}



/////////////////////////////////////////////////////////////////////////////
// DDR3 RAM rank routines
/////////////////////////////////////////////////////////////////////////////


/** Write an RDIMM control word.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param cw_addr Control word address.
 * @param cw_data Control word data.
 * @param cw_cs Bitmask of chip selects; the selected ranks get their
 *  registers configured.  Note that there must be more than
 *  one bit on in this mask.
 */
static void
write_cw(pos_t shimaddr, int cw_addr, int cw_data, int cw_cs)
{
  DBG("Writing control word %d, data = 0x%x, cs = 0x%x\n",
      cw_addr, cw_data, cw_cs);

  issue_mr(shimaddr, 1, (cw_addr << 8) | cw_data, cw_cs);

  if (cw_addr == 2 || cw_addr == 10)
    // Wait stabilization time (6 usec) after writing to RC10 (operating
    // speed control word) or RC2
    drv_udelay(6);
  else
    // Wait tMRD (8 clocks) after programming a control word
    WAIT_CLOCKS(8);
}


/** Configure registered DIMM registers.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param freq_hz DDR frequency in Hertz.
 */
static void
config_rdimm(pos_t shimaddr, long freq_hz)
{
  //
  // For now, just write the same value to all regs on all DIMMs.  We might
  // eventually want to allow for a mix of DIMMs that might have different
  // SPD values to be copied to the control words; right now that'll fail
  // when the SPDs are coalesced.
  //
  int cw_cs = 0xffff;

  //
  // RDIMMs distinguish between normal memory operations (including mode
  // register writes to the DRAMs themselves) and control word writes
  // to the actual register chip by the number of chip selects present; one
  // for DRAMs, more than one for the register chip.  This creates a
  // problem for single-rank RDIMMs.  Typically we have 2 or 4 chip selects
  // per slot, so we program the rank mapping to skip those extra ranks,
  // making the instantiated logical ranks contiguous.  But this means that
  // even if we assert all of the logical ranks, only one physical rank per
  // slot will get asserted, and thus the RDIMMs will not recognize when
  // their control words are being programmed.
  //
  // To avoid this problem, in this routine, while we're writing control
  // words, we use the mode 0 rank mapping, where each logical rank maps
  // directly to each physical rank.  We restore the previously computed
  // rank mapping before we return.
  //
  // Note that is thus impossible to support registered DIMMS in a system
  // which only runs one chip select line to each DIMM slot.
  //
  MSH_DDR3_DIMM_CFG_t mddc = { .word = cfg_rd(shimaddr.word, 0,
                                              MSH_DDR3_DIMM_CFG) };
  MSH_DDR3_DIMM_CFG_t mode0_mddc = mddc;

  mode0_mddc.rank_mapping = MSH_DDR3_DIMM_CFG__RANK_MAPPING_VAL_MODE0;

  cfg_wr(shimaddr.word, 0, MSH_DDR3_DIMM_CFG, mode0_mddc.word);

  //
  // RC10 - RDIMM Operating Speed
  //
  int cw_data;

  if (2 * freq_hz > 1600000000)
      cw_data = 4;
  else if (2 * freq_hz > 1333333333)
      cw_data = 3;
  else if (2 * freq_hz > 1066666666)
      cw_data = 2;
  else if (2 * freq_hz > 800000000)
      cw_data = 1;
  else
      cw_data = 0;

  write_cw(shimaddr, 10, cw_data, cw_cs);

  //
  // RC0 - Global Features
  //
  int output_inversion_disabled = 0;
  int float_enabled = 0;
  int a_outputs_disabled = 0;
  int b_outputs_disabled = 0;
  cw_data = (output_inversion_disabled << 0) |
            (float_enabled << 1) |
            (a_outputs_disabled << 2) |
            (b_outputs_disabled << 3);
  write_cw(shimaddr, 0, cw_data, cw_cs);

  //
  // RC1 - Clock Driver Enable
  //
  // ENHANCEME - these could be disabled to save power based on SPD info
  // showing what clock outputs are used.
  //
  int disable_y0_clock = 0;
  int disable_y1_clock = 0;
  int disable_y2_clock = 0;
  int disable_y3_clock = 0;
  cw_data = (disable_y0_clock << 0) |
            (disable_y1_clock << 1) |
            (disable_y2_clock << 2) |
            (disable_y3_clock << 3);
  write_cw(shimaddr, 1, cw_data, cw_cs);

  //
  // RC2 - Timing
  //

  // 0 = Standard (1/2 Clock)
  // 1 = Address and command nets pre-launch
  int pre_launch = 0;

  // 0 = 100 Ohm, 1 = 150 Ohm
  int in_bus_term_150 = 0;

  // 0 = Operation (Frequency Band 1)
  // 1 = Test Mode (Frequency Band 2)
  int freq_band_sel = 0;

  cw_data = (pre_launch << 0) |
            (in_bus_term_150 << 2) |
            (freq_band_sel << 3);
  write_cw(shimaddr, 2, cw_data, cw_cs);

  //
  // RC3 - Command/Address Driver Characteristics
  //
  write_cw(shimaddr, 3, minfo->rc3_val, cw_cs);

  //
  // RC4 - Control Signal Driver Characteristics
  //
  write_cw(shimaddr, 4, minfo->rc4_val, cw_cs);

  //
  // RC5 - CK Driver Characteristics
  //
  write_cw(shimaddr, 5, minfo->rc5_val, cw_cs);

  // RC6 and RC7 are vendor-specific
  // ENHANCEME - would we want to write these?

  //
  // RC8 - Additional IBT Setting
  //
  cw_data = 0;
  write_cw(shimaddr, 8, cw_data, cw_cs);

  //
  // RC9 - Power Saving Settings
  //
  int weak_drive = 0;
  int cke_power_down_mode = 0;
  int cke_power_down_mode_enable = 0;
  cw_data = (weak_drive << 0) |
            (cke_power_down_mode << 2) |
            (cke_power_down_mode_enable << 3);
  write_cw(shimaddr, 9, cw_data, cw_cs);

  //
  // RC11 - Operating Voltage VDD
  //
  if (dimm_voltage == VOLT_1_5)
    cw_data = 0;
  else if (dimm_voltage == VOLT_1_35)
    cw_data = 1;
  else if (dimm_voltage == VOLT_1_2)
    cw_data = 2;
  else
    cw_data = 0;

  write_cw(shimaddr, 11, cw_data, cw_cs);

  //
  // Restore the original rank mapping.
  //
  cfg_wr(shimaddr.word, 0, MSH_DDR3_DIMM_CFG, mddc.word);
}

/////////////////////////////////////////////////////////////////////////////
// Shmoo routines
/////////////////////////////////////////////////////////////////////////////

/** Check a configuration with a simple cacheline write & read.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane to test, or -1 to test all lanes.
 * @return zero if the write and read succeeded; otherwise, a bitmap of
 *  bits which failed.  Upon successful return, the shim's error state bits
 *  will be cleared.
 */
static uint64_t
check_config_quick_diff(pos_t shimaddr, int lane)
{
  //
  // We'll be doing uncached reads and writes for our testing, so configure
  // the target shim into the CBOX memory map, set up the AAR we'll use,
  // and define some data patterns.
  //
  set_cbox_mmap_spr(0, shimaddr.word);
  static const SPR_AAR_t aar =
  {{
    .physical_memory_mode = 1,
    .memory_attribute = SPR_AAR__MEMORY_ATTRIBUTE_VAL_UNCACHEABLE,
  }};

  //
  // For each address we test, we use two different data blocks so that the
  // results from one test cannot make a subsequent test "appear" to work;
  // i.e. previous data written read back as if it were written correctly
  // for this test
  //
  // The data we use must have enough variety in it, from one 64-bit beat
  // to the next within the burst, for every data lane AND the ECC lane(s).
  //
  // Note that complementing the test data is NOT effective for the ECC
  // lane(s) because the ECC bits generated for the complemented 64-bit
  // value are the same as they were for the original data; i.e. there
  // would be NO change in the ECC values for the second test data block.
  //
  // The test data we are using are randomly-generated values that seem to
  // be sufficient.
  //

  static const uint64_t test_data_0[8] =
  {
    0xBAABD96C6030A23DUL,
    0xD51130A912AF5BE1UL,
    0x07075CA0D0C589D3UL,
    0xF0BBA4E820A242DDUL,
    0x7006C22FF40A5D47UL,
    0xDA35C523F7E67AEDUL,
    0x78582DC2321D0B61UL,
    0x848BB92DDB590FE3UL,
  };

  static const uint64_t test_data_1[8] =
  {
    0x5F9AC1B2AE901D8FUL,
    0x5C2AE3C7DBDD2928UL,
    0x48D943D16A431424UL,
    0x3A229F24F8B5BA11UL,
    0x680658D891A8A492UL,
    0x965AA701847E5ECFUL,
    0xAAB5A55608D0410FUL,
    0x5BC6AC93B0BFEE18UL,
  };

  static const uint64_t* const test_datas[2] =
  {
    test_data_0,
    test_data_1,
  };

  //
  // Do our write/read test.
  //
  uint64_t err_bits = 0;
  uint64_t mask = test_lane_mask(lane);

  for (int rank = 0; rank < minfo->ctl_ranks; rank++)
  {
    PA addr = rank * minfo->rank_bytes;

    //
    // There may be bad ECC in memory.  To fix that, we need to write to
    // the cacheline we're going to test, wait for the write to complete,
    // and then clear the error bits.  If we don't do that, the RMW for
    // our very first write will set an ECC error bit and the test will
    // fail.
    //
    phys_wr64(addr, 0, aar.word);
    __insn_mf();
    (void) any_errors(shimaddr);

    //
    // We do 2 passes using different data.
    //
    for (uint64_t pass = 0; pass < 2; pass++)
    {
      const uint64_t* const test_data = test_datas[pass];

      for (int i = 0; i < 8; i++)
        phys_wr64(addr + i * 8, test_data[i], aar.word);

      for (int i = 0; i < 8; i++)
      {
        err_bits |= mask & (phys_rd64(addr + i * 8, aar.word) ^ test_data[i]);
        //
        // If we see an error, and we aren't testing all lanes, we can
        // quit.  If we're testing all lanes, we want to report all errors,
        // so we need to test everything.  (Actually, the BTK exits early
        // if it's testing all lanes and all bits are in error, but it's
        // not clear how often that actually happens, so for now we aren't
        // doing that.)
        //
        if (err_bits && lane >= 0)
          return err_bits;
      }
    }

    if (err_bits && lane >= 0)
      return err_bits;

    //
    // If we got ECC errors, behave as if all lanes failed.  This assumes
    // we don't actually enable ECC checking until we're checking the ECC
    // lanes, since otherwise the data lanes would always fail.  Note that
    // we have to check this on every rank since we clear the error bits
    // before testing a different cacheline.
    //
    if (lane < 0 && !err_bits && any_errors(shimaddr))
      return ~0UL;
  }

  return err_bits;
}

/** Check a configuration with a simple cacheline write & read.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane to test, or -1 to test all lanes.
 * @return 1 if the write and read succeeded; 0 if it failed, or if we saw
 *  any ECC errors during the test.  Upon successful return, the shim's
 *  error state bits will be cleared.
 */
static uint64_t
check_config_quick(pos_t shimaddr, int lane)
{
  return check_config_quick_diff(shimaddr, lane) == 0;
}


/////////////////////////////////////////////////////////////////////////////
// Clock control
/////////////////////////////////////////////////////////////////////////////

// config_pll() is inlined in config_clocks, below.

/** Configure the shim's clocks.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param freq_hz Shim core frequency in hertz; this is equal to the
 *  desired transactions per second rate, and twice the actual DDR clock
 *  frequency.
 */
static void
config_clocks(pos_t shimaddr, long freq_hz)
{
  //
  // Clock bringup
  //

  DBG("Configuring clocks\n");

  MSH_DDR3_MODE_CONTROL_t mdmc =
    { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL) };

  //
  // FIXME - we don't support using a different frequency for an external
  // refclk.  Of course, we might not support it at all eventually, if
  // it turns out not to be useful.
  //
#ifdef USE_EXT_REFCLK
  mdmc.refclk_source = 1;
#else
  mdmc.refclk_source = 0;
#endif
  cfg_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc.word);

  unsigned int m, n, q, range;
  freq_to_pll(2 * freq_hz, &m, &n, &q, &range, REFCLK, 0);

  DBG("Setting clock gen freq to %ld Hz, m %d, n %d, q %d, range %d\n",
      2 * freq_hz, m, n, q, range);

  MSH_DDR3_CLKGEN_PLL_CONTROL_t mdcpc =
  {{
    .rst = 1,
    .divr = n,
    .divf = m,
    .divq = q,
    .range = range,
  }};
  cfg_wr(shimaddr.word, 0, MSH_DDR3_CLKGEN_PLL_CONTROL, mdcpc.word);

  // de-assert clkgen pll reset
  mdcpc.rst = 0;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_CLKGEN_PLL_CONTROL, mdcpc.word);

  //
  // Wait 50 usec for PLL to lock
  //
  drv_udelay(50);

  //
  // Check PLL lock bit
  //
  do
  {
    mdcpc.word = cfg_rd(shimaddr.word, 0, MSH_DDR3_CLKGEN_PLL_CONTROL);
  } while (!mdcpc.lock);

  // de-assert clkgen div rst

  mdmc.clkgen_div_rst_n = 1;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc.word);

  // Now we can switch dclk domain to clkgen pll output;

  mdmc.dclk_source = 1;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc.word);

  freq_to_pll_fb(freq_hz, &m, &n, &q, &range, freq_hz / 4);

  DBG("Setting deskew freq to %ld Hz, m %d, n %d, q %d, range %d\n",
      freq_hz, m, n, q, range);

  //
  // Program the deskew PLL values while it is in reset
  //
  MSH_DDR3_DESKEW_PLL_CONTROL_t mddpc =
  {{
    .rst = 1,
    .divr = n,
    .divf = m,
    .divq = q,
    .range = range,
  }};
  cfg_wr(shimaddr.word, 0, MSH_DDR3_DESKEW_PLL_CONTROL, mddpc.word);

  //
  // De-assert deskew PLL reset
  //
  mddpc.rst = 0;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_DESKEW_PLL_CONTROL, mddpc.word);

  //
  // Wait 50 usec for PLL to lock
  //
  drv_udelay(50);

  //
  // Check PLL lock bit
  //
  do
  {
    mddpc.word = cfg_rd(shimaddr.word, 0, MSH_DDR3_DESKEW_PLL_CONTROL);
  } while (!mddpc.lock);
}

/////////////////////////////////////////////////////////////////////////////
// DQS index and lane routines
/////////////////////////////////////////////////////////////////////////////

/** Return first ECC lane.
 * @return First ECC lane.
 */
static int
low_ecc_lane()
{
  return (minfo->sdram_width_bits == 4) ? 16 : 8;
}


/** Return second ECC lane.
 * @return Second ECC lane.
 */
static int
high_ecc_lane()
{
  return (minfo->sdram_width_bits == 4) ? 17 : -1;
}


/** Determine whether a lane is an ECC lane.
 * @param lane Lane number.
 * @return Nonzero if lane is used for ECC.
 */
static int
is_ecc_lane(int lane)
{
  return (lane == low_ecc_lane() || lane == high_ecc_lane());
}


/** Return the width of a lane, in bits.
 * @return Lane width.
 */
static int
lane_width()
{
  return min(minfo->sdram_width_bits, 8);
}


/** Return a right-justified mask which has the same number of 1 bits as a
 *  lane.
 * @return Lane mask.
 */
static uint64_t
lane_mask()
{
  return RMASK(lane_width());
}


/** Return number of data lanes.
 * @return Number of data lanes.
 */
static int
num_data_lanes()
{
  return (minfo->sdram_width_bits == 4) ? 16 : 8;
}


/** Return number of configured lanes (i.e., data lanes plus ECC lanes).
 * @return Number of configured lanes.
 */
static int
num_config_lanes()
{
  return (minfo->ecc) ? ((minfo->sdram_width_bits == 4) ? 18 : 9)
                     : ((minfo->sdram_width_bits == 4) ? 16 : 8);
}


/** Compute a mask for a particular lane within a 64-bit word.
 * @param DQS index.  If this is less than 0, or is an ECC lane, a mask of
 *   all 1's will be returned.
 */
static uint64_t
test_lane_mask(int lane)
{
  if (lane < 0 || is_ecc_lane(lane))
    return ~0UL;
  else
    return lane_mask() << (lane * lane_width());
}


/** Convert a lane number into a DQS index.
 * @param lane Lane number.
 * @param DQS index.
 */
static int
lane2dqs(int lane)
{
  if (minfo->sdram_width_bits == 4)
  {
    if (lane & 1)
      return (lane / 2) + 9;
    else
      return lane / 2;
  }

  return lane;
}


/////////////////////////////////////////////////////////////////////////////
// RAM mode register support
/////////////////////////////////////////////////////////////////////////////

/** Initialize the shadow mode registers with the values they were given
 *  by the hardware init flow.
 * @param shimaddr Tile coordinates of the memory shim.
 */
static void
init_mode_regs(pos_t shimaddr)
{
  //
  // This routine models the updates to the mode registers that will occur
  // when the controller is asked to perform the RAM init operation.  When
  // this is done, some of the values written are fixed values and some
  // come from mshim configuration registers.
  //
  // Another use of this method is to initialize the mode register objects
  // before using them to perform explicit writes to the RAMs.
  //
  // In either case, the usage is similar, in that first the mshim
  // configuration registers must be written to set up the values to be
  // used, so that this method can read them and extract those values.
  //

  MSH_DDR3_COL_TIMING_t mdct =
    { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_COL_TIMING) };
  MSH_DDR3_MR0_t ddr3_mr0 =
    { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_MR0) };
  MSH_DDR3_MR1_t ddr3_mr1 =
    { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_MR1) };
  MSH_DDR3_MR2_t ddr3_mr2 =
    { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_MR2) };
  MSH_DDR3_MR3_t ddr3_mr3 =
    { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_MR3) };

  //
  // MR0
  //
  DDR3_MR0_t mr0 = { .word = 0 };

  mr0.bl = ddr3_mr0.bl;
  mr0.rbt = ddr3_mr0.bt;

  //
  // Encode CAS latency.
  //
  int cl = mdct.cl - 4;
  mr0.cl_lsb = (cl >> 0) & 7;
  mr0.cl_msb = (cl >> 3) & 1;

  mr0.tm = 0;
  mr0.dll = 1;

  //
  // Encode same write recovery time.
  //
  if (mdct.wr <= 8)
    mr0.wr = mdct.wr - 4;
  else
    mr0.wr = (mdct.wr >> 1) & 7;

  mr0.ppd = 1;

  //
  // MR1
  //
  DDR3_MR1_t mr1 = { .word = 0 };

  mr1.qoff = ddr3_mr1.qoff;
  mr1.tdqs = ddr3_mr1.tdqs;
  mr1.level = 0;
  mr1.al = ddr3_mr1.al;
  mr1.rtt_nom_lsb = (ddr3_mr1.rtt >> 0) & 1;
  mr1.rtt_nom_mid = (ddr3_mr1.rtt >> 1) & 1;
  mr1.rtt_nom_msb = (ddr3_mr1.rtt >> 2) & 1;
  mr1.dic_lsb = (ddr3_mr1.ds >> 0) & 1;
  mr1.dic_msb = (ddr3_mr1.ds >> 1) & 1;
  mr1.dll = ddr3_mr1.dll_disable;

  //
  // MR2
  //
  DDR3_MR2_t mr2 = { .word = 0 };

  mr2.rtt_wr = ddr3_mr2.rtt_wr;
  mr2.srt = ddr3_mr2.srt;
  mr2.asr = ddr3_mr2.asr;
  mr2.cwl = mdct.cwl - 5;
  mr2.pasr = ddr3_mr2.pasr;

  //
  // MR3
  //
  DDR3_MR3_t mr3 = { .word = 0 };

  mr3.word = ddr3_mr3.mr3;

  //
  // Set all of the shadow values.
  //
  for (int rank = 0; rank < minfo->ctl_ranks; rank++)
  {
    shadow_mr[0][rank] = mr0.word;
    shadow_mr[1][rank] = mr1.word;
    shadow_mr[2][rank] = mr2.word;
    shadow_mr[3][rank] = mr3.word;
  }
}


/** Issue a mode register or control word write.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param mr Mode register number (1-hot representation).
 * @param mr_data Mode register data.
 * @param mr_cs Bitmask of chip selects.
 */
static void
issue_mr(pos_t shimaddr, int mr, int mr_data, int mr_cs)
{
  // Modify the mode register. Command is issued on rising edge of mr
  MSH_DDR3_USER_INIT_1_t mdui1 =
    { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_USER_INIT_1) };

  mdui1.init_mr = 0;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_USER_INIT_1, mdui1.word);

  mdui1.init_mr_data = mr_data;
  mdui1.init_mr = mr;
  mdui1.init_mr_cs = mr_cs;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_USER_INIT_1, mdui1.word);

  // Wait for init "ack"
  MSH_STATUS_t ms = { .word = 0 };
  while (!ms.init_done)
    ms.word = cfg_rd(shimaddr.word, 0, MSH_STATUS);
}


/** Close a DRAM page.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param addr Physical address of page to close.
 */
static void
close_page(pos_t shimaddr, PA addr)
{
  //
  // We put the controller into close-page mode, do a diag read from the
  // page we want to close, and restore the old page mode.
  //
  MSH_CONTROL_t mc, save_mc;

  mc.word = cfg_rd(shimaddr.word, 0, MSH_CONTROL);
  save_mc = mc;

  mc.close_page = 1;
  cfg_wr(shimaddr.word, 0, MSH_CONTROL, mc.word);

  mshim_diag_read(shimaddr, addr);

  cfg_wr(shimaddr.word, 0, MSH_CONTROL, save_mc.word);
}


/////////////////////////////////////////////////////////////////////////////
// BIST support
/////////////////////////////////////////////////////////////////////////////

/** Set up the BIST engine.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param seq_mode Sequence mode; 0/1/2 = read only, write only, write & read.
 * @param block_size Number of DDR burst-of-8's per block, minus 1.
 * @param read_repeat Number of times to repeat the read, minus 1.
 * @param run_count Number of times to run the whole BIST operation.
 * @param capture_en If nonzero, compute a signature of read data.
 * @param data_mode Data mode; 0-5 are fixed patterns with various interleaves,
 *  7 is pseudo-random data.
 * @param data_mask Mask of read data bits to ignore.
 * @param addr_mode Address mode; 0 sequential, 1 pseudo-random.
 * @param addr_start Start address (cacheline offset).  Unused if addr_mode
 *  is random.
 * @param addr_end End address (cacheline offset).  addr_end minus addr_start
 *  must be a power of 2.
 * @param addr_enable Mask of address bits to change.
 * @param addr_cnt Number of addresses to test.
 */
static void
bist_setup(pos_t shimaddr, long seq_mode, long block_size,
           long read_repeat, long run_count, long capture_en, long data_mode,
           long data_mask, long addr_mode, long addr_start, long addr_end,
           long addr_enable, long addr_cnt)
{
  // Set up request bist_ctl function
  MSH_BIST_CTL_t mbc =
  {{
    .capture_en = capture_en,
    .seq_mode = seq_mode,
    .addr_mode = addr_mode,
    .data_mode = data_mode,
    .block_size = block_size,
    .read_repeat = read_repeat,
    .run_count = run_count,
  }};
  cfg_wr(shimaddr.word, 0, MSH_BIST_CTL, mbc.word);

  // Set up BIST_ADDR_ENABLE
  cfg_wr(shimaddr.word, 0, MSH_BIST_ADDR_ENABLE,
         addr_enable << MSH_BIST_ADDR_ENABLE__VAL_SHIFT);

  // Set up BIST_ADDR_START
  cfg_wr(shimaddr.word, 0, MSH_BIST_ADDR_START,
         addr_start << MSH_BIST_ADDR_START__VAL_SHIFT);

  // Set up BIST_ADDR_END
  cfg_wr(shimaddr.word, 0, MSH_BIST_ADDR_END,
         addr_end << MSH_BIST_ADDR_END__VAL_SHIFT);

  // Set up BIST_ADDR_CNT
  cfg_wr(shimaddr.word, 0, MSH_BIST_ADDR_CNT, addr_cnt);

  // Set up BIST_DATA_MASK
  cfg_wr(shimaddr.word, 0, MSH_BIST_DATA_MASK, data_mask);

  //
  // Set up BIST_DATA_SEED_L.  If we aren't capturing, we set it to 0
  // since we assume we're zeroing out memory.
  //
  if (capture_en)
    cfg_wr(shimaddr.word, 0, MSH_BIST_DATA_SEED_L, 1);
  else
    cfg_wr(shimaddr.word, 0, MSH_BIST_DATA_SEED_L, 0);

  // Set up BIST_DATA_SEED_H
  cfg_wr(shimaddr.word, 0, MSH_BIST_DATA_SEED_H, 0);
}


/** Start the BIST engine.
 * @param shimaddr Tile coordinates of the memory shim.
 */
static void
bist_start(pos_t shimaddr)
{
  // Start BIST running
  cfg_wr(shimaddr.word, 0, MSH_BIST_RUN, 1);
}


/** Wait for the BIST engine to finish; panic if it doesn't.
 * @param shimaddr Tile coordinates of the memory shim.
 */
static void
bist_finish(pos_t shimaddr)
{
  // Poll until BIST is done
  int retries = 100000000;
  while (cfg_rd(shimaddr.word, 0, MSH_BIST_RUN) == 0)
  {
    if (retries-- <= 0)
    {
      boot_printf("boot_panic: BIST hung during training\n");
      boot_error(BOOT_ERR_MSH_BIST_HANG);
    }
  }
#if 0
  DBG("    sig_h = 0x%lx, sig_l = 0x%lx\n",
      cfg_rd(shimaddr.word, 0, MSH_BIST_DATA_SIGNATURE_H),
      cfg_rd(shimaddr.word, 0, MSH_BIST_DATA_SIGNATURE_L));
#endif
}


#if 0 // Used in BTK, currently unused in the booter

static void
bist_run(pos_t shimaddr)
{
  bist_start(shimaddr);
  bist_finish(shimaddr);
}

#endif

/** Finish and check a BIST run to verify a configuration setting.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane to test, or -1 to test all lanes.
 * @param data_mode BIST data mode.
 * @param run_count BIST run count.
 * @return 1 if the BIST run passed (produced the expected signature), 0 if
 *  it failed.
 */
static int
check_config_run_bist_finish(pos_t shimaddr, int lane, int data_mode,
                             int run_count)
{
  //
  // Note that all of the signatures defined in this routine are dependent
  // upon the parameters passed to bist_setup in check_config_run_bist_start();
  // if any of those values change, the signatures may also change.
  //

  /** Data related to a particular BIST mode. */
  struct sigs
  {
    /** High half of the expected signature if all indices are tested. */
    const uint64_t* full_high;
    /** Low half of the expected signature if all indices are tested. */
    const uint64_t* full_low;
    /** High half of the expected signature if a specific nybble is tested;
     *  array indexed by nybble number. */
    const uint64_t* high_by_nybble;
    /** Low half of the expected signature if a specific nybble is tested;
     *  array indexed by nybble number. */
    const uint64_t* low_by_nybble;
    /** High half of the expected signature if a specific byte is tested;
     *  array indexed by byte number. */
    const uint64_t* high_by_byte;
    /** Low half of the expected signature if a specific byte is tested;
     *  array indexed by byte number. */
    const uint64_t* low_by_byte;
  };

  const uint64_t full_signature_high_mode7_r10 = 0xc73bbcddf15d5a83;
  const uint64_t full_signature_low_mode7_r10 = 0x1daae5f4904ddc46;

  const uint64_t high_signatures_mode7_r10_x4[] =
  {
    0x1d52d3a402f7586cUL,
    0x340a8a3e79705cb8UL,
    0x33259b13605c2d3cUL,
    0xde608a3ab7151049UL,
    0x3833ff5666c972a6UL,
    0x453bd350c83f4fc4UL,
    0xefc47239b7143487UL,
    0x6365c86442a78fb1UL,
    0x424aeba8ea8e9987UL,
    0xfcb729d649702e8dUL,
    0x539f3f35f89d8c7fUL,
    0xe119c52c48ce19d3UL,
    0x5b3f059c3a86df12UL,
    0xb31dfcced1dba186UL,
    0x0ee82497752cb9dfUL,
    0xdf053b4a7a575a18UL,
  };

  const uint64_t low_signatures_mode7_r10_x4[] =
  {
    0x3ca71bd2dc1013deUL,
    0xab55c9091392bd67UL,
    0x0cfe0ec9359171aeUL,
    0x7f534fbb7244ef12UL,
    0xa2b9414539ac1bc9UL,
    0x9f75af15778cea59UL,
    0x46585c91c5890d6aUL,
    0x2c67ebfc2f3300ddUL,
    0x8568c37f2398972cUL,
    0x073297e0a3ffae98UL,
    0xaacc147f3399711dUL,
    0x9a50a1f6291812efUL,
    0x6c7047ed6925c676UL,
    0x6098b4e04d1d477eUL,
    0x4270b03ca481a9caUL,
    0x4ff268b33692bea6UL,
  };

  const uint64_t high_signatures_mode7_r10_x8[] =
  {
    0xee63e5478ada5e57UL,
    0x2a7eadf4261467f6UL,
    0xba3390db5fab67e1UL,
    0x4b9a068004eee1b5UL,
    0x79c67ea352a3ed89UL,
    0x75bd46c4410ecf2fUL,
    0x2f19458f1a002417UL,
    0x16d6a300fe26b944UL,
  };

  const uint64_t low_signatures_mode7_r10_x8[] =
  {
    0x8a58372f5fcf72ffUL,
    0x6e07a486d79842faUL,
    0x20660ba4de6d2dd6UL,
    0x779552997af7d1f1UL,
    0x9ff0b16b102ae5f2UL,
    0x2d36507d8accbfb4UL,
    0x114216f9b4755d4eUL,
    0x10283d7b025ecb2aUL,
  };

  struct sigs mode7_r10 =
  {
    &full_signature_high_mode7_r10,
    &full_signature_low_mode7_r10,
    high_signatures_mode7_r10_x4,
    low_signatures_mode7_r10_x4,
    high_signatures_mode7_r10_x8,
    low_signatures_mode7_r10_x8,
  };

  const uint64_t full_signature_high_mode7_r0 = 0x5ad7ae2539b5cfacUL;
  const uint64_t full_signature_low_mode7_r0 = 0x85ead0ccc0261a29UL;

  const uint64_t high_signatures_mode7_r0_x4[] =
  {
    0xbac9576a7997a325UL,
    0xba6a7a92d75fa2c7UL,
    0xababc757bdf99b8bUL,
    0x23b9a7233a2bb738UL,
    0x0f391369b0d0cf05UL,
    0x7079daeebf95af48UL,
    0x04c99d0ea5402833UL,
    0xef74f54b82ba301fUL,
    0xfd850a7cd607e4dcUL,
    0xde20d5f16aad0f5aUL,
    0xef50b3d72d0b669fUL,
    0xb5acd3f2d13644a3UL,
    0x384928875370a452UL,
    0xe993879b6f1e576dUL,
    0x922e1dcab712fb88UL,
    0x26e1e130ca4e943dUL,
  };

  const uint64_t low_signatures_mode7_r0_x4[] =
  {
    0xbba98e7342868b31UL,
    0x7a71c581c78f0d37UL,
    0x1f665869606a9fa9UL,
    0x9bc97671befe2eaaUL,
    0x8c026704dc998b80UL,
    0x309d06bb72a767ffUL,
    0x0bd7a092566113a6UL,
    0xda5f99e3841f973eUL,
    0x698e7d23def5c96cUL,
    0x3f62ced35e264eaeUL,
    0x5af842a54dfaa345UL,
    0xb5968016eed6270cUL,
    0xa4069c8f8c76f5deUL,
    0x6cddc1b46c4b28f5UL,
    0xeefaa2055daafaafUL,
    0xb7c3b359b9b57bedUL,
  };

  const uint64_t high_signatures_mode7_r0_x8[] =
  {
    0x5a7483dd977dce4eUL,
    0xd2c5ce51be67e31fUL,
    0x259767a236f0afe1UL,
    0xb16ac6601e4fd780UL,
    0x797271a8851f242aUL,
    0x002bce00c588ed90UL,
    0x8b0d013905db3c93UL,
    0xee1852df44e9a019UL,
  };

  const uint64_t low_signatures_mode7_r0_x8[] =
  {
    0x44329b3e452f9c2fUL,
    0x0145fed41eb2ab2aUL,
    0x3975b1736e18f656UL,
    0x5462e9bd12589eb1UL,
    0xd306633c40f59debUL,
    0x6a84127f630a9e60UL,
    0x4d318df7201bc702UL,
    0xdcd3c19024399b6bUL,
  };

  struct sigs mode7_r0 =
  {
    &full_signature_high_mode7_r0,
    &full_signature_low_mode7_r0,
    high_signatures_mode7_r0_x4,
    low_signatures_mode7_r0_x4,
    high_signatures_mode7_r0_x8,
    low_signatures_mode7_r0_x8,
  };

  //
  // Wait for BIST to finish, then check the signature.
  //
  bist_finish(shimaddr);

  if (data_mode != 7)
    return 0;

  struct sigs* sigp;
  switch (run_count)
  {
  case 0:
    sigp = &mode7_r0;
    break;
  case 10:
    sigp = &mode7_r10;
    break;
  default:
    return 0;
  }

  uint64_t exp_sig_h = *sigp->full_high;
  uint64_t exp_sig_l = *sigp->full_low;
  if (lane >= 0 && !is_ecc_lane(lane))
  {
    if (lane_width() == 4)
    {
      exp_sig_h = sigp->high_by_nybble[lane];
      exp_sig_l = sigp->low_by_nybble[lane];
    }
    else
    {
      exp_sig_h = sigp->high_by_byte[lane];
      exp_sig_l = sigp->low_by_byte[lane];
    }
  }

#if 0
  DBG("bist: exp/act h,l 0x%llx/0x%lx, 0x%llx/0x%lx\n",
      exp_sig_h, cfg_rd(shimaddr.word, 0, MSH_BIST_DATA_SIGNATURE_H),
      exp_sig_l, cfg_rd(shimaddr.word, 0, MSH_BIST_DATA_SIGNATURE_L));
#endif

  return (exp_sig_h == cfg_rd(shimaddr.word, 0, MSH_BIST_DATA_SIGNATURE_H) &&
          exp_sig_l == cfg_rd(shimaddr.word, 0, MSH_BIST_DATA_SIGNATURE_L));
}


/** Do one configuration-checking BIST run.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane to test, or -1 to test all lanes.
 * @param data_mode BIST data mode.
 * @param run_count BIST run count.
 * @return 1 if the BIST run passed (produced the expected signature), 0 if
 *  it failed.
 */
static int
check_config_run_bist(pos_t shimaddr, int lane, int data_mode, int run_count,
                      uint64_t start_addr)
{
  uint64_t mask = test_lane_mask(lane);

  bist_setup(shimaddr, 2, 0x3f, 63, run_count, 1, data_mode, ~mask, 0,
             start_addr >> 6, (start_addr + BIST_SIZE - 1) >> 6, ~0,
             BIST_SIZE);

  bist_start(shimaddr);

  return check_config_run_bist_finish(shimaddr, lane, data_mode, run_count);
}

/////////////////////////////////////////////////////////////////////////////
// General DLL support
/////////////////////////////////////////////////////////////////////////////

/** Assert/deassert the pointer sync signal (thus resetting the PHY's
 *  read/write pointers).
 * @param shimaddr Tile coordinates of the memory shim.
 */
static void
pulse_ptr_sync(pos_t shimaddr)
{
  // Assumes that ptr_sync signal is deasserted on entry

  // Get the initial value of the reg
  MSH_DDR3_MODE_CONTROL_t mdmc = { .word = cfg_rd(shimaddr.word, 0,
                                                  MSH_DDR3_MODE_CONTROL) };
  // Assert
  mdmc.ptr_sync_n = 0;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc.word);

  // Deassert
  mdmc.ptr_sync_n = 1;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc.word);
}


/** Reset a DLL.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param reset_val Bitmask of DLLs to reset: 1 for the transmit DLL, 2 for
 *  the receive DLL.
 */
static void
reset_dll(pos_t shimaddr, int reset_val)
{
  //
  // Get the current register state and calculate the non-reset and
  // reset versions of the register.
  //
  MSH_DDR3_MODE_CONTROL_t mdmc = { .word = cfg_rd(shimaddr.word, 0,
                                                  MSH_DDR3_MODE_CONTROL) };
  mdmc.dll_rst &= ~reset_val;

  MSH_DDR3_MODE_CONTROL_t mdmc_res = mdmc;
  mdmc_res.dll_rst |= reset_val;

  //
  // Put the DLL into reset.
  //
  cfg_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc_res.word);

  //
  // Take it out of reset and then put it right back in immediately; this
  // produces more reliable results than just doing it once.
  //
  cfg_double_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc.word,
                mdmc_res.word);

  //
  // Finally take it out of reset for good.
  //
  cfg_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc.word);

  //
  // Wait 4096 dclk (DLL ref clock, not strobe clock) cycles for TX DLL to
  // lock.
  //
  WAIT_CLOCKS(4096);
}

/////////////////////////////////////////////////////////////////////////////
// Rx DQS delay control
/////////////////////////////////////////////////////////////////////////////

/** Configure one receive bit delay.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane number.
 * @param dqsn_delay DQS- delay value.
 * @param dqsp_delay DQS+ delay value.
 */
static void
write_rx_control(pos_t shimaddr, int lane, int dqsn_delay,
                 int dqsp_delay)
{
  //
  // Change the delay settings.
  //
  int regaddr = MSH_DDR3_DATA0_RX_CONTROL + 8 * REG_INDEX(lane);

  MSH_DDR3_DATA0_RX_CONTROL_t mddrc =
  {
    .word = cfg_rd(shimaddr.word, 0, regaddr)
  };

  if (FIELD_INDEX(lane))
  {
    mddrc.dqs1_dqsn_delay = dqsn_delay;
    mddrc.dqs1_dqsp_delay = dqsp_delay;
  }
  else
  {
    mddrc.dqs0_dqsn_delay = dqsn_delay;
    mddrc.dqs0_dqsp_delay = dqsp_delay;
  }

  cfg_wr(shimaddr.word, 0, regaddr, mddrc.word);
}


/** Assert DLL slave reset for one lane.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane to assert reset for.
 */
static void
assert_rx_dll_slave_reset_lane(pos_t shimaddr, int lane)
{
  write_rx_control(shimaddr, lane, 0x20, 0x20);
}

/** Toggle DLL slave reset for one lane.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane to toggle reset for.
 */
static void
toggle_rx_dll_slave_reset_lane(pos_t shimaddr, int lane)
{
  write_rx_control(shimaddr, lane, 0x20, 0x20);
  write_rx_control(shimaddr, lane, 0, 0);
}


/** Toggle DLL slave reset for all lanes.
 * @param shimaddr Tile coordinates of the memory shim.
 */
static void
toggle_rx_dll_slave_reset(pos_t shimaddr)
{
  int num_c_lanes = num_config_lanes();

  for (int lane = 0; lane < num_c_lanes; lane++)
    toggle_rx_dll_slave_reset_lane(shimaddr, lane);
}


/** Configure receive delays for a lane.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane to configure.
 * @param delay Delay value.
 */
static void
write_rx_delay(pos_t shimaddr, int lane, int delay)
{
  rx_dqsn_delays[lane] = delay;
  rx_dqsp_delays[lane] = delay;
  write_rx_control(shimaddr, lane, delay, delay);
}


#if 0 // Used in BTK, currently unused in the booter

/** Configure a set of receive delays.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param first_lane First lane to configure.
 * @param num_lanes Number of lanes to configure; values are taken from
 *  rx_dqs[np]_delays/rx_dq_delay_offsets.
 */
static void
config_rx_delays(pos_t shimaddr, int first_lane, int num_lanes)
{
  if (rx_dll_enabled)
  {
    //
    // Use config reg values to write to all regs.
    //
    const int last_lane = first_lane + num_lanes - 1;
    for (int lane = first_lane; lane <= last_lane; lane++)
    {
      write_rx_control(shimaddr, lane, rx_dqsn_delays[lane],
                       rx_dqsp_delays[lane]);
    }
  }
}

#endif

/////////////////////////////////////////////////////////////////////////////
// Tx DLL support
/////////////////////////////////////////////////////////////////////////////

/** Hold the transmit and address DLLs in reset.
 * @param shimaddr Tile coordinates of the memory shim.
 */
static void
assert_tx_dll_reset(pos_t shimaddr)
{
  MSH_DDR3_MODE_CONTROL_t mdmc =
    { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL) };
  mdmc.dll_rst |= 1;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc.word);
}


#if 0 // Used in BTK, currently unused in the booter

/** Release the transmit and address DLLs from reset.
 * @param shimaddr Tile coordinates of the memory shim.
 */
static void
deassert_tx_dll_reset(pos_t shimaddr)
{
  MSH_DDR3_MODE_CONTROL_t mdmc =
    { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL) };
  mdmc.dll_rst &= ~1;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc.word);
}

#endif


/** Reset the transmit and address DLLs.
 * @param shimaddr Tile coordinates of the memory shim.
 */
static void
reset_tx_acc_dll(pos_t shimaddr)
{
  reset_dll(shimaddr, 1);
}

/////////////////////////////////////////////////////////////////////////////
// Rx DLL support
/////////////////////////////////////////////////////////////////////////////

/** Hold the receive DLL in reset.
 * @param shimaddr Tile coordinates of the memory shim.
 */
static void
assert_rx_dll_reset(pos_t shimaddr)
{
  MSH_DDR3_MODE_CONTROL_t mdmc =
    { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL) };
  mdmc.dll_rst |= 2;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc.word);
}


/** Release the receive DLL from reset.
 * @param shimaddr Tile coordinates of the memory shim.
 */
static void
deassert_rx_dll_reset(pos_t shimaddr)
{
  MSH_DDR3_MODE_CONTROL_t mdmc =
    { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL) };
  mdmc.dll_rst &= ~2;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc.word);
}


/** Reset the receive DLL.
 * @param shimaddr Tile coordinates of the memory shim.
 */
static void
reset_rx_dll(pos_t shimaddr)
{
  assert_rx_dll_reset(shimaddr);

  if (rx_slave_reset_supported)
    toggle_rx_dll_slave_reset(shimaddr);

  deassert_rx_dll_reset(shimaddr);
}


/** Reset the receive DLL slaves.
 * @param shimaddr Tile coordinates of the memory shim.
 */
static void
reset_rx_dll_slaves(pos_t shimaddr)
{
  int num_c_lanes = num_config_lanes();

  //
  // Reset the slaves for all Rx DQS DLLs.  If a separate slave reset isn't
  // supported, the master will also be reset.  In either case, set the
  // delay values to 0 so we can increment from that after this.
  //
  if (rx_slave_reset_supported)
    toggle_rx_dll_slave_reset(shimaddr);
  else
  {
    for (int lane = 0; lane < num_c_lanes; lane++)
      write_rx_control(shimaddr, lane, 0, 0);

    reset_rx_dll(shimaddr);
  }
}


/////////////////////////////////////////////////////////////////////////////
// Address/command/control delay control
/////////////////////////////////////////////////////////////////////////////

/** Start to configure a DQ's transmit DLL.
 * @param shimaddr Tile coordinates of the memory shim.
 */
static void
change_tx_control_start(pos_t shimaddr)
{
  //
  // Assert ptr sync
  //
  MSH_DDR3_MODE_CONTROL_t mdmc =
    { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL) };
  mdmc.ptr_sync_n = 0;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc.word);

  //
  // Assert Tx DLL reset because DLL vendor says we should before changing
  // Tx control
  //
  assert_tx_dll_reset(shimaddr);
}


/** Finish configuring a DQ's transmit DLL.
 * @param shimaddr Tile coordinates of the memory shim.
 */
static void
change_tx_control_finish(pos_t shimaddr)
{
  //
  // Relocking the DLL
  //
  reset_tx_acc_dll(shimaddr);

  //
  // De-assert ptr sync
  //
  MSH_DDR3_MODE_CONTROL_t mdmc =
    { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL) };
  mdmc.ptr_sync_n = 1;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc.word);
}


/** Compute the address DLL edge retiming value from a delay.
 * @param delay Delay.
 * @return Edge retiming value.
 */
static int
acc_retime_edge(int delay)
{
  if (delay >= 0xb && delay < 0x29)
    return 1;
  else
    return 0;
}


/** Compute the address DLL read pointer initialization value from a delay.
 * @param delay Delay.
 * @return Read pointer initialization value.
 */
static int
acc_init_val(int delay)
{
  if (delay <= 0x28)
    return 0;
  else
    return 1;
}


/** Set the delay values for the address DLL.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param delay 0 Delay value 0.
 * @param delay 1 Delay value 1.
 */
static void
write_acc_control(pos_t shimaddr, long delay0, long delay1)
{
  change_tx_control_start(shimaddr);

  MSH_DDR3_ACC_CONTROL_t mdac =
  {{
    .delay0 = delay0,
    .retime_edge0 = acc_retime_edge(delay0),
    .init_val0 = acc_init_val(delay0),
    .delay1 = delay1,
    .retime_edge1 = acc_retime_edge(delay1),
    .init_val1 = acc_init_val(delay1),
  }};
  cfg_wr(shimaddr.word, 0, MSH_DDR3_ACC_CONTROL, mdac.word);

  change_tx_control_finish(shimaddr);
}

/////////////////////////////////////////////////////////////////////////////
// Tx DQS and DQ delay control
/////////////////////////////////////////////////////////////////////////////

/** Entry in an encoding table.  The retiming encoding routines use
 *  these tables to make it easier to develop different retiming
 *  algorithms. */
struct enc_delay_entry
{
  /** Lowest value to be encoded using this entry. */
  uint8_t range_start;
  /** Lowest value to be encoded using the next entry. */
  uint8_t range_end;
  /** Offset to be added to unencoded value to get encoded value. */
  int8_t adjust;
  /** Retiming value for this range. */
  uint8_t retime_val;
};


/** Encode a delay value using a table.
 * @param table Encoding table.
 * @param log_delay Logical delay.
 * @param delay Pointer to returned delay value.
 * @param retime Pointer to returned retime value.
 */
static void
enc_delay(const struct enc_delay_entry* table,
          uint8_t log_delay, uint8_t* delay, uint8_t* retime)
{
  uint8_t log_delay_low = log_delay & 0x3f;

  while (log_delay_low >= table->range_end)
    table++;

  *delay = (log_delay & 0x40) | (log_delay_low + table->adjust);
  *retime = table->retime_val;
}

//
// Note that the routines which do the DQS/DQ encoding assume that each
// table fully covers the encoding space from 0 to 63; if that is not true,
// the routines will run off the end of the tables and produce incorrect
// results.
//

/** DQS encoding table for A1 and earlier chips, and A2 and later when
 *  not running at 1066 MT/s. */
static const struct enc_delay_entry dqs_enc_table_default[] =
{
  { 0x00, 0x10, +16, 0 },
  { 0x10, 0x20, +16, 1 },
  { 0x20, 0x30, -16, 2 },
  { 0x30, 0x40, -16, 3 },
};

/** DQS encoding table for A2 and later chips at 1066 MT/s. */
static const struct enc_delay_entry dqs_enc_table_a2_1066[] =
{
  { 0x00, 0x10, +12, 0 },
  { 0x10, 0x1f, +12, 1 },
  { 0x1f, 0x30, -20, 2 },
  { 0x30, 0x40, -20, 3 },
};

/** DQS encoding table to use for our current chip. */
static const struct enc_delay_entry* dqs_enc_table = dqs_enc_table_default;

/** Encode a DQ strobe delay value.
 * @param logical_dqs_delay Logical DQ strobe delay.
 * @param dqs_delay Pointer to returned DQ strobe delay value to be written to
 *  shim registers.
 * @param dqs_retime Pointer to returned DQ strobe retime value to be written
 *  to shim registers.
 */
static void
enc_dqs_delay(int logical_dqs_delay, uint8_t* dqs_delay, uint8_t* dqs_retime)
{
  enc_delay(dqs_enc_table, logical_dqs_delay, dqs_delay, dqs_retime);
}


/** DQ encoding table.  (Currently the same for all chips.) */
static const struct enc_delay_entry dq_enc_table[] =
{
  { 0x00, 0x10, +31, 0 },
  { 0x10, 0x20,  -1, 1 },
  { 0x20, 0x30,  -1, 2 },
  { 0x30, 0x40, -33, 3 },
};

/** Encode a DQ delay value.
 * @param logical_dq_delay Logical DQ delay.
 * @param dq_delay Pointer to returned DQ delay value to be written to
 *  shim registers.
 * @param dq_retime Pointer to returned DQ retime value to be written to
 *  shim registers.
 */
static void
enc_dq_delay(int logical_dq_delay, uint8_t* dq_delay, uint8_t* dq_retime)
{
  enc_delay(dq_enc_table, logical_dq_delay, dq_delay, dq_retime);
}


/** Configure a DQ's transmit DLL using raw delay/retime pairs.  Note:
 *  unlike the version in the BTK, this routine does not have a last_update
 *  parameter, and thus does not support calling change_tx_control_{start,
 *  finish}; it's only called in contexts where we're doing that
 *  before/after it's called.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane number.
 * @param dqs_delay Strobe delay.
 * @param dqs_retime Strobe delay.
 * @param dq_delay Data delay.
 * @param dq_retime Data retime.
 */
static void
change_tx_control(pos_t shimaddr, int lane, int dqs_delay,
                  int dqs_retime, int dq_delay, int dq_retime)
{
  //
  // Change the delay setting
  //
  int regaddr = MSH_DDR3_DATA0_TX_CONTROL + 8 * REG_INDEX(lane);

  MSH_DDR3_DATA0_TX_CONTROL_t mddtc =
  {
    .word = cfg_rd(shimaddr.word, 0, regaddr)
  };

  if (FIELD_INDEX(lane))
  {
    mddtc.dqs1_dq_delay = dq_delay;
    mddtc.dqs1_dqs_delay = dqs_delay;
    mddtc.dq_retime = (dq_retime << 2) | (mddtc.dq_retime & 0x3);
    mddtc.dqs_retime = (dqs_retime << 2) | (mddtc.dqs_retime & 0x3);
  }
  else
  {
    mddtc.dqs0_dq_delay = dq_delay;
    mddtc.dqs0_dqs_delay = dqs_delay;
    mddtc.dq_retime = dq_retime | (mddtc.dq_retime & 0xC);
    mddtc.dqs_retime = dqs_retime | (mddtc.dqs_retime & 0xC);
  }

  cfg_wr(shimaddr.word, 0, regaddr, mddtc.word);
}


#if 0 // Used in BTK, currently unused in the booter

/** Configure a DQ's transmit DLL using logical delay values.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane number.
 * @param log_dqs_delay Logical strobe delay.
 * @param log_dq_delay Logical data delay.
 * @param last_update If nonzero, reset the transmit DLL and deassert
 *  pointer sync.  Specify zero if you're just going to call this routine
 *  again without any intervening memory operations (e.g., if you're
 *  setting up a range of DQ's at once).
 */
static void
write_tx_control(pos_t shimaddr, int lane, int log_dqs_delay,
                 int log_dq_delay, int last_update)
{
  int dqs_delay, dqs_retime;
  enc_dqs_delay(log_dqs_delay, &dqs_delay, &dqs_retime);
  int dq_delay, dq_retime;
  enc_dq_delay(log_dq_delay, &dq_delay, &dq_retime);

  change_tx_control_start(shimaddr);

  change_tx_control(shimaddr, lane, dqs_delay, dqs_retime,
                    dq_delay, dq_retime);

  if (last_update)
    change_tx_control_finish(shimaddr);
}

#endif


/** Change the write latency value.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param wrlat New write latency.
 */
static void
change_wrlat(pos_t shimaddr, int wrlat)
{
  MSH_DDR3_PHY_DELAY_t mdpd =
    { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_PHY_DELAY) };
  mdpd.wrlat = wrlat;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_PHY_DELAY, mdpd.word);
}


/** Survey the delays in tx_dqs_delays/tx_dq_delay_offsets and calculate
 *  the minimum cycle used and the number of cycles spanned.
 * @param num_lanes Number of lanes to examine.
 * @param min_cycle_rv Pointer to returned minimum cycle.
 * @param cyc_range_rv Pointer to returned cycle range.
 */
static void
cycle_range(int num_lanes, int* min_cycle_rv, int* cyc_range_rv)
{
  //
  // Find out the range of cycles required.
  //
  int min_cycle = INT_MAX;
  int max_cycle = INT_MIN;

  for (int lane = 0; lane < num_lanes; lane++)
  {
    int dqs_cycle = tx_dqs_delays[lane] / DLL_STEPS_PER_CYCLE;
    int dq_cycle = (tx_dqs_delays[lane] + tx_dq_delay_offsets[lane]) /
                   DLL_STEPS_PER_CYCLE;

    min_cycle = min(min_cycle, min(dqs_cycle, dq_cycle));
    max_cycle = max(max_cycle, max(dqs_cycle, dq_cycle));
  }

  *cyc_range_rv = max_cycle - min_cycle + 1;
  *min_cycle_rv = min_cycle;
}


/** Calculate a write latency value from a set of delays.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param first_lane First lane to configure.
 * @param num_lanes Number of lanes to configure; values are taken from
 *  tx_dqs_delays/tx_dq_delay_offsets.
 */
static int
calc_wrlat(int first_lane, int num_lanes)
{
  uint8_t max_dqs_delay = 0;
  uint8_t min_dqs_delay = ~0;
  uint8_t max_dq_delay = 0;
  uint8_t min_dq_delay = ~0;

  const int last_lane = first_lane + num_lanes - 1;
  for (int lane = first_lane; lane <= last_lane; lane++)
  {
    max_dqs_delay = max(max_dqs_delay, tx_dqs_delays[lane]);
    min_dqs_delay = min(min_dqs_delay, tx_dqs_delays[lane]);

    max_dq_delay = max(max_dq_delay,
                       tx_dqs_delays[lane] + tx_dq_delay_offsets[lane]);
    min_dq_delay = min(min_dq_delay,
                       tx_dqs_delays[lane] + tx_dq_delay_offsets[lane]);
  }

  uint8_t min_delay = min(min_dqs_delay, min_dq_delay);
  uint8_t max_delay = max(max_dqs_delay, max_dq_delay);

  int min_wrlat = max((max_delay / 64) - 1, MIN_ALLOWED_WRLAT);
  int max_wrlat = min(min_delay / 64, MAX_ALLOWED_WRLAT);

#if 0
   DBG("%d entries, min/max dqs %d/%d min/max dq %d/%d min/max %d/%d\n",
       num_lanes, min_dqs_delay, max_dqs_delay, min_dq_delay, max_dq_delay,
       min_wrlat, max_wrlat);
#endif

  if (min_wrlat > max_wrlat)
    // FIXME: should we panic in some cases if we can't find a proper value?
    return -1;
  else
    return max_wrlat;
}


/////////////////////////////////////////////////////////////////////////////
// Rx DQ bit delay control
/////////////////////////////////////////////////////////////////////////////

/** Configure the receive bit delay for all bits on a lane.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane number.
 * @param bit_delay Delay value.
 */
static void
write_rx_bit_delay_lane(pos_t shimaddr, int lane, int bit_delay)
{
  rx_bit_delays[lane] = bit_delay;

  int regaddr = MSH_DDR3_DATA0_RX_CONTROL + 8 * REG_INDEX(lane);

  MSH_DDR3_DATA0_RX_CONTROL_t mddrc = 
  {
    .word = cfg_rd(shimaddr.word, 0, regaddr)
  };

  //
  // Expand to 4 copies for each bit in a nybble.
  //
  bit_delay |= (bit_delay << 3);
  bit_delay |= (bit_delay << 6);

  if (minfo->sdram_width_bits >= 8)
  {
    //
    // Expand once more if we're using x8 or x16 chips.
    //
    bit_delay |= (bit_delay << 12);
    mddrc.bit_delay = bit_delay;
  }
  else
  {
    //
    // Write the proper half of the bitfield, preserving the value for
    // the other nybble.
    //
    if (FIELD_INDEX(lane))
      mddrc.bit_delay = (mddrc.bit_delay & 0xFFF) | (bit_delay << 12);
    else
      mddrc.bit_delay = (mddrc.bit_delay & 0xFFF000) | bit_delay;
  }

  cfg_wr(shimaddr.word, 0, regaddr, mddrc.word);
}


/** Configure a set of receive bit delays.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param num_lanes Number of lanes to configure.
 */
static void
config_rx_bit_delays(pos_t shimaddr, int num_lanes)
{
  for (int lane = 0; lane < num_lanes; lane++)
    write_rx_bit_delay_lane(shimaddr, lane, rx_bit_delays[lane]);
}


/////////////////////////////////////////////////////////////////////////////
// Rx gate delay control
/////////////////////////////////////////////////////////////////////////////

/** Configure a DQ's receive gate timing.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane number.
 * @param phase_delay Phase delay (includes cycle delay).
 * @param dqs_retime Fine delay.
 */
static void
write_rx_control_gate(pos_t shimaddr, int lane,
                      int phase_delay, int fine_delay)
{
  // Save this test value.
  rx_gate_phase_delays[lane] = phase_delay;
  rx_gate_fine_delays[lane] = fine_delay;

  int cycle_delay = phase_delay / 4;
  int phase_in_cycle = phase_delay % 4;

  int regaddr = MSH_DDR3_DATA0_RX_CONTROL_GATE + 8 * REG_INDEX(lane);

  MSH_DDR3_DATA0_RX_CONTROL_GATE_t mddrcg =
  {
    .word = cfg_rd(shimaddr.word, 0, regaddr)
  };

  //
  // For x8 or x16 parts, we need to write both sides, since even though
  // only the dqs0 values are used for gate delays in that case, both
  // values are used to control termination.
  //
  if (FIELD_INDEX(lane) || lane_width() > 4)
  {
    mddrcg.dqs1_gate_cycle_delay = cycle_delay;
    mddrcg.dqs1_gate_phase_delay = phase_in_cycle;
    mddrcg.dqs1_gate_fine_delay = fine_delay;
  }

  if (!FIELD_INDEX(lane) || lane_width() > 4)
  {
    mddrcg.dqs0_gate_cycle_delay = cycle_delay;
    mddrcg.dqs0_gate_phase_delay = phase_in_cycle;
    mddrcg.dqs0_gate_fine_delay = fine_delay;
  }

  cfg_wr(shimaddr.word, 0, regaddr, mddrcg.word);
}

/////////////////////////////////////////////////////////////////////////////
// Rx and Tx delay control
/////////////////////////////////////////////////////////////////////////////

#if 0 // Currently unused here

/** Configure a set of transmit and receive delays.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param first_lane First lane to configure.
 * @param num_lanes Number of lanes to configure; values are taken from
 *  tx_dqs_delays/tx_dq_delay_offsets/rx_dqs[np]_delays.
 */
static void
config_delays(pos_t shimaddr, int first_lane, int num_lanes)
{
  config_tx_delays(shimaddr, first_lane, num_lanes);
  config_rx_delays(shimaddr, first_lane, num_lanes);
}

#endif

/////////////////////////////////////////////////////////////////////////////
// Configuration test support
/////////////////////////////////////////////////////////////////////////////

/** Check a configuration with BIST.  Note that the shim's error state bits
 *  must be cleared before calling this routine; before return, those bits are
 *  cleared.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane to test, or -1 to test all lanes.
 * @param run_count BIST run count.
 * @return 1 if all BIST runs passed (produced the expected signatures);
 *  0 if any failed, or if we saw any ECC errors during any tests.
 */
static int
do_check_config(pos_t shimaddr, int lane, int run_count)
{
  //
  // Pick a start point for our test so that it runs across each pair of
  // ranks.
  //
  for (int rank = 1; rank < minfo->ctl_ranks; rank += 2)
  {
    if (!check_config_run_bist(shimaddr, lane, 7, run_count,
                               rank * minfo->rank_bytes - (BIST_SIZE / 2)) ||
        any_errors(shimaddr))
      return 0;
  }

  //
  // If we have an odd number of ranks, do a final test for the last one.
  //
  if (minfo->ctl_ranks & 1)
  {
    if (!check_config_run_bist(shimaddr, lane, 7, run_count,
                               (minfo->ctl_ranks - 1) * minfo->rank_bytes) ||
        any_errors(shimaddr))
      return 0;
  }

  return 1;
}

/** Check a configuration with a moderate-strength BIST.  (Note that this
 *  currently runs the same final BIST test as check_config_long, but this
 *  might change in the future.)
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane to test, or -1 to test all lanes.
 * @return 1 if all BIST runs passed (produced the expected signatures);
 *  0 if any failed, or if we saw any ECC errors during any tests.
 */
static int
check_config_med(pos_t shimaddr, int lane)
{
  if (!check_config_quick(shimaddr, lane))
  {
#ifdef VERBOSE_CHECK_CONFIG
    DBG("check_config_med(%d) = 0 (quick check failed)\n", lane);
#endif
    return 0;
  }

  int rv = do_check_config(shimaddr, lane, 10);
#ifdef VERBOSE_CHECK_CONFIG
  DBG("check_config_med(%d) = %d\n", lane, rv);
#endif
  return rv;
}


/** Check a configuration with a high-strength BIST.  (Note that this
 *  currently runs the same final BIST test as check_config_med, but this
 *  might change in the future.)
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane to test, or -1 to test all lanes.
 * @param exp_fail Nonzero if our tests should be optimized for expected
 *  failure.
 * @return 1 if all BIST runs passed (produced the expected signatures);
 *  0 if any failed, or if we saw any ECC errors during any tests.
 */
static int
check_config_long(pos_t shimaddr, int lane, int exp_fail)
{
  if (!check_config_quick(shimaddr, lane))
  {
#ifdef VERBOSE_CHECK_CONFIG
    DBG("check_config_long(%d) = 0 (quick check failed)\n", lane);
#endif
    return 0;
  }

  if (exp_fail && !do_check_config(shimaddr, lane, 0))
  {
#ifdef VERBOSE_CHECK_CONFIG
    DBG("check_config_long(%d) = 0 (run_count 0 failed)\n", lane);
#endif
    return 0;
  }

  int rv = do_check_config(shimaddr, lane, 10);
#ifdef VERBOSE_CHECK_CONFIG
  DBG("check_config_long(%d) = %d\n", lane, rv);
#endif
  return rv;
}

#if 0
//
// To time each BIST run, enable this code, then comment out
// check_config_long(), above.
//

uint_reg_t cc_cycles[4];
uint_reg_t* cc_cyp = &cc_cycles[2];
int cc_count[4];
int* cc_ctp = &cc_count[2];

static int
dbg_check_config_long(pos_t shimaddr, int lane, int exp_fail)
{
  uint_reg_t orig_cycle = get_cycle_count();
  int rv;

  if (!check_config_quick(shimaddr, lane))
    rv = -2;
  else
  {
    if (exp_fail && !do_check_config(shimaddr, lane, 0))
      rv = -1;
    else
      rv = do_check_config(shimaddr, lane, 10);
  }

  uint_reg_t new_cycle = get_cycle_count();
  cc_cyp[rv] += new_cycle - orig_cycle;
  cc_ctp[rv]++;

  return rv;
}

static inline int
check_config_long(pos_t shimaddr, int lane, int exp_fail)
{
  return dbg_check_config_long(shimaddr, lane, exp_fail) > 0;
}

static void
init_bist_timing()
{
  for (int i = -2; i <= 1; i++)
    cc_ctp[i] = cc_cyp[i] = 0;
}

static void
fini_bist_timing()
{
  for (int i = -2; i <= 1; i++)
  {
    if (cc_ctp[i] == 0)
      cc_ctp[i] = 1;
    boot_printf("when check_config returns %2d: %3d calls, %8lld mean cycles\n",
        i, cc_ctp[i], cc_cyp[i] / cc_ctp[i]);
  }
}

#else

static inline void
init_bist_timing()
{
}

static inline void
fini_bist_timing()
{
}

#endif


/////////////////////////////////////////////////////////////////////////////
// Gate leveling support
/////////////////////////////////////////////////////////////////////////////

/** Get gate leveling data.  Essentially we're trying a range of different
 *  phase delay values, and figuring out which ones give us a positive
 *  leveling response.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param addr Memory address to use.
 * @param num_lanes Number of lanes to train.
 * @param edge_results Returned data. This is an array of num_lanes
 *  values.  Each value will be set to a bitmap of trials which gave
 *  a positive response, where the bit number is equal to the phase delay.
 */
static void
gather_gate_level(pos_t shimaddr, PA addr, int num_lanes,
                  uint64_t* edge_results)
{
  //
  // For each DQS, we create a bitmap of the parameter values that work.
  // We declare that every set of parameters works initially; then, as soon
  // as we get a failure, we mark it as failed.  The 0th bit is the first
  // set of parameters tried, the 1st bit is the second, etc.
  //
  for (int lane = 0; lane < num_lanes; lane++)
    edge_results[lane] = ~ (uint64_t) 0;

  //
  // Enable gate leveling for this edge.
  //
  MSH_DDR3_RDLVL_CONTROL_t mdrc = { .word = cfg_rd(shimaddr.word, 0,
                                                   MSH_DDR3_RDLVL_CONTROL) };
  mdrc.en = 1;
  mdrc.gate_en = 1;
  mdrc.edge = 0;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_RDLVL_CONTROL, mdrc.word);

  //
  // This mask contains the bit associated with the current set of parameters.
  //
  uint64_t result_mask = 1;

  for (int phase = 0; phase < PHASE_RANGE; phase++)
  {
    // Write this delay value for all dqs
    for (int lane = 0; lane < num_lanes; lane++)
      write_rx_control_gate(shimaddr, lane, phase, 0);

    const int samples = 1;

    for (int sample = 0; sample < samples; sample++)
    {
      pulse_ptr_sync(shimaddr);

      //
      // Read data using a diag read, then examine the read leveling
      // response data set by that read.
      //
      mshim_diag_read(shimaddr, addr);

      for (int lane = 0; lane < num_lanes; lane++)
      {
        int regaddr = MSH_DDR3_DATA0_RX_CONTROL_GATE +
                      8 * REG_INDEX(lane);
        MSH_DDR3_DATA0_RX_CONTROL_GATE_t mddrcg =
        {
          .word = cfg_rd(shimaddr.word, 0, regaddr)
        };

        if (((mddrcg.rdlvl_resp >> (FIELD_INDEX(lane) * 4)) & 1) == 0)
          edge_results[lane] &= ~result_mask;
      }
    }

    result_mask <<= 1;
  }

  //
  // Disable gate leveling.
  //
  mdrc.en = 0;
  mdrc.gate_en = 0;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_RDLVL_CONTROL, mdrc.word);

  // FIXME: not sure this is necessary.
  pulse_ptr_sync(shimaddr);

  //
  // Close the page we were accessing for gate leveling so that any
  // subsequent mode register write will succeed.
  //
  close_page(shimaddr, addr);

#ifdef MSH_DEBUG
  boot_printf("Lane DQS   Gate leveling results:\n");

  for (int lane = 0; lane < num_lanes; lane++)
  {
    boot_printf("%4d %3d: ", lane, lane2dqs(lane));
    result_mask = 1;
    for (int phase = 0; phase < PHASE_RANGE; phase++)
    {
      boot_printf("%c", (edge_results[lane] & result_mask) ? '*' : '.');
      result_mask <<= 1;
    }
    boot_printf("\n");
  }
#endif
}


/** Get gate delay candidates.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param num_lanes Number of lanes to train.
 * @param edge_results Returned data. This is an array of num_lanes
 *  values.  Each value will be set to a bitmap of trials which gave
 *  a positive response, where the bit number is equal to the phase delay.
 */
static void
get_gate_delay_candidates(pos_t shimaddr, int num_lanes,
                          uint64_t gate_results[NLANE])
{
  //
  // Gate leveling.
  //

  //
  // Gather gate leveling results using trailing edge sampling.  To speed
  // things up, we're not testing every possible value, and it seems to
  // work reasonably well.
  //
  const int num_gate_settings = PHASE_RANGE;

  uint64_t tmp_gate_results[NLANE];

  for (int lane = 0; lane < num_lanes; lane++)
    gate_results[lane] = 0;

  for (int rank = 0; rank < minfo->ctl_ranks; rank++)
  {
    PA addr = rank * minfo->rank_bytes;

    gather_gate_level(shimaddr, addr, num_lanes, tmp_gate_results);

    //
    // The list of gate results will include a result for every lane.  Do
    // edge detect on the results to turn them into a bitmap of settings to
    // return.
    //
    for (int lane = 0; lane < num_lanes; lane++)
    {
      //
      // Search the bitmap for the rising edges.  Ultimately we want the
      // trailing edge of the gate signal to line up with the rising edge of
      // the last dqs pulse of the read burst.
      //
      uint64_t edges = 0;

      for (int i = 0; i < num_gate_settings - 1; i++)
        if (((tmp_gate_results[lane] >> i) & 3) == 2)
          edges |= 1UL << (i + 1);

      gate_results[lane] |= edges;
    }
  }
}


/////////////////////////////////////////////////////////////////////////////
// Delay adjustment
/////////////////////////////////////////////////////////////////////////////

// target_dq_offset global variable instead of function

/** Constructor for avoid zone bitmaps */
#define AVOID(lo, hi) (RMASK64((hi) - (lo) + 1) << (lo))

//
// Note: changes to the avoid zones might require changes to
// TX_RANGE_{STEP,START}.
//

/** DQS avoid table for A2 and later chips. */
static const uint64_t dqs_avoid_table_a2 =
  AVOID( 0,  2) |
  AVOID(11, 18) |
  AVOID(27, 34) |
  AVOID(43, 50) |
  AVOID(59, 63);

/** DQS avoid table for A2 and later chips.  Right now this is the same as
 *  the DQS table. */
static const uint64_t dq_avoid_table_a2 =
  AVOID( 0,  2) |
  AVOID(11, 18) |
  AVOID(27, 34) |
  AVOID(43, 50) |
  AVOID(59, 63);

/** DQS translation table, set as part of initial setup. */
static uint64_t dqs_avoid_table = 0;

/** DQ translation table, set as part of initial setup. */
static uint64_t dq_avoid_table = 0;


/** Given a bitmap, and a bit position within it, return the bit at the bit
 *  position, along with the limits of the contiguous string of identical
 *  bits which contain the bit position.  For instance: if you had a 4-bit
 *  bitmap bm containing binary 0011, bit_range(bm, 0) and bit_range(bm, 1)
 *  would both return 1, with a lo_bit of 0 and a hi_bit of 1.  Similarly,
 *  bit_range(bm, 2) and bit_range(bm, 3) would both return 0, with a
 *  lo_bit of 2 and a hi_bit of 3.  0 <= *lo_bit <= bit <= *hi_bit <= 63.
 * @param bitmap Bitmap to search.
 * @param bit Bit position to test.
 * @param lo_bit Pointer to a value which is set to the position of the
 *  low bit in the contiguous identical string of bits which contain the
 *  bit position.
 * @param hi_bit Pointer to a value which is set to the position of the
 *  high bit in the contiguous identical string of bits which contain the
 *  bit position.
 * @return Value of the bit at the tested bit position.
 */
static int
bit_range(uint64_t bitmap, int bit, int* lo_bit, int* hi_bit)
{
  int retval = (bitmap >> bit) & 1;

  //
  // The bit-twiddling below searches for the limits of a string of 0's,
  // so if the selected bit is a 1 we invert the mask.
  //
  if (retval)
    bitmap = ~bitmap;

  //
  // The low mask contains all bits lower than the selected bit.
  //
  uint64_t lo_mask = bitmap & ((1UL << bit) - 1);
  *lo_bit = 64 - __builtin_clzll(lo_mask);

  //
  // The high mask contains all bits higher than the selected bit.
  //
  uint64_t hi_mask = bitmap & ((~0UL << bit) << 1);
  *hi_bit = __builtin_ctzll(hi_mask) - 1;

  return retval;
}


/** This routine is useful when you've called bit_range() and found that
 *  the bit you tested doesn't have the desired value, and want the nearest
 *  bit position to that bit which does have the desired value.
 *  As with bit_range(), 0 <= lo_bit <= bit <= hi_bit <= 63.
 * @param bit Bit that the returned value should be close to; must be
 *  within the invalid range defined by lo_bit and hi_bit.
 * @param lo_bit Lowest bit position in the invalid range.
 * @param hi_bit Highest bit position in the invalid range.
 * @return The bit position outside the invalid range, closest to bit.
 *  Note that if lo_bit == 0 and hi_bit == 63, the return value is undefined,
 *  since all bits are invalid.
 */
static int
closest_bit_outside_bit_range(int bit, int lo_bit, int hi_bit)
{
  if (lo_bit <= 0)
    return hi_bit + 1;
  else if (hi_bit >= 63)
    return lo_bit - 1;
  else if (bit - lo_bit < hi_bit - bit)
    return lo_bit - 1;
  else
    return hi_bit + 1;
}


/** This routine is useful when you've called bit_range() and found that
 *  the bit you tested doesn't have the desired value, and want the nearest
 *  bit position to that bit which does have the desired value.
 *  As with bit_range(), 0 <= lo_bit <= bit <= hi_bit <= 63.
 * @param bit Bit that the returned value should be close to; must be
 *  within the invalid range defined by lo_bit and hi_bit.
 * @param lo_bit Lowest bit position in the invalid range.
 * @param hi_bit Highest bit position in the invalid range.
 * @return The bit position outside the invalid range, closest to bit.
 *  Note that if lo_bit == 0 and hi_bit == 63, the return value is undefined,
 *  since all bits are invalid.
 */
static int
closest_bit_below_bit_range(int lo_bit, int hi_bit)
{
  if (lo_bit <= 0)
    return hi_bit + 1;
  else
    return lo_bit - 1;
}


/** This routine is useful when you've called bit_range() and found that
 *  the bit you tested doesn't have the desired value, and want the nearest
 *  bit position to that bit which does have the desired value.
 *  As with bit_range(), 0 <= lo_bit <= bit <= hi_bit <= 63.
 * @param bit Bit that the returned value should be close to; must be
 *  within the invalid range defined by lo_bit and hi_bit.
 * @param lo_bit Lowest bit position in the invalid range.
 * @param hi_bit Highest bit position in the invalid range.
 * @return The bit position outside the invalid range, closest to bit.
 *  Note that if lo_bit == 0 and hi_bit == 63, the return value is undefined,
 *  since all bits are invalid.
 */
static int
closest_bit_above_bit_range(int lo_bit, int hi_bit)
{
  if (hi_bit >= 63)
    return lo_bit - 1;
  else 
    return hi_bit + 1;
}


/** Translate a delay to a new value, if needed, to avoid delays we don't
 *  want to use.
 * @param current_delay Delay to translate.
 * @param table Translation table.
 * @param lane Lane number.
 * @param dir Specify direction of adjustment: -1 picks next valid lower
 *  value, 0 picks closest, 1 picks next valid higher value.
 * @return Potentially translated delay value.
 */
static uint8_t
check_avoid(uint8_t current_delay, uint64_t table, int lane, int dir)
{
  int low_del = current_delay & 63;
  int high_del = current_delay & ~63;

  int lo_bit, hi_bit;
  if (!bit_range(table, current_delay, &lo_bit, &hi_bit))
    return current_delay;
  else
  {
    switch (dir)
    {
    case -1:
      return high_del | closest_bit_below_bit_range(lo_bit, hi_bit);
    case 1:
      return high_del | closest_bit_above_bit_range(lo_bit, hi_bit);
    case 0:
    default:
      return high_del | closest_bit_outside_bit_range(low_del, lo_bit, hi_bit);
    }
  }
}


/** Translate a DQ delay to a new value, if needed, to avoid delays we don't
 *  want to use.
 * @param current_delay Delay to translate.
 * @param lane Lane number.
 * @param dir Specify direction of adjustment: -1 picks next valid lower
 *  value, 0 picks closest, 1 picks next valid higher value.
 * @return Potentially translated delay value.
 */
static uint8_t
check_dq_avoid(uint8_t current_delay, int lane, int dir)
{
  return check_avoid(current_delay, dq_avoid_table, lane, dir);
}


/** Translate a DQS delay to a new value, if needed, to avoid delays we don't
 *  want to use.
 * @param current_delay Delay to translate.
 * @param lane Lane number.
 * @param dir Specify direction of adjustment: -1 picks next valid lower
 *  value, 0 picks closest, 1 picks next valid higher value.
 * @return Potentially translated delay value.
 */
static uint8_t
check_dqs_avoid(uint8_t current_delay, int lane, int dir)
{
  return check_avoid(current_delay, dqs_avoid_table, lane, dir);
}


/** Provide the limits of the non-avoid zone containing a given DQS delay,
 *  which must not be in an avoid zone.
 * @param delay The DQS delay.
 * @param lo_delay The pointed-to value is set to the lowest delay less
 *  than or equal to delay which is not in an avoid zone.
 * @param hi_delay The pointed-to value is set to the highest delay greater
 *  than or equal to delay which is not in an avoid zone.
 */
static void
dqs_non_avoid_range(int delay, int* lo_delay, int* hi_delay)
{
  (void) bit_range(dqs_avoid_table, delay & 63, lo_delay, hi_delay);
  *lo_delay |= (delay & ~63);
  *hi_delay |= (delay & ~63);
}


static void
set_up_tx_delays(int lane, uint8_t dqs_delay, int dir)
{
  tx_dqs_delays[lane] = check_dqs_avoid(dqs_delay + minfo->dqs_tweak, lane,
                                        dir);
  tx_dq_delay_offsets[lane] =
    (int) check_dq_avoid(tx_dqs_delays[lane] + target_dq_offset +
                         minfo->dq_tweak, lane, dir) - tx_dqs_delays[lane];
}


/** Survey the delays in tx_dqs_delays/tx_dq_delay_offsets and modify them
 *  if needed so that they fit into 2 cycles.  Then verify that we still
 *  have a working configuration.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param num_lanes Number of lanes to examine.
 * @return Nonzero if things are too widely spaced to fit into 2 cycles, or
 *  the final configuration test failed; 0 otherwise.
 */
static int
squeeze_tx_delays(pos_t shimaddr, int num_lanes)
{
  int min_cycle;
  int cyc_range;
  cycle_range(num_lanes, &min_cycle, &cyc_range);

  //
  // Make sure there's a viable range of cycles required.
  //
  if (cyc_range > 4)
  {
    boot_printf("msh%d: can't cope with delays over more than 4 cycles, "
                "shim ignored\n", msh_port(shimaddr));
    return 1;
  }

  //
  // If the delays are spread over 3 cycles we will move any delays
  // outside the 2-cycle range to the closest edge.  Hopefully that
  // won't be too big of a nudge.
  //
  if (cyc_range > 2)
  {
    DBG("Squeezing Tx delays for data lanes\n");

    int min_dqs = (min_cycle + 1) * DLL_STEPS_PER_CYCLE;
    int max_dqs = min_dqs + (2 * DLL_STEPS_PER_CYCLE) - 1;

    //
    // Fix any lanes with out-of-range DQS or DQ delays.
    //
    for (int lane = 0; lane < num_lanes; lane++)
    {
      int dqs_delay = tx_dqs_delays[lane];
      int dq_delay = dqs_delay + tx_dq_delay_offsets[lane];

      if (dqs_delay < min_dqs || dq_delay < min_dqs)
      {
        int new_dqs_delay = min_dqs;

        if (target_dq_offset < 0)
          new_dqs_delay -= target_dq_offset;

        DBG("Lane %d dqs delay adjusted up: %d -> %d\n",
            lane, dqs_delay, new_dqs_delay);

        set_up_tx_delays(lane, new_dqs_delay, 1);
      }
      else if (dqs_delay > max_dqs || dq_delay > max_dqs)
      {
        int new_dqs_delay = max_dqs;

        if (target_dq_offset > 0)
          new_dqs_delay -= target_dq_offset;

        DBG("Lane %d dqs delay adjusted down: %d -> %d\n",
            lane, dqs_delay, new_dqs_delay);

        set_up_tx_delays(lane, new_dqs_delay, -1);
      }
    }
  }

  return 0;
}


/** Configure a set of transmit delays.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param first_lane First lane to configure.
 * @param num_lanes Number of lanes to configure; values are taken from
 *  tx_dqs_delays/tx_dq_delay_offsets.
 * @return Nonzero if we successfully configured the delays; zero if not,
 *  which happens if we can't find an appropriate wrlat value.
 */
static int
config_tx_delays(pos_t shimaddr, int first_lane, int num_lanes)
{
  int wrlat = calc_wrlat(first_lane, num_lanes);

  if (wrlat < 0)
    return 0;

  change_wrlat(shimaddr, wrlat);

  change_tx_control_start(shimaddr);

  //
  // Transmit delays.
  //
  const int last_lane = first_lane + num_lanes - 1;
  for (int lane = first_lane; lane <= last_lane; lane++)
  {
    uint8_t raw_dqs_delay;
    uint8_t raw_dqs_retime;
    uint8_t raw_dq_delay;
    uint8_t raw_dq_retime;

    enc_dqs_delay(tx_dqs_delays[lane] - wrlat * 64, &raw_dqs_delay,
                  &raw_dqs_retime);

    enc_dq_delay(tx_dqs_delays[lane] - wrlat * 64 +
                 tx_dq_delay_offsets[lane], &raw_dq_delay, &raw_dq_retime);

    //
    // The BTK code goes to a fair bit of trouble to avoid reading registers
    // when we're going to write both halves of them.  We aren't bothering
    // with that here, since our register accesses are so much faster.  If
    // this ever turned out to have a large performance impact, we could
    // change our minds about that.
    //
    change_tx_control(shimaddr, lane, raw_dqs_delay, raw_dqs_retime,
                      raw_dq_delay, raw_dq_retime);
  }

  change_tx_control_finish(shimaddr);

  return 1;
}


/** Set and then configure a transmit delay for a data lane.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane to configure.
 * @param tx_delay Delay to set.
 * @return Nonzero if we successfully configured the delay; zero if not,
 *  which happens if we can't find an appropriate wrlat value.
 */
static int
config_data_tx_delay(pos_t shimaddr, int lane, int tx_delay)
{
  set_up_tx_delays(lane, tx_delay, 0);
  return config_tx_delays(shimaddr, lane, 1);
}


/** Set and then configure a transmit delay for the ECC lanes.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param tx_delay Delay to set.
 * @return Nonzero if we successfully configured the delay; zero if not,
 *  which happens if we can't find an appropriate wrlat value.
 */
static int
config_ecc_tx_delays(pos_t shimaddr, int tx_delay)
{
  set_up_tx_delays(low_ecc_lane(), tx_delay, 0);
  if (high_ecc_lane() >= 0)
    set_up_tx_delays(high_ecc_lane(), tx_delay, 0);
  return config_tx_delays(shimaddr, 0, num_config_lanes());
}


/** Set and configure a gate delay.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane to configure.
 * @param phase Phase delay to configure (the fine delay will be set to zero).
 * @param gate_results Full set of gate delay candidates (per-lane bitmap).
 */
static void
set_gate_delay(pos_t shimaddr, int lane, int phase,
               uint64_t gate_results[NLANE])
{
  write_rx_control_gate(shimaddr, lane, phase, 0);

  if (is_ecc_lane(lane) && lane_width() == 4)
  {
    //
    // We don't want to just use the same gate delay as the other ECC
    // lane; we pick the closest one in the target lane's list.
    //
    uint64_t other_mask = gate_results[lane + 1];
    int other_phase;

    int lo_bit, hi_bit;
    if (bit_range(other_mask, phase, &lo_bit, &hi_bit))
    {
      //
      // The same delay works on the other lane.
      //
      other_phase = phase;
    }
    else
    {
      //
      // The same delay doesn't work, so let's see what's closest.  Note
      // this code assumes that there is at least one valid gate delay for
      // the other lane.
      //
      other_phase = closest_bit_outside_bit_range(phase, lo_bit, hi_bit);
    }

    write_rx_control_gate(shimaddr, lane + 1, other_phase, 0);
  }
}


/////////////////////////////////////////////////////////////////////////////
// Delay search support
/////////////////////////////////////////////////////////////////////////////

// reorder_delays implements both reorder_phase_delays and reorder_dqs_delays

/** Reorder a list of delays in decreasing order of proximity to a given
 *  value.
 * @param good_delay Delay to order based on.
 * @param delays Array of delays to order.
 * @param ndelays Number of delays in delays[].
 */
static void
reorder_delays(int good_delay, int delays[NLANE], int ndelays)
{
  //
  // In the most common case, the number of input values is 3, and even in
  // the worst (non-write-leveling) case, it's still small (24), so we use
  // a simple bubble sort.
  //
  for (int i = 0; i < ndelays - 1; i++)
    for (int j = i + 1; j < ndelays; j++)
      if (abs(good_delay - delays[i]) > abs(good_delay - delays[j]))
      {
        int tmp = delays[i];
        delays[i] = delays[j];
        delays[j] = tmp;
      }
}


static int
check_this_tx_delay(pos_t shimaddr, int lane, int tx_delay)
{
  if (is_ecc_lane(lane))
  {
    if (!config_ecc_tx_delays(shimaddr, tx_delay))
      return 0;
  }
  else
  {
    if (!config_data_tx_delay(shimaddr, lane, tx_delay))
      return 0;
  }

  return check_config_quick(shimaddr, lane);
}


static int
nudge_tx_delay(pos_t shimaddr, int lane, int mid_delay)
{
  DBG("  Nudging lane %d\n", lane);

  //
  // Choose the midpoint of the working range as a starting point.  Because
  // that delay might be in an avoid zone, adjust it to get it into a
  // usable zone.
  //
  int tx_delay = check_dq_avoid(mid_delay, lane, 0);

  //
  // Find out the limits of the non-avoid zone our delay is in.
  //
  int use_zone_top, use_zone_bottom;
  dqs_non_avoid_range(tx_delay, &use_zone_bottom, &use_zone_top);

  //
  // Test the range of working values within this "use" zone
  // and choose the middle of that working range.
  //
  int use_zone_min = -1;
  for (int delay = use_zone_bottom; delay <= use_zone_top; delay++)
  {
      if (check_this_tx_delay(shimaddr, lane, delay))
      {
        use_zone_min = delay;
        break;
      }
  }
  if (use_zone_min < 0)
  {
    boot_printf("msh%d: lane %d: use zone min is -1\n", msh_port(shimaddr),
                lane);
    return 1;
  }

  int use_zone_max = -1;
  for (int delay = use_zone_top; delay >= use_zone_bottom; delay--)
  {
      if (check_this_tx_delay(shimaddr, lane, delay))
      {
        use_zone_max = delay;
        break;
      }
  }
  if (use_zone_max < 0)
  {
    boot_printf("msh%d: lane %d: use zone max is -1\n", msh_port(shimaddr),
                lane);
    return 1;
  }

  //
  // Display some info about the usable zone.
  //
  DBG("    Use zone min is %d, use zone max is %d, use zone width is %d\n",
      use_zone_min, use_zone_max, use_zone_max - use_zone_min + 1);
  if (use_zone_min != use_zone_bottom)
    DBG("    Use zone min %d is not the same as use zone bottom %d\n",
        use_zone_min, use_zone_bottom);
  if (use_zone_max != use_zone_top)
    DBG("    Use zone max %d is not the same as use zone top %d\n",
        use_zone_max, use_zone_top);

  tx_delay = (use_zone_min + use_zone_max) / 2;
  if (is_ecc_lane(lane))
    config_ecc_tx_delays(shimaddr, tx_delay);
  else
    config_data_tx_delay(shimaddr, lane, tx_delay);

  DBG("    Lane %2d: Tx DQS delay nudged from %3d to %3d\n", lane, mid_delay,
      tx_delay);

  return 0;
}

/** Return the length of a gate fine delay step in picoseconds. */
static int
gate_fine_delay_size()
{
  return (core_voltage_uv <= 925000) ? 40 : 30;
}

/** Return the length of a gate phase delay step in picoseconds. */
static int
gate_phase_delay_size()
{
  return tCK_fs / (4 * 1000);
}

/** Return the total delay for a given gate phase and fine setting. */
static int
gate_time(int phase_delay, int fine_delay)
{
  return phase_delay * gate_phase_delay_size() +
         fine_delay * gate_fine_delay_size();
}

/** Return the maximum fine gate delay. */
static int
max_fine_gate_delay()
{
  return min(7, gate_phase_delay_size() / gate_fine_delay_size());
}

/* Decrement a gate phase/fine delay pair. */
static void
dec_gate_delay(int* phase_delay, int* fine_delay)
{
  int next_phase_delay = *phase_delay;
  int next_fine_delay = *fine_delay - 1;

  if (next_fine_delay < 0)
  {
    next_fine_delay = max_fine_gate_delay();
    next_phase_delay--;
  }

  *phase_delay = next_phase_delay;
  *fine_delay = next_fine_delay;
}

/* Increment a gate phase/fine delay pair. */
static void
inc_gate_delay(int* phase_delay, int* fine_delay)
{
  int next_phase_delay = *phase_delay;
  int next_fine_delay = *fine_delay + 1;

  if (next_fine_delay > max_fine_gate_delay())
  {
    next_fine_delay = 0;
    next_phase_delay++;
  }

  *phase_delay = next_phase_delay;
  *fine_delay = next_fine_delay;
}

/** Shmoo the gate delay a bit to get a better value.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane number to train.
 */
static void
tune_gate_delay(pos_t shimaddr, int lane)
{
  int test_lane = is_ecc_lane(lane) ? -1 : lane;

  int first_phase = rx_gate_phase_delays[lane];

  //
  // Note that, to find the max and min working gate delays, we are
  // intentionally testing from values above and below the initial working
  // value and then searching back toward the initial value.  In other
  // words, we don't just search outward in both directions from the
  // initial working value.  This filters out the case where the initial
  // value was a marginal value, values on either side of it fail and we
  // end up with what appears (falsely) to be a tiny working range.
  //

  //
  // Find smallest working gate delay.
  //
  DBG("    Lane %2d: Searching for min gate delay from initial value %d.%d\n",
      lane, first_phase, 0);

  int min_phase = -1;
  int min_fine = -1;
  int phase;
  int fine;

  //
  // If we're starting at the bottom, there's no point in doing any
  // searching.
  //
  if (first_phase <= 0)
  {
    min_phase = 0;
    min_fine = 0;
  }
  else
  {
    //
    // Start 3 steps below the initial working delay and search up by phase
    // to find the first working phase delay.
    //
    phase = max(0, first_phase - 3);

    while (phase < 32)
    {
      DBG("    Lane %2d: Testing gate delay %d.%d\n", lane, phase, 0);
      write_rx_control_gate(shimaddr, lane, phase, 0);
      if (check_config_quick(shimaddr, test_lane))
        break;
      else
        phase++;
    }

    //
    // Now search down with fine steps to find the first failing phase/fine
    // delay.
    //
    fine = 0;
    while (phase >= 0)
    {
      //
      // Calculate next value to test.
      //
      dec_gate_delay(&phase, &fine);

      //
      // If this value is below the minimum, we're done.
      //
      if (phase < 0)
      {
        inc_gate_delay(&phase, &fine);
        min_phase = phase;
        min_fine = fine;
        break;
      }
      else
      {
        DBG("    Lane %2d: Testing gate delay %d.%d\n", lane, phase, fine);
        write_rx_control_gate(shimaddr, lane, phase, fine);
        //
        // If this value fails, we're done.
        //
        if (!check_config_quick(shimaddr, test_lane))
        {
          inc_gate_delay(&phase, &fine);
          min_phase = phase;
          min_fine = fine;
          break;
        }
      }
    }
  }

  //
  // Find largest working gate delay.
  //
  DBG("    Lane %2d: Searching for max gate delay from initial value %d.%d\n",
      lane, first_phase, 0);

  int max_phase = -1;
  int max_fine = -1;

  //
  // If we're starting at the top, there's no point in doing any
  // searching.
  //
  if (first_phase >= 31)
  {
    min_phase = 31;
    min_fine = 0;
  }
  else
  {
    //
    // Start 3 steps above the initial working delay and search down by
    // phase to find the first working phase delay.
    //
    phase = min(31, first_phase + 3);

    while (phase >= 0)
    {
      DBG("    Lane %2d: Testing gate delay %d.%d\n", lane, phase, 0);
      write_rx_control_gate(shimaddr, lane, phase, 0);
      if (check_config_quick(shimaddr, test_lane))
        break;
      else
        phase--;
    }

    //
    // Now search up with fine steps to find the first failing phase/fine
    // delay.
    //
    fine = 0;
    while (phase <= 31)
    {
      //
      // Calculate next value to test.
      //
      inc_gate_delay(&phase, &fine);

      //
      // If this value is above the maximum, we're done.
      //
      if (phase > 31)
      {
        dec_gate_delay(&phase, &fine);
        max_phase = phase;
        max_fine = fine;
        break;
      }
      else
      {
        DBG("    Lane %2d: Testing gate delay %d.%d\n", lane, phase, fine);
        write_rx_control_gate(shimaddr, lane, phase, fine);
        //
        // If this value fails, we're done.
        //
        if (!check_config_quick(shimaddr, test_lane))
        {
          dec_gate_delay(&phase, &fine);
          max_phase = phase;
          max_fine = fine;
          break;
        }
      }
    }
  }

  if (min_fine < 0 || max_fine < 0)
  {
    //
    // This should never happen, since the initial value should have passed
    // a strong test, but better to be paranoid here.
    //
    DBG("    Lane %2d: gate delay tuning failed (min %d.%d, max %d.%d), "
        "using first %d\n", lane, min_phase, min_fine, max_phase, max_fine,
        first_phase);

    write_rx_control_gate(shimaddr, lane, first_phase, 0);
  }

  //
  // Calculate a centered gate delay time and then the closest phase/fine
  // setting to it.
  //
  int min_gate_time = gate_time(min_phase, min_fine);
  int max_gate_time = gate_time(max_phase, max_fine);
  int target_gate_time = (min_gate_time + max_gate_time) / 2;
  phase = min_phase;
  fine = min_fine;
  while (1)
  {
    int cur_gate_time = gate_time(phase, fine);

    int next_fine = (fine + 1) % 8;
    int next_phase = (next_fine == 0) ? phase + 1 : phase;
    int next_gate_time = gate_time(next_phase, next_fine);

    if (next_gate_time < target_gate_time)
    {
        phase = next_phase;
        fine = next_fine;
    }
    else
    {
      int current_diff = target_gate_time - cur_gate_time;
      int next_diff = next_gate_time - target_gate_time;
      if (next_diff < current_diff)
      {
        phase = next_phase;
        fine = next_fine;
      }
      break;
    }
  }

  DBG("    Lane %2d: Initial working gate delay %2d.0, min %2d.%d, "
      "max %2d.%d, chose %2d.%d\n", lane, first_phase,
      min_phase, min_fine, max_phase, max_fine, phase, fine);

  write_rx_control_gate(shimaddr, lane, phase, fine);
}


/** Shmoo the ECC lane gate delays a bit to get a better value.
 * @param shimaddr Tile coordinates of the memory shim.
 */
static void
tune_ecc_gate_delays(pos_t shimaddr)
{
  tune_gate_delay(shimaddr, low_ecc_lane());
  if (high_ecc_lane() >= 0)
    tune_gate_delay(shimaddr, high_ecc_lane());
}


/** Search for minimally working configuration values for all lanes.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param nlanes Number of lanes to search.
 * @param tx_delays Candidate transmit delays.
 * @param tx_ndelays Number of candidate transmit delays.
 * @param gate_results Bitmap of phase/fine candidates for each lane.
 * @return Nonzero if we found a working configuration.
 */
static int
search_data_tx_and_gate_delays(pos_t shimaddr,
                               int nlanes,
                               int tx_delays[MAX_DELAYS],
                               int tx_ndelays,
                               uint64_t gate_results[NLANE])
{ 
  reorder_delays(good_tx_dqs_delay, tx_delays, tx_ndelays);

  //
  // Convert the gate delay bitmaps into arrays; we'll be sorting these
  // later.
  //
  int gate_delays[NLANE][64];
  int gate_ndelays[NLANE] = { 0 };
  int max_gate_ndelays = 0;

  for (int lane = 0; lane < NLANE; lane++)
  {
    for (uint64_t results = gate_results[lane]; results; results &= results - 1)
      gate_delays[lane][gate_ndelays[lane]++] = __builtin_ctzll(results);

    max_gate_ndelays = max(max_gate_ndelays, gate_ndelays[lane]);
  }

  //
  // Display the candidates being evaluated
  //
  DBG("Testing candidate settings for all lanes\n");

  DBG("Candidate TX DQS delays are:");
  for (int i = 0; i < tx_ndelays; i++)
    DBG(" %d", tx_delays[i]);
  DBG("\n");

  DBG("Candidate RX gate delays are:\n");
  for (int lane = 0; lane < nlanes; lane++)
  {
    DBG("  lane %2d: ", lane);
    for (int i = 0; i < gate_ndelays[lane]; i++)
      DBG(" %2d", gate_delays[lane][i]);
    DBG("\n");
  }

  //
  // Search for a working Tx DQS delay and Rx gate delay combination.
  // 
  // We are searching for a Tx Delay and Rx gate delay using writes and
  // reads so we must also have a good Rx DQS delay and Rx DQ bit delay for
  // the search to work.  We fix the Rx DQ bit delays (at 0) and allow the
  // Rx DQS delay to vary assuming that there will be some Rx DQS delay for
  // which that fixed DQ delay will be good.
  // 
  int tx_delays_found = 0;
  int num_found = 0;

  //
  // If we're using the Rx DLL, we'll vary the receive delay over its full
  // range, otherwise we just keep it at zero.
  //
  int max_rx_dqs_delay = rx_dll_enabled ? 63 : 0;
  if (rx_dll_enabled)
    reset_rx_dll_slaves(shimaddr);

  //
  // Try each candidate receive delay.
  //
  for (int rx_dqs_delay = 0;
       rx_dqs_delay <= max_rx_dqs_delay && num_found < nlanes; rx_dqs_delay++)
  {
    DBG("Testing Rx DQS delay %d\n", rx_dqs_delay);

    //
    // Set the RX delay to this trial value.
    //
    for (int lane = 0; lane < nlanes; lane++)
      write_rx_delay(shimaddr, lane, rx_dqs_delay);

    //
    // Try each candidate transmit delay.
    //
    for (int tx_idx = 0; tx_idx < tx_ndelays && num_found < nlanes;
         tx_idx++)
    {
      int tx_dqs_delay = tx_delays[tx_idx];

      DBG("  Testing Tx DQS delay %d\n", tx_dqs_delay);

      for (int lane = 0; lane < nlanes; lane++)
      {
        set_up_tx_delays(lane, tx_dqs_delay, 0);
        reorder_delays(good_phase_delay, gate_delays[lane], gate_ndelays[lane]);
      }

      config_tx_delays(shimaddr, 0, nlanes);

      int gate_index = 0;

      while (num_found < nlanes)
      {
        DBG("    Testing Lane/gate:");
        for (int lane = 0; lane < nlanes; lane++)
        {
          if (!(tx_delays_found & (1 << lane)) &&
              gate_index < gate_ndelays[lane])
          {
            int gate_delay = gate_delays[lane][gate_index];
            int fine = gate_delay % (FINE_RANGE / FINE_STEP);
            int phase = gate_delay / (FINE_RANGE / FINE_STEP);

            write_rx_control_gate(shimaddr, lane, phase, fine);

            DBG(" %2d/%2d", lane, gate_delay);

            if (lane == 7 && nlanes > 7)
              DBG("\n                      ");
          }
          else
          {
            DBG(" %2d/--", lane);
            if (lane == 7 && nlanes > 7)
              DBG("\n                      ");
          }
        }
        DBG("\n");

        uint64_t diff = check_config_quick_diff(shimaddr, -1);

        for (int lane = 0; lane < nlanes; lane++)
        {
          if (!(tx_delays_found & (1 << lane)) &&
              gate_index < gate_ndelays[lane] &&
              (diff & test_lane_mask(lane)) == 0)
          {
            if (check_config_med(shimaddr, lane))
            {
              tune_gate_delay(shimaddr, lane);
              tx_delays_found |= 1 << lane;
              num_found++;
              good_phase_delay = gate_delays[lane][gate_index];
            }
          }
        }

        gate_index++;

        if (gate_index >= max_gate_ndelays)
          break;
      }
    }
  }

  return RMASK(nlanes) & ~tx_delays_found;
}


/** Compute the minimum or maximum set of Tx delays for a set of lanes.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param upper If nonzero, compute max delay, else min delay.
 * @param delays Array to be filled in with min or max delay values.
 * @param nlanes Number of lanes to check.
 * @return Bitmap of failing lanes.
 */
static int
find_data_tx_delay_limit(pos_t shimaddr, int upper, int* delays, int nlanes)
{
  int delays_found_mask = 0;
  char* name;
  int delay_base;
  int delay_mult;

  if (upper)
  {
    name = "max";
    delay_base = MAX_TX_DQS_DELAY + DLL_STEPS_PER_CYCLE;
    delay_mult = -1;
  }
  else
  {
    name = "min";
    delay_base = TX_RANGE_START;
    delay_mult = 1;
  }

  for (int delay_offset = MIN_TX_DQS_DELAY;
       delay_offset <= MAX_TX_DQS_DELAY + 1 - TX_RANGE_STEP;
       delay_offset += TX_RANGE_STEP)
  {
    int tx_delay = delay_base + delay_offset * delay_mult;

    for (int lane = 0; lane < nlanes; lane++)
      set_up_tx_delays(lane, tx_delay, 0);

    DBG("  Testing Tx DQS delay %d\n", tx_dqs_delays[0]);
    if (!config_tx_delays(shimaddr, 0, nlanes))
      continue;

    uint64_t diff = check_config_quick_diff(shimaddr, -1);
    for (int lane = 0; lane < nlanes; lane++)
    {
      if (!(delays_found_mask & (1 << lane)))
      {
        if ((diff & test_lane_mask(lane)) == 0)
        {
          //
          // Capture the actual delay that was tested, after avoid processing.
          //
          delays[lane] = tx_dqs_delays[lane];

          DBG("    Lane %2d: %s delay is %3d\n", lane, name, delays[lane]);

          delays_found_mask |= 1 << lane;
        }
      }

      if (delays_found_mask == RMASK(nlanes))
        break;
    }

    if (delays_found_mask == RMASK(nlanes))
      break;
  }

  if (delays_found_mask != RMASK(nlanes))
    for (int lane = 0; lane < nlanes; lane++)
      if (!(delays_found_mask & (1 << lane)))
        boot_printf("msh%d: failed to find %s Tx delay for data lane %d\n",
                    msh_port(shimaddr), name, lane);

  return RMASK(nlanes) & ~delays_found_mask;
}


/** Compute minimum or maximum Tx delays for the ECC lanes.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param upper If nonzero, compute max delay, else min delay.
 * @return Delay, or -1 if one could not be found.
 */
static int
find_ecc_tx_delay_limit(pos_t shimaddr, int upper)
{
  int delay = -1;
  char* name;
  int delay_base;
  int delay_mult;

  if (upper)
  {
    name = "max";
    delay_base = MAX_TX_DQS_DELAY + DLL_STEPS_PER_CYCLE;
    delay_mult = -1;
  }
  else
  {
    name = "min";
    delay_base = TX_RANGE_START;
    delay_mult = 1;
  }

  for (int delay_offset = MIN_TX_DQS_DELAY;
       delay_offset <= MAX_TX_DQS_DELAY + 1 - TX_RANGE_STEP;
       delay_offset += TX_RANGE_STEP)
  {
    int tx_delay = delay_base + delay_offset * delay_mult;

    DBG("  Testing Tx DQS delay %d\n", tx_delay);
    if (!config_ecc_tx_delays(shimaddr, tx_delay))
      continue;

    if (check_config_quick(shimaddr, -1))
    {
      delay = tx_dqs_delays[low_ecc_lane()];
      DBG("    Lane %2d: %s delay is %3d\n", low_ecc_lane(), name, delay);
    }

    if (delay >= 0)
      break;
  }

  if (delay < 0)
    boot_printf("msh%d: failed to find %s Tx delay for ECC lanes\n",
                msh_port(shimaddr), name);

  return delay;
}


/** Width of a bit delay in ps. */
const int bit_delay_size = 40;
/** Goal for the fraction of margin to have on the back side, in percent. */
const int backside_margin_fraction_pct = 10;

/** Return the step size in ps for a receive delay.
 * @param rx_delay Receive delay.
 * @return Step size.
 */
static int
step_size(int rx_delay)
{
  const int step_divisors [4] = {8, 8, 4, 2};
  int step = rx_delay % 4;
  int divisor = step_divisors[step];

  return tCK_fs / (16 * 1000 * divisor);
}


/** Find the trailing edge.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane number to check.
 * @param working_width Working width in ps; adjusted based on the bit
 *  delay returned, or set to -1 if this isn't the trailing edge.
 * @return Bit delay corresponding to the trailing edge, and new
 *  *working_width value.
 */
static int
find_trailing_edge(pos_t shimaddr, int lane, int* working_width)
{
  //
  // We search for the trailing edge from bit delay 1 up.  We continue
  // searching for the first passing bit delay to identify exactly where it
  // is.  We decrement the margin from the front edge for each bit delay
  // that fails.
  //
  DBG("    Lane %d: Looking for trailing edge\n", lane);
  int bit_delay = 0;

  for (bit_delay = 1; bit_delay < 8; bit_delay++)
  {
    //
    // Configure this bit delay.
    //
    write_rx_bit_delay_lane(shimaddr, lane, bit_delay);
    if (is_ecc_lane(lane) && lane_width() == 4)
      write_rx_bit_delay_lane(shimaddr, lane + 1, bit_delay);

    //
    // If this bit delay passes, we are done and should break out to
    // capture the bit delay.
    //
    if (check_config_long(shimaddr, lane, 1))
    {
      DBG("      Bit delay %d passed\n", bit_delay);
      break;
    }

    //
    // This bit delay failed so we decrement our working width.
    //
    *working_width -= bit_delay_size;
    DBG("      Bit delay %d failed; decremented working width "
        "for trailing edge, now %d\n", bit_delay, *working_width);
  }

  //
  // We should never see the trailing edge at a large bit delay.  Typically
  // it will be at bit delay 1 or 2.
  //
  if (bit_delay > 4)
    return 0;

  return bit_delay;
}


/** Search for the trailing edge of the working region.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane number to check.
 * @param trailing_edge_rx_delay Rx delay of trailing edge.
 * @param trailing_edge_bit_delay Bit delay of trailing edge.
 * @param working_width Working width in ps; updated based on the search.
 * @param target_width Target width in ps.
 * @param bit_delay Current bit delay.
 * @param leading_edge_rx_delay Rx delay of leading edge.
 * @param leading_edge_bit_delay Bit delay of leading edge.
 * @return Nonzero if we find the trailing edge.
 */
static int
check_rx_search_done(pos_t shimaddr, int lane,
                     int trailing_edge_rx_delay,
                     unsigned int trailing_edge_bit_delay,
                     int working_width,
                     int target_width,
                     int bit_delay,
                     int leading_edge_rx_delay,
                     unsigned int leading_edge_bit_delay)
{
  int foundit = 0;

  //
  // If we hit the trailing edge or we have seen enough working width,
  // we'll stop.
  //
  if (trailing_edge_rx_delay >= 0 || working_width >= target_width)
  {
    // Set the flag indicating that we're ready to stop
    foundit = 1;

    //
    // Calculate the amount of back margin we'd like.
    //
    int fraction_pct, width;

    if (trailing_edge_rx_delay >= 0)
    {
        DBG("  Lane %2d: Stopping because we hit the trailing edge\n", lane);
        width = working_width;
        fraction_pct = max(50, backside_margin_fraction_pct);
    }
    else
    {
        DBG("  Lane %2d: Stopping because we have enough working width\n",
            lane);

        // ENHANCEME - could just be working width
        width = target_width;

        fraction_pct = backside_margin_fraction_pct;
    }
    int target_back_margin = (width * fraction_pct) / 100;

    DBG("    Working width %d target width %d back_margin %d%% "
        "target_back_margin %d\n",
        working_width, target_width, fraction_pct, target_back_margin);

    if (rx_slave_reset_supported)
    {
      //
      // We will be moving the Rx DQS delay back to a midpoint of the
      // tested working range.  We round down the midpoint if it is not an
      // integer.  A slightly smaller setting on Rx DQS setting is chosen.
      // We will be moving the Rx bit delay back to a midpoint.  To match
      // with Rx DQS setting, bit_delay = 3 is preferred.
      //
      bit_delay = 3;
    }
    else
    {
      //
      // Figure out what bit delay value we're going to use.  Coming into
      // this, the bit delay should be positioned at the trailing edge value.
      // Increment the bit delay to gain back side margin.  Stop when we've
      // achieved the back side margin that we want, or when we hit a bit
      // delay of 7, because that's the maximum value.
      //
      int back_margin = 0;
      int front_margin = working_width;
      while (back_margin < target_back_margin)
      {
          bit_delay++;
          back_margin += bit_delay_size;
          front_margin -= bit_delay_size;

          if (bit_delay >= 7)
              break;
      }

      //
      // Adjust the bit_delay if a single rank of soldered-down memory, or
      // 1 DIMM, is populated.
      //
      const int bit_delay_offset = 2;
      if (minfo->onboard ? minfo->ctl_ranks == 1 : minfo->numdimms == 1)
        bit_delay = min(7, bit_delay + bit_delay_offset);
    }

    //
    // If we're stopping now because we've found the configuration we want,
    // save the bit delay value we calculated, and configure it.
    //
    write_rx_bit_delay_lane(shimaddr, lane, bit_delay);
    if (is_ecc_lane(lane) && lane_width() == 4)
      write_rx_bit_delay_lane(shimaddr, lane + 1, bit_delay);

    //
    // If separate DLL slave reset is supported, set the Rx delay to the
    // midpoint of the tested working range.
    //
    if (rx_slave_reset_supported)
    {
      int rx_dqs_delay = (leading_edge_rx_delay + trailing_edge_rx_delay) / 2;
      DBG("    Leading edge %d, trailing edge %d, selecting %d\n",
          leading_edge_rx_delay, trailing_edge_rx_delay, rx_dqs_delay);
      //
      // Reset the slave and configure the delay.
      //
      toggle_rx_dll_slave_reset_lane(shimaddr, lane);
      write_rx_delay(shimaddr, lane, rx_dqs_delay);
      if (is_ecc_lane(lane) && lane_width() == 4)
      {
        toggle_rx_dll_slave_reset_lane(shimaddr, lane + 1);
        write_rx_delay(shimaddr, lane + 1, rx_dqs_delay);
      }
    }
  }

  return foundit;
}


/** Search for the trailing edge of the working region.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane number to check.
 * @param target_width Target width in ps.
 * @param working_width Working width in ps; updated based on the search.
 * @param trailing_edge_rx_delay Returned Rx delay of trailing edge.
 * @param trailing_edge_bit_delay Returned bit delay of trailing edge.
 * @param rx_delay Current receive delay.
 * @return Nonzero if we find the trailing edge.
 */
static int
check_config_trailing_edge(pos_t shimaddr, int lane, int target_width,
                           int* working_width,
                           int* trailing_edge_rx_delay,
                           unsigned int* trailing_edge_bit_delay,
                           int rx_delay)
{
  //
  // Test bit delay 0.
  //
  int bit_delay = 0;
  write_rx_bit_delay_lane(shimaddr, lane, bit_delay);
  if (is_ecc_lane(lane) && lane_width() == 4)
    write_rx_bit_delay_lane(shimaddr, lane + 1, bit_delay);

  if (check_config_long(shimaddr, lane, 0))
  {
    *trailing_edge_rx_delay = -1;

    return 0;
  }
  else
  {
    //
    // Decrement working width because bit delay 0 failed.
    //
    *working_width -= bit_delay_size;

    //
    // Look for the trailing edge (starting with bit delay 1).
    //
    bit_delay = find_trailing_edge(shimaddr, lane, working_width);

    //
    // Capture the trailing edge.
    //
    *trailing_edge_rx_delay = rx_delay;
    *trailing_edge_bit_delay = bit_delay;
    DBG("Lane %d: Trailing edge (%d,%d)\n", lane, rx_delay, bit_delay);

    return 1;
  }
}


/** Search for the trailing edge of the working region; version used when
 *  Rx slave reset is supported.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param lane Lane number to check.
 * @param trailing_edge_rx_delay Returned Rx delay of trailing edge.
 * @param trailing_edge_bit_delay Returned bit delay of trailing edge.
 * @param rx_delay Current receive delay.
 * @param already_passed If nonzero, don't do the check_config step, just
 *  assume it passed.
 * @return Nonzero if we find the trailing edge.
 */
static int
check_config_trailing_edge_rsrs(pos_t shimaddr, int lane,
                                int* trailing_edge_rx_delay,
                                unsigned int* trailing_edge_bit_delay,
                                int rx_delay, int already_passed)
{
  if (already_passed || check_config_long(shimaddr, lane, 1))
  {
    //
    // Bump up rx delay, since the failing delay is what we call the
    // trailing edge.
    //
    rx_delay++;

    //
    // Note that, unlike check_config_trailing_edge(), we don't bother to
    // change the bit delay and find the actual trailing edge.  This is
    // because we know that check_rx_search_done() is just going to use a
    // bit delay of 3, and will pick the Rx delay as the mean of the
    // leading and trailing edges' Rx delays, ignoring their bit delays.
    //

    //
    // Capture the trailing edge.
    //
    *trailing_edge_rx_delay = rx_delay;
    *trailing_edge_bit_delay = 3;
    DBG("      Lane %d: Trailing edge (%d,%d)\n", lane,
        *trailing_edge_rx_delay, *trailing_edge_bit_delay);

    return 1;
  }
  else
    return 0;
}


/** Search for working Rx configuration values for the data lanes.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param target_width Target width for Rx search.
 * @return Bitmap of failed lanes.
 */
static int
search_rx_delays_all_data_lanes(pos_t shimaddr, int target_width)
{
  int num_d_lanes = num_data_lanes();

  int leading_edge_rx_delays[NLANE] = { [0 ... NLANE - 1] = -1 };
  unsigned int leading_edge_bit_delays[NLANE] = { 0 };
  int leading_edge_count = 0;

  int working_widths[NLANE] = { 0 };

  int search_done = 0;

  int trailing_edge_rx_delays[NLANE] = { [0 ... NLANE - 1] = -1 };
  unsigned int trailing_edge_bit_delays[NLANE] = { 0 };

  int bit_delays[NLANE] = { 0 };

  //
  // Reset/relock the Rx DLL again so we can search up through values
  // again.
  //
  reset_rx_dll_slaves(shimaddr);

  for (int rx_delay = 0; rx_delay < 64; rx_delay++)
  {
    DBG("  Testing Rx DQS delay %d\n", rx_delay);

    //
    // Store these Rx DQS delay values and configure them.  We only change
    // the Rx DQS delay for lanes that aren't done yet.
    //
    for (int lane = 0; lane < num_d_lanes; lane++)
      if (!(search_done & (1 << lane)))
        write_rx_delay(shimaddr, lane, rx_delay);

    //
    // If we have not yet found the leading edge for all lanes, see if we
    // can find it for those that we haven't found it for yet at this Rx
    // DQS delay.
    //
    if (leading_edge_count < num_d_lanes)
    {
      // Search for leading edges from max bit delay to min
      for (int bit_delay = 7; bit_delay >= 0; bit_delay--)
      {
        //
        // If we haven't found any leading edges at all yet, try to test
        // and find all of them with one BIST test. This speeds things up
        // for the most common case: leading edge for all lanes at Rx DQS
        // delay 0, bit delay 7.
        //
        if (leading_edge_count == 0)
        {
          //
          // Configure this bit delay for the lanes of interest.
          //
          for (int lane = 0; lane < num_d_lanes; lane++)
            write_rx_bit_delay_lane(shimaddr, lane, bit_delay);

          //
          // Check the configuration for all data lanes.
          //
          if (check_config_long(shimaddr, -1, 0))
          {
            // Capture information about the leading
            // edge for all lanes
            for (int lane = 0; lane < num_d_lanes; lane++)
            {
              working_widths[lane] = bit_delay * bit_delay_size;
              leading_edge_rx_delays[lane] = rx_delay;
              leading_edge_bit_delays[lane] = bit_delay;
            }
            leading_edge_count = num_d_lanes;

            DBG("    Found leading edge for all lanes at (%d,%d), working "
                "width is %d\n", rx_delay, bit_delay, working_widths[0]);
          }
        }

        //
        // If we haven't found the leading edge for all of the lanes yet,
        // try this bit delay for each lane individually at this Rx DQS
        // delay.
        //
        if (leading_edge_count < num_d_lanes)
        {
          // Consider each lane
          for (int lane = 0; lane < num_d_lanes; lane++)
          {
            //
            // If we haven't found the leading edge for this lane, try it.
            //
            if (leading_edge_rx_delays[lane] < 0)
            {
              //
              // Configure this bit delay for the lanes of interest.
              //
              write_rx_bit_delay_lane(shimaddr, lane, bit_delay);

              // Check the configuration for this data lanes
              if (check_config_long(shimaddr, lane, 0))
              {
                // Capture information about the
                // leading edge for this lane
                working_widths[lane] = bit_delay * bit_delay_size;
                leading_edge_rx_delays[lane] = rx_delay;
                leading_edge_bit_delays[lane] = bit_delay;
                leading_edge_count++;

                DBG("    Lane %d: Found leading edge at (%d,%d), "
                    "working width is %d\n", lane, rx_delay, bit_delay,
                    working_widths[lane]);
              }
            }
          }
        }
      }
    }

    //
    // For any lane that has just took a step away from the Rx DQS delay
    // where its leading edge was found, increase the measured working
    // width by the DLL step size.  If we find that some Rx DQ bit delays
    // don't pass for this Rx DQS delay, we will take away from that
    // working width.
    //
    for (int lane = 0; lane < num_d_lanes; lane++)
    {
      //
      // Only update if this isn't the Rx DQS delay where we found the
      // leading edge.  We don't do it in that case because the working
      // width was initialized when the leading edge was found.
      //
      if (leading_edge_rx_delays[lane] >= 0 &&
          rx_delay != leading_edge_rx_delays[lane])
        working_widths[lane] += step_size(rx_delay);
    }

    //
    // If we have found the leading edge for all data lanes and we haven't
    // found the done condition for any data lanes, we check for the
    // trailing edge for all lanes together at bit delay 0.  This is the
    // common case for the first few Rx DQS delays and so speeds up the
    // process.
    //
    int all_passed = 0;
    if (leading_edge_count == num_d_lanes && search_done == 0)
    {
      //
      // Configure a bit delay of 0 for all of the data lanes.
      //
      for (int lane = 0; lane < num_d_lanes; lane++)
      {
        write_rx_bit_delay_lane(shimaddr, lane, 0);
        bit_delays[lane] = 0;
      }

      //
      // Check the configuration for all data lanes.
      //
      if (check_config_long(shimaddr, -1, 0))
      {
        //
        // Set the flag that will indicate that they all passed.
        //
        all_passed = 1;
        DBG("    All lanes passed, working width is now %d\n",
            working_widths[0]);
      }
    }

    //
    // If some lanes did not pass at bit delay 0 for this Rx DQS delay,
    // figure out if any lanes pass.
    //
    if (!all_passed)
    {
      //
      // Consider each lane; only check lanes for which we have found the
      // leading edge but not the done condition.
      //
      for (int lane = 0; lane < num_d_lanes; lane++)
      {
        if (leading_edge_rx_delays[lane] >= 0 && !(search_done & (1 << lane)))
        {
          check_config_trailing_edge(shimaddr, lane, target_width,
                                     &working_widths[lane],
                                     &trailing_edge_rx_delays[lane],
                                     &trailing_edge_bit_delays[lane],
                                     rx_delay);
          bit_delays[lane] = trailing_edge_bit_delays[lane];
        }
      }
    }

    //
    // Check to see if we can stop searching for any lanes that have
    // measured sufficient working width or that hit the trailing edge.
    //
    for (int lane = 0; lane < num_d_lanes; lane++)
    {
      if (!(search_done & (1 << lane)))
      {
        if (check_rx_search_done(shimaddr, lane,
                                 trailing_edge_rx_delays[lane],
                                 trailing_edge_bit_delays[lane],
                                 working_widths[lane], target_width,
                                 bit_delays[lane],
                                 leading_edge_rx_delays[lane],
                                 leading_edge_bit_delays[lane]))
          search_done |= 1 << lane;
      }
    }

    //
    // Break out if we've reached the done condition for all of the lanes.
    //
    if (__builtin_popcount(search_done) == num_d_lanes)
      break;
  }

  //
  // Return a bitmap indicating which lanes failed.
  //
  return RMASK(num_d_lanes) & ~search_done;
}


/** Search for working Rx configuration values for the data lanes; version
 *  used when Rx slave reset is supported.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param target_width Target width for Rx search.
 * @return Bitmap of failed lanes.
 */
static int
search_rx_delays_all_data_lanes_rsrs(pos_t shimaddr, int target_width)
{
#ifdef MSH_OLD_RX_DELAY_SEARCH
  return search_rx_delays_all_data_lanes(shimaddr, target_width);
#endif

  int num_d_lanes = num_data_lanes();

  int leading_edge_rx_delays[NLANE] = { [0 ... NLANE - 1] = -1 };
  unsigned int leading_edge_bit_delays[NLANE] = { 0 };

  int working_widths[NLANE] = { 0 };

  int trailing_edge_rx_delays[NLANE] = { [0 ... NLANE - 1] = -1 };
  unsigned int trailing_edge_bit_delays[NLANE] = { 0 };

  int bit_delays[NLANE] = { 0 };

  //
  // Reset/relock the Rx DLL again so we can search up through values.
  //
  reset_rx_dll_slaves(shimaddr);

  int remaining_lanes = RMASK(num_d_lanes);

  for (int rx_delay = 0; rx_delay < 32 && remaining_lanes; rx_delay++)
  {
    DBG("  Testing Rx DQS delay %d\n", rx_delay);

    //
    // Store these Rx DQS delay values and configure them.  We only change
    // the Rx DQS delay for lanes that aren't done yet.
    //
    for (int lane = 0; lane < num_d_lanes; lane++)
      if (remaining_lanes & (1 << lane))
        write_rx_delay(shimaddr, lane, rx_delay);

    // Search for leading edges from max bit delay to min
    for (int bit_delay = 7; bit_delay >= 0; bit_delay--)
    {
      //
      // If we haven't found any leading edges at all yet, try to test
      // and find all of them with one BIST test. This speeds things up
      // for the most common case: leading edge for all lanes at Rx DQS
      // delay 0, bit delay 7.
      //
      if (remaining_lanes == RMASK(num_d_lanes))
      {
        //
        // Configure this bit delay for the lanes of interest.
        //
        for (int lane = 0; lane < num_d_lanes; lane++)
          write_rx_bit_delay_lane(shimaddr, lane, bit_delay);

        //
        // Check the configuration for all data lanes.
        //
        if (check_config_long(shimaddr, -1, 1))
        {
          // Capture information about the leading
          // edge for all lanes
          for (int lane = 0; lane < num_d_lanes; lane++)
          {
            working_widths[lane] = bit_delay * bit_delay_size;
            leading_edge_rx_delays[lane] = rx_delay;
            leading_edge_bit_delays[lane] = bit_delay;
          }
          remaining_lanes = 0;

          DBG("    Found leading edge for all lanes at (%d,%d), working "
              "width is %d\n", rx_delay, bit_delay, working_widths[0]);
        }
      }

      //
      // If we've found all the leading edges, we're done.
      //
      if (!remaining_lanes)
        break;

      //
      // Otherwise, try this bit delay for each lane individually at this
      // Rx DQS delay.
      //
      for (int lane_mask = remaining_lanes; lane_mask;
           lane_mask &= lane_mask - 1)
      {
        int lane = __builtin_ctz(lane_mask);

        //
        // Configure this bit delay for the lanes of interest.
        //
        write_rx_bit_delay_lane(shimaddr, lane, bit_delay);

        // Check the configuration for this data lane
        if (check_config_long(shimaddr, lane, 1))
        {
          // Capture information about the
          // leading edge for this lane
          working_widths[lane] = bit_delay * bit_delay_size;
          leading_edge_rx_delays[lane] = rx_delay;
          leading_edge_bit_delays[lane] = bit_delay;
          remaining_lanes &= ~(1 << lane);

          DBG("    Lane %d: Found leading edge at (%d,%d), "
              "working width is %d\n", lane, rx_delay, bit_delay,
              working_widths[lane]);
        }
      }
    }
  }

  //
  // If we didn't find all of the leading edges, just return the failing
  // lanes right now.
  //
  if (remaining_lanes)
    return remaining_lanes;

  //
  // Now we have all of the leading edges; let's find the trailing edges.
  //
  remaining_lanes = RMASK(num_d_lanes);

  //
  // Configure a bit delay of 0 for all of the data lanes.
  //
  for (int lane = 0; lane < num_d_lanes; lane++)
  {
    write_rx_bit_delay_lane(shimaddr, lane, 0);
    bit_delays[lane] = 0;
  }

  //
  // The standard algorithm works forward from the leading edge, looking
  // for lanes that fail.  When we find one, we know the trailing edge is
  // somewhwere between that lane and the previous one, so we increase the
  // bit delay until we pass again.  This turns out to be expensive, since
  // all of those successful tests we do before we hit the first failure
  // take a long time to run.
  //
  // This algorithm works backwards from the end of the delay space toward
  // the leading edge, looking for lanes that succeed.  When we find one,
  // we again know that the trailing edge is somewhere between that lane
  // and the previous one, so we increase the delay by 1 to get back to the
  // spot that fails, then increase the bit delay until we pass again.
  // With this scheme, we're normally testing each individual lane, since
  // the all-lanes test will fail until we hit a delay where all lanes
  // work.  However, a failing test is so much cheaper that this is still
  // a big win.
  //

  //
  // Start at 30 since if 31 passes there is no trailing edge...
  //
  for (int rx_delay = 30; rx_delay >= 0 && remaining_lanes; rx_delay--)
  {
    DBG("  Looking for trailing edges at Rx DQS delay %d\n", rx_delay);

    for (int lane_mask = remaining_lanes; lane_mask; lane_mask &= lane_mask - 1)
    {
      int lane = __builtin_ctz(lane_mask);

      toggle_rx_dll_slave_reset_lane(shimaddr, lane);
      write_rx_delay(shimaddr, lane, rx_delay);
    }

    int found_this_pass = 0;
    int all_passed = 0;

    //
    // First see if all the lanes pass.  It's a waste to check all lanes if
    // only one is left.
    //
    if (__builtin_popcount(remaining_lanes) > 1 &&
        check_config_long(shimaddr, -1, 1))
    {
      DBG("    All remaining lanes passed, mask 0x%x\n", remaining_lanes);
      //
      // We don't take all lanes out of remaining_lanes, since we still
      // need to call check_config_trailing_edge_rsrs to pick the bit delay
      // for the trailing edge.  However, this flag, which we pass to that
      // routine, will cause it not call check_config_long() again for each
      // lane.
      //
      all_passed = 1;
    }

    //
    // Now see if any individual lanes pass.
    //
    for (int lane_mask = remaining_lanes; lane_mask; lane_mask &= lane_mask - 1)
    {
      int lane = __builtin_ctz(lane_mask);

      if (check_config_trailing_edge_rsrs(shimaddr, lane,
                                          &trailing_edge_rx_delays[lane],
                                          &trailing_edge_bit_delays[lane],
                                          rx_delay, all_passed))
      {
        bit_delays[lane] = trailing_edge_bit_delays[lane];
        found_this_pass |= 1 << lane;
        remaining_lanes &= ~(1 << lane);
      }
    }

    //
    // For any lanes that just passed, compute the final delay.
    //
    for (int lane_mask = found_this_pass; lane_mask; lane_mask &= lane_mask - 1)
    {
      int lane = __builtin_ctz(lane_mask);

      //
      // Note we know we've hit the trailing edge, so we know this
      // succeeds, and thus we don't need to check the return value.
      //
      check_rx_search_done(shimaddr, lane, trailing_edge_rx_delays[lane],
                           trailing_edge_bit_delays[lane],
                           working_widths[lane], target_width,
                           bit_delays[lane], leading_edge_rx_delays[lane],
                           leading_edge_bit_delays[lane]);
    }
  }

  //
  // Return a bitmap indicating which lanes failed.
  //
  return remaining_lanes;
}


/** Search for working configuration values for ECC lanes.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param tx_delays Candidate transmit delays.
 * @param tx_ndelays Number of candidate transmit delays.
 * @param gate_results Bitmap of phase/fine candidates for each lane.
 * @param target_width Target width for Rx search in ps.
 * @return Nonzero if we found a working configuration.
 */
static int
search_delays_ecc_lane(pos_t shimaddr,
                       int tx_delays[MAX_DELAYS],
                       int tx_ndelays,
                       uint64_t gate_results[NLANE],
                       int target_width)
{
  int low_lane = low_ecc_lane();
  int high_lane = high_ecc_lane();

  // When we have found the leading edge, we will store it here
  // This will also serve as a flag to indicate whether we
  // have found it or not.
  int leading_edge_rx_delay = -1;
  unsigned int leading_edge_bit_delay = 0;

  int working_width;

  // This flag will indicate that we have found a working
  // configuration and can stop.
  // This flag is returned from this method.
  int search_done = 0;

  //
  // If we're using the Rx DLL, we'll vary the receive delay over its full
  // range, otherwise we just keep it at zero.
  //
  int max_rx_dqs_delay = rx_dll_enabled ? 63 : 0;

  reorder_delays(good_tx_dqs_delay, tx_delays, tx_ndelays);

  //
  // Convert the gate delay bitmap into an array, so that we can reorder
  // it.
  //
  int gate_delays_low[64];
  int gate_ndelays_low = 0;

  for (uint64_t results = gate_results[low_lane]; results;
       results &= results - 1)
    gate_delays_low[gate_ndelays_low++] = __builtin_ctzll(results);

  reorder_delays(good_phase_delay, gate_delays_low, gate_ndelays_low);

  // Search through Rx DQS delay values from low to high assuming we
  // can't decrease them later
  for (int rx_delay = 0; rx_delay <= max_rx_dqs_delay; rx_delay++)
  {
    DBG("  Testing Rx DQS delay %d\n", rx_delay);

    //
    // Store these Rx DQS delay values and configure them.
    //
    write_rx_delay(shimaddr, low_lane, rx_delay);
    if (high_lane >= 0)
      write_rx_delay(shimaddr, high_lane, rx_delay);

    //
    // If we haven't found the leading edge yet (or haven't found working
    // Tx and gate delays) try to do that.
    //
    if (leading_edge_rx_delay < 0)
    {
      for (int tx_idx = 0; tx_idx < tx_ndelays; tx_idx++)
      {
        int tx_delay = tx_delays[tx_idx];

        //
        // If we are successful in configuring this, keep going.
        //
        if (config_ecc_tx_delays(shimaddr, tx_delay))
        {
          // Consider each candidate Rx gate delay
          for (int gate_idx = 0; gate_idx < gate_ndelays_low; gate_idx++)
          {
            int gate_delay = gate_delays_low[gate_idx];
            //
            // Note: the BTK uses write_gate_delay() here, and inlines the
            // handling of the other ECC lane.  We may get around to doing
            // that eventually, but haven't; the code is the same.
            //
            set_gate_delay(shimaddr, low_lane, gate_delay, gate_results);
            DBG("    Testing Tx DQS delay %d, Rx gate delay %d\n",
                tx_dqs_delays[low_lane], gate_delay);

            if (rx_dll_enabled)
            {
              //
              // Try to find the leading edge at this Rx DQS delay.
              // Start by assuming we won't find the leading edge.
              //
              working_width = 0;

              //
              // Test bit delay 0 first.  If it fails, there's no point in
              // testing anything else at this test point.  This weeds out
              // bad gate delays more quickly.
              //
              write_rx_bit_delay_lane(shimaddr, low_lane, 0);
              if (high_lane >= 0)
                write_rx_bit_delay_lane(shimaddr, high_lane, 0);

              if (check_config_long(shimaddr, -1, 0))
              {
                //
                // Before changing Rx delays in any way, tune up the gate
                // delays with the initial Rx DQS and DQ delay values.
                // This mimics what is done for the data lane flow.
                //
                tune_ecc_gate_delays(shimaddr);

                //
                // Look for the leading edge by looking for the largest bit
                // delay that passes for this Rx DQS delay.  Note that if
                // this is the maximum bit delay, it actually might NOT be
                // the leading edge, it's just the first value we can see.
                // This can cause us to underestimate the amount of
                // working width we have.
                //
                for (int bit_delay = 7; bit_delay >= 0; bit_delay--)
                {
                  //
                  // Configure this bit delay.
                  //
                  write_rx_bit_delay_lane(shimaddr, low_lane, bit_delay);
                  if (high_lane >= 0)
                    write_rx_bit_delay_lane(shimaddr, high_lane, bit_delay);

                  //
                  // Test this bit delay.  If it passes, we've found what
                  // we're looking for, so we capture this as the leading
                  // edge and break out of this loop.
                  //
                  if (check_config_long(shimaddr, -1, 0))
                  {
                    leading_edge_rx_delay = rx_delay;
                    leading_edge_bit_delay = bit_delay;
                    DBG("    Leading edge is (%d,%d)\n", leading_edge_rx_delay,
                        leading_edge_bit_delay);

                    //
                    // For now, for purposes of our working width
                    // calculation, we assume that all smaller bit delay
                    // values will also pass.  We will adjust this later
                    // when/if we find the trailing edge.
                    //
                    working_width = bit_delay * bit_delay_size;
                    DBG("    Initial working width is %d\n", working_width);

                    break;
                  }
                }
              }
            }
            else
            {
              //
              // If the Rx DLL is not enabled, we just need to check to see
              // if this configuration (Tx delay and gate delay) is good or
              // not.
              //
              if (check_config_long(shimaddr, -1, 0))
              {
                tune_ecc_gate_delays(shimaddr);

                //
                // Set the flag that will get us out of the search loops.
                //
                search_done = 1;
              }
            }

            //
            // If we've found the working Tx delay and gate delay, tune
            // them up.
            //
            if (search_done || leading_edge_rx_delay >= 0)
            {
              int max_delay = find_ecc_tx_delay_limit(shimaddr, 1);
              int min_delay = find_ecc_tx_delay_limit(shimaddr, 0);

              if (min_delay > max_delay)
              {
                boot_printf("msh%d: min delay %d is greater than max "
                            "delay %d for ECC lane(s)\n", msh_port(shimaddr),
                            min_delay, max_delay);
                return 0;
              }

              int delay_range = max_delay - min_delay;
              int mid_delay = min_delay + (delay_range / 2);

              DBG("Lane %d: min = %d, max = %d, range = %d, mid = %d\n",
                  low_lane, min_delay, max_delay, delay_range, mid_delay);

              nudge_tx_delay(shimaddr, low_lane, mid_delay);

              //
              // Don't try another gate delay if we've found the right one.
              //
              break;
            }
          }
        }
        
        //
        // Don't try another Tx DQS delay if we've found the right one.
        //
        if (search_done || leading_edge_rx_delay >= 0)
          break;
      }
    }

    //
    // If the Rx DLL is enabled and we have found the leading edge, try to
    // find the trailing edge.
    //
    if (rx_dll_enabled && leading_edge_rx_delay >= 0)
    {
#ifndef MSH_OLD_RX_DELAY_SEARCH
      //
      // If we can reset the slave, and thus can back up, we're going to
      // use a different algorithm to find the trailing edge, below, so
      // skip this part.
      //
      if (rx_slave_reset_supported)
        break;
#endif

      //
      // If we have stepped past the Rx DQS delay where we saw the leading
      // edge, increment the front side margin by the size of the step we
      // took to get to this Rx DQS delay.
      //
      if (rx_delay != leading_edge_rx_delay)
      {
        working_width += step_size(rx_delay);
        DBG("      Incremented working width by step size %d; now %d.\n",
            step_size(rx_delay), working_width);
      }

      int trailing_edge_rx_delay = 0;
      unsigned int trailing_edge_bit_delay = 0;

      check_config_trailing_edge(shimaddr, low_lane, target_width,
                                 &working_width,
                                 &trailing_edge_rx_delay,
                                 &trailing_edge_bit_delay,
                                 rx_delay);
      int bit_delay = trailing_edge_bit_delay;

      //
      // Figure out if this configuration is where we will stop searching
      // and the Rx DQS and bit delay we will use.
      //
      search_done = check_rx_search_done(shimaddr, low_lane,
                                         trailing_edge_rx_delay,
                                         trailing_edge_bit_delay,
                                         working_width, target_width,
                                         bit_delay,
                                         leading_edge_rx_delay,
                                         leading_edge_bit_delay);
    }

    //
    // Don't try any more Rx DQS delays if we've found the one we want.
    //
    if (search_done)
      break;
  }

#ifndef MSH_OLD_RX_DELAY_SEARCH
  if (rx_dll_enabled && leading_edge_rx_delay >= 0 &&
      rx_slave_reset_supported)
  {
    //
    // We can back up, so we're going to search from the back and stop
    // when we succeed, instead of searching from the front and stopping
    // when we fail.  This means we fail more and succeed less, which
    // ends up being a lot quicker.
    //

    //
    // Configure a bit delay of 0 for the ECC lanes.
    //
    write_rx_bit_delay_lane(shimaddr, low_lane, 0);
    if (high_lane >= 0)
      write_rx_bit_delay_lane(shimaddr, high_lane, 0);

    //
    // We start at 30 since if 31 passes there is no trailing edge...
    //
    for (int rx_delay = 30; rx_delay >= 0 && !search_done; rx_delay--)
    {
      DBG("  Looking for trailing edge at Rx delay %d\n", rx_delay);

      toggle_rx_dll_slave_reset_lane(shimaddr, low_lane);
      write_rx_delay(shimaddr, low_lane, rx_delay);
      if (high_lane >= 0)
      {
        toggle_rx_dll_slave_reset_lane(shimaddr, high_lane);
        write_rx_delay(shimaddr, high_lane, rx_delay);
      }

      int trailing_edge_rx_delay = 0;
      unsigned int trailing_edge_bit_delay = 0;

      if (check_config_trailing_edge_rsrs(shimaddr, low_lane,
                                          &trailing_edge_rx_delay,
                                          &trailing_edge_bit_delay,
                                          rx_delay, 0))
      {
        DBG("Lane %d passed\n", low_lane);

        int bit_delay = trailing_edge_bit_delay;

        //
        // Compute the final delay now that we've seen both edges.  Note we
        // know we've hit the trailing edge, so we know this succeeds, and
        // thus we don't need to check the return value.
        //
        check_rx_search_done(shimaddr, low_lane, trailing_edge_rx_delay,
                             trailing_edge_bit_delay, working_width,
                             target_width, bit_delay,
                             leading_edge_rx_delay,
                             leading_edge_bit_delay);
        search_done = 1;
      }
    }
  }
#endif

  return search_done;
}

/////////////////////////////////////////////////////////////////////////////
// ECC control
/////////////////////////////////////////////////////////////////////////////

// ecc_enable, ecc_disable inlined where used

/////////////////////////////////////////////////////////////////////////////
// Timing register config
/////////////////////////////////////////////////////////////////////////////

/** Configure timing registers.
 * @param shimaddr Tile coordinates of the memory shim.
 * @return Nonzero if configuration was successful, 0 otherwise.
 */
static int
config_timing_regs(pos_t shimaddr)
{
  DBG("Configuring timing registers\n");

  //
  // Set up mode register values.  We aren't actually writing the mode
  // registers themselves right now; the controller will do that
  // automatically as part of its initialization flow later, using the
  // values we set here.
  //
  cfg_wr(shimaddr.word, 0, MSH_DDR3_MR0, 0);

  MSH_DDR3_MR1_t mdm1 = { .word = 0 };
  mdm1.ds = 1;

  //
  // If we're working with x8 RAMs, enable TDQS so that input isn't used as
  // a data mask.  This is a function of the RAM width, not the lane width;
  // x4 and x16 RAMs do not support this feature and are configured similarly.
  //
  if (minfo->sdram_width_bits == 8)
    mdm1.tdqs = 1;

  cfg_wr(shimaddr.word, 0, MSH_DDR3_MR1, mdm1.word);

  cfg_wr(shimaddr.word, 0, MSH_DDR3_MR2, 0);

  cfg_wr(shimaddr.word, 0, MSH_DDR3_MR3, 0);

  //
  // Row timing.
  //
  MSH_DDR3_ROW_TIMING_t mdrt = { .word = 0 };

  mdrt.rrd = PARAM_TO_CYCLES(tRRDmin_fs, 4, 8);
  mdrt.rp = PARAM_TO_CYCLES(tRPmin_fs, 1, 15);
  if (mdrt.rp < mdrt.rrd)
    mdrt.rp = mdrt.rrd;
  mdrt.rcd = PARAM_TO_CYCLES(tRCDmin_fs, 2, 15);
  mdrt.ras = PARAM_TO_CYCLES(tRASmin_fs, 4, 38);
  mdrt.rc = PARAM_TO_CYCLES(tRCmin_fs, 5, 52);
  mdrt.faw = PARAM_TO_CYCLES(tFAWmin_fs, 7, 43);

  cfg_wr(shimaddr.word, 0, MSH_DDR3_ROW_TIMING, mdrt.word);

  //
  // Column timing.
  //
  MSH_DDR3_COL_TIMING_t mdct = { .word = 0 };

  mdct.wr =  PARAM_TO_CYCLES(tWRmin_fs, 5, 12);
  if (mdct.wr == 9 || mdct.wr == 11)
    mdct.wr++;
  mdct.wtr = PARAM_TO_CYCLES(tWTRmin_fs, 4, 9);
  mdct.rtp =  PARAM_TO_CYCLES(tRTPmin_fs, 4, 9);

  /** Table of standard DDR3 periods, used in calculating CWL and CL. */
  static const long cwl_tCK_tbl_fs[] =
  {
    2500000L, // CWL 5  (           tCK(avg) >= 2.5 ns)
    1875000L, // CWL 6  (2.5 ns   > tCK(avg) >= 1.875 ns)
    1500000L, // CWL 7  (1.875 ns > tCK(avg) >= 1.5 ns)
    1250000L, // CWL 8  (1.5 ns   > tCK(avg) >= 1.25 ns)
    1071000L, // CWL 9  (1.25 ns  > tCK(avg) >= 1.07ns)
     937500L, // CWL 10 (1.07 ns  > tCK(avg) >= 0.935 ns)
    LONG_MIN, // End of table
  };

  // CWL just depends upon the frequency we're using.
  for (int i = 0; ; i++)
    if (tCK_fs >= cwl_tCK_tbl_fs[i])
    {
      mdct.cwl = 5 + i;
      break;
    }

  //
  // CL is a bit more complicated; we calculate based on a notional period
  // which is the largest standard period less than or equal to the real
  // period.  That gets divided into the SPD parameter for minimum CL, then
  // we bump that up until we find a supported value.
  //
  long tCKproposed_fs = tCK_fs;

  for (int i = 0; cwl_tCK_tbl_fs[i] > 0; i++)
    if (tCKproposed_fs >= cwl_tCK_tbl_fs[i])
    {
      tCKproposed_fs = cwl_tCK_tbl_fs[i];
      break;
    }

  int min_cl = PARAM_TO_CYCLES_CLK(tAAmin_fs, 4, 19, tCKproposed_fs);
  mdct.cl = 0;

  for (int i = min_cl; i < 19; i++)
    if (minfo->cas_latencies & (1 << i))
    {
      mdct.cl = i;
      break;
    }

  if (!mdct.cl)
  {
    boot_printf("msh%d: can't find workable CL value, shim ignored "
                "(want %d, available mask 0x%lx)\n", msh_port(shimaddr), min_cl,
                minfo->cas_latencies);
    return 0;
  }

  mdct.r2w = 2;
  mdct.r2r = 1;
  mdct.w2w = 1;
  mdct.w2r = 2;

  cfg_wr(shimaddr.word, 0, MSH_DDR3_COL_TIMING, mdct.word);

  //
  // Refresh timing
  //
  // Our desired refresh interval is always 7.8 us.  The controller has a
  // refresh timing register which specifies how often it does a refresh
  // operation.  However, depending on the configuration, it refreshes
  // different sets of ranks each time it does a refresh:
  //
  // - If we're using single- or dual-rank registered DIMMs, it only does
  //   one rank at a time.  Thus, in order to make sure every rank is
  //   refreshed during our desired interval, we must set the refresh
  //   period to that interval divided by the number of ranks.
  //
  // - If we're using quad-rank registered DIMMs, it does two ranks at a
  //   time.  Thus, we must set the refresh period to our desired interval
  //   divided by half the number of ranks.
  //
  // - Otherwise, it does all ranks simultaneously.  In this case, the
  //   refresh period is equal to our desired interval.
  //
  long ref_per_fs = SCALE_FS * 7800 / SCALE_NS;
  if (minfo->rdimm || minfo->lrdimm)
  {
    if (minfo->dimm_ranks >= 4)
      ref_per_fs /= (minfo->ctl_ranks / 2);
    else
      ref_per_fs /= minfo->ctl_ranks;
  }

  MSH_DDR3_REF_TIMING_t mdrft =
  {{
    .ref_per = FS_TO_CYCLES(ref_per_fs),
  }};
  cfg_wr(shimaddr.word, 0, MSH_DDR3_REF_TIMING, mdrft.word);

  //
  // Miscellaneous timing.
  //
  MSH_DDR3_MISC_TIMING_t mdmt = { .word = 0 };

  mdmt.rfc = PARAM_TO_CYCLES(tRFCmin_fs, 6, 511);

  // 200 usec startup delay
  mdmt.startup_delay = FS_TO_CYCLES(SCALE_FS * 200 / SCALE_US);

  // DDR3 spec - table 68 - max(12nCK, 15ns)
  mdmt.mod = max(12, FS_TO_CYCLES(15 * (SCALE_FS / SCALE_NS)));

  // DDR3 spec - tXS = max(5nCK, tRFC(min) + 10ns)
  mdmt.xs = max(5, FS_TO_CYCLES(minfo->tRFCmin_fs +
                                10 * (SCALE_FS / SCALE_NS)));

  // DDR3 spec - tXPR = max(tXS, 5 x tCK) - Section 3.3.1, bullet 5
  mdmt.xpr = max((long) mdmt.xs, 5);

  cfg_wr(shimaddr.word, 0, MSH_DDR3_MISC_TIMING, mdmt.word);

  return 1;
}

/////////////////////////////////////////////////////////////////////////////
// On-Die Termination (ODT) configuration
/////////////////////////////////////////////////////////////////////////////

/** The ODT settings are represented as tables, where the X and Y indices
 *  range over the total number of ranks; one specifies the rank that's
 *  being accessed, and the other specifies the rank whose settings are
 *  held in that table element.  (In other words, rank Y's termination is
 *  this when rank X is being accessed.)  Making these all 16x16 tables
 *  would take a lot of memory, so we don't want to do that.  Also, we have
 *  another problem: the tables have entries which vary depending on some
 *  other configuration value (say, whether we have SODIMMs).  We don't
 *  really want to have multiple copies of those tables, and we can't just
 *  walk through them and change all of the varying values, since other
 *  shims might need them set differently.
 *
 *  To address this, we define a compact representation for the tables,
 *  and provide a little access routine to read them instead of using
 *  normal array references.  The tables are laid out like this:
 *
 *  table[0] - Number of ranks defined in the table.
 *  table[1] - Value to return for the magic write ODT setting (WR).
 *  table[2] - Value to return for the magic nominal ODT setting (NOM).
 *  table[3 .. 2 + ranks*ranks]: table entries, in increasing order of
 *    setting rank within increasing order of access rank.
 */
typedef uint8_t* odt_tbl;

// Define RTT (termination resistance) value(s) useful for defining the tables

/** Special value which will be replaced by the table's nominal  ODT value
 *  upon read. */
#define NOM 254

/** Special value which will be replaced by the table's write ODT value
 *  upon read. */
#define WR 255

/** Set a table's nominal ODT value. */
#define SET_NOM(table, nom) do { (table)[2] = (nom); } while (0)

/** Set a table's write ODT value. */
#define SET_WR(table, wr) do { (table)[1] = (wr); } while (0)

/** Return a value from an ODT table.
 * @param table Table.
 * @param access_rank Rank being accessed.
 * @param setting_rank Rank whose setting will be retrieved.
 */
static uint8_t
odt(odt_tbl table, int access_rank, int setting_rank)
{
  uint8_t val = table[3 + access_rank * table[0] + setting_rank];
  return (val == WR) ? table[1] : (val == NOM) ? table[2] : val;
}


/** Provide ODT setting tables for the current configuration.
 * @param rd_table Pointer to be filled in with the read table.
 * @param wr_table Pointer to be filled in with the write table.
 * @param portnum Port number of the shim.
 * @param freq_hz DDR frequency in Hertz.
 */
static void
get_odt_tables(odt_tbl* rd_table, odt_tbl* wr_table, int portnum, long freq_hz)
{
  //
  // This function specifies read and write ODT control.  There are 8 ODT
  // control pins/signals for the interface.  It is assumed that two ODT
  // signals are connected to each DIMM socket, in order; i.e. ODT 0 and 1
  // connect to the first DIMM socket, 2 and 3 to the second DIMM socket,
  // etc.
  //

  // FIXME - review to consider if this covers soldered-down correctly
  // FIXME - do we support more than single rank of soldered down???

  //
  // Read ODT table
  //

  //
  // Adjust nominal RTT value.
  //
  int nom = (minfo->numdimms > 2) ? 30 : 40;

  msh_reg_override(portnum, freq_hz,
                   BI_MSH_REG_ENTRY_PARAMETER__VAL_ODT_RTT_NOM, &nom);

  if (minfo->dimm_ranks >= 4)
  {
    //
    // Quad-rank DIMMs.
    //
    if (minfo->ctl_ranks == 4)
    {
      //
      // One DIMM.
      //
      static uint8_t tbl[] =
      {
        4,   0, 0,
        0,   0, NOM, 0,
        0,   0, NOM, 0,
        NOM, 0, 0,   0,
        NOM, 0, 0,   0,
      };
      SET_NOM(tbl, nom);
      *rd_table = tbl;
    }
    else
    {
      //
      // Two to four DIMMs (although four have never been seen to work).
      //
      static uint8_t tbl[] =
      {
        16,  0, 0,
        0,   0, 0,   0, NOM, 0, NOM, 0, NOM, 0, NOM, 0, NOM, 0, NOM, 0,
        0,   0, 0,   0, NOM, 0, NOM, 0, NOM, 0, NOM, 0, NOM, 0, NOM, 0,
        0,   0, 0,   0, NOM, 0, NOM, 0, NOM, 0, NOM, 0, NOM, 0, NOM, 0,
        0,   0, 0,   0, NOM, 0, NOM, 0, NOM, 0, NOM, 0, NOM, 0, NOM, 0,
        NOM, 0, NOM, 0, 0,   0, 0,   0, NOM, 0, NOM, 0, NOM, 0, NOM, 0,
        NOM, 0, NOM, 0, 0,   0, 0,   0, NOM, 0, NOM, 0, NOM, 0, NOM, 0,
        NOM, 0, NOM, 0, 0,   0, 0,   0, NOM, 0, NOM, 0, NOM, 0, NOM, 0,
        NOM, 0, NOM, 0, 0,   0, 0,   0, NOM, 0, NOM, 0, NOM, 0, NOM, 0,
        NOM, 0, NOM, 0, NOM, 0, NOM, 0, 0,   0, 0,   0, NOM, 0, NOM, 0,
        NOM, 0, NOM, 0, NOM, 0, NOM, 0, 0,   0, 0,   0, NOM, 0, NOM, 0,
        NOM, 0, NOM, 0, NOM, 0, NOM, 0, 0,   0, 0,   0, NOM, 0, NOM, 0,
        NOM, 0, NOM, 0, NOM, 0, NOM, 0, 0,   0, 0,   0, NOM, 0, NOM, 0,
        NOM, 0, NOM, 0, NOM, 0, NOM, 0, NOM, 0, NOM, 0, 0,   0, 0,   0,
        NOM, 0, NOM, 0, NOM, 0, NOM, 0, NOM, 0, NOM, 0, 0,   0, 0,   0,
        NOM, 0, NOM, 0, NOM, 0, NOM, 0, NOM, 0, NOM, 0, 0,   0, 0,   0,
        NOM, 0, NOM, 0, NOM, 0, NOM, 0, NOM, 0, NOM, 0, 0,   0, 0,   0,
      };
      SET_NOM(tbl, nom);
      *rd_table = tbl;
    }
  }
  else if (minfo->dimm_ranks == 1)
  {
    //
    // Single-rank.
    //
    static uint8_t tbl[] =
    {
      4,   0, 0,
      0,   NOM, NOM, NOM,
      NOM, 0,   NOM, NOM,
      NOM, NOM, 0,   NOM,
      NOM, NOM, NOM, 0, 
    };
    SET_NOM(tbl, nom);
    *rd_table = tbl;
  }
  else if (minfo->dimm_ranks == 2)
  {
    //
    // Dual-rank.
    //
    static uint8_t tbl[] =
    {
      8,   0, 0,
      0,   0, NOM, 0, NOM, 0, NOM, 0,
      0,   0, NOM, 0, NOM, 0, NOM, 0,
      NOM, 0,   0, 0, NOM, 0, NOM, 0,
      NOM, 0,   0, 0, NOM, 0, NOM, 0,
      NOM, 0, NOM, 0,   0, 0, NOM, 0,
      NOM, 0, NOM, 0,   0, 0, NOM, 0,
      NOM, 0, NOM, 0, NOM, 0,   0, 0,
      NOM, 0, NOM, 0, NOM, 0,   0, 0,
    };
    SET_NOM(tbl, nom);
    *rd_table = tbl;
  }
  else
  {
    //
    // Error.
    //
    *rd_table = NULL;
  }

  //
  // Write ODT table
  //

  //
  // Adjust write RTT value.
  //
  int wr = (minfo->sodimm) ? 40 : 120;

  //
  // With newer chips that have only one single- or dual-rank DIMM (or
  // soldered-down bank) and no board-induced DQ/DQS skew, we can improve
  // signal integrity at high speeds with a lower write termination value.
  //
  if ((is_gx72 || is_a3_or_later) && minfo->dqs_offset_ps == 0 &&
      !minfo->sodimm && minfo->numdimms <= 1 && minfo->dimm_ranks <= 2 &&
      freq_hz > 700 * 1000 * 1000)
    wr = 60;

  msh_reg_override(portnum, freq_hz,
                   BI_MSH_REG_ENTRY_PARAMETER__VAL_ODT_RTT_WR, &wr);

  if (minfo->dimm_ranks >= 4)
  {
    //
    // Quad-rank DIMMs.  Note that this table covers 1 to 4 DIMMs, although
    // 4 have never been seen to work.
    //
    static uint8_t tbl[] =
    {
      16,   0, 0,
      WR,  0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,
      0,   WR, NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,
      NOM, 0,  WR,  0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,
      NOM, 0,  0,   WR, NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,
      NOM, 0,  NOM, 0,  WR,  0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,
      NOM, 0,  NOM, 0,  0,   WR, NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,
      NOM, 0,  NOM, 0,  NOM, 0,  WR,  0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,
      NOM, 0,  NOM, 0,  NOM, 0,  0,   WR, NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,
      NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  WR,  0,  NOM, 0,  NOM, 0,  NOM, 0,
      NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  0,   WR, NOM, 0,  NOM, 0,  NOM, 0,
      NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  WR,  0,  NOM, 0,  NOM, 0,
      NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  0,   WR, NOM, 0,  NOM, 0,
      NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  WR,  0,  NOM, 0,
      NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  0,   WR, NOM, 0,
      NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  WR,  0,
      NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  NOM, 0,  0,   WR,
    };
    SET_NOM(tbl, nom);
    SET_WR(tbl, wr);
    *wr_table = tbl;
  }
  else if (minfo->dimm_ranks == 1)
  {
    //
    // Single-rank.
    //
    static uint8_t tbl[] =
    {
      4,   0, 0,
      WR,  NOM, NOM, NOM,
      NOM,  WR, NOM, NOM,
      NOM, NOM,  WR, NOM,
      NOM, NOM, NOM, WR,
    };
    SET_NOM(tbl, nom);
    SET_WR(tbl, wr);
    *wr_table = tbl;
  }
  else if (minfo->dimm_ranks == 2)
  {
    //
    // Dual-rank.
    //
    static uint8_t tbl[] =
    {
      8,   0, 0,
      WR,  0, NOM,  0, NOM,  0, NOM,  0,
      0,  WR, NOM,  0, NOM,  0, NOM,  0,
      NOM, 0,  WR,  0, NOM,  0, NOM,  0,
      NOM, 0,   0, WR, NOM,  0, NOM,  0,
      NOM, 0, NOM,  0,  WR,  0, NOM,  0,
      NOM, 0, NOM,  0,   0, WR, NOM,  0,
      NOM, 0, NOM,  0, NOM,  0,  WR,  0,
      NOM, 0, NOM,  0, NOM,  0,   0, WR,
    };
    SET_NOM(tbl, nom);
    SET_WR(tbl, wr);
    *wr_table = tbl;
  }
  else
  {
    //
    // Error.
    //
    *wr_table = NULL;
  }
}


/** Translate a logical rank to an ODT pin number.
 * @param log_rank Logical rank.
 * @return ODT pin number.
 */
static int
rank2odt(int log_rank)
{
  // Return the ODT pin number associated with the specified
  // logical rank number
  // Returns None for an odd-numbered rank of a quad-rank DIMM
  // where there is no ODT pin that controls it.

  if (minfo->dimm_ranks == 1)
    return log_rank * 2;
  else if (minfo->dimm_ranks == 2)
    return log_rank;
  else
  {
    // For quad-rank DIMMs, only even-numbered ranks map to an ODT pin
    if (log_rank & 1)
      return -1;
    else
      return log_rank >> 1;
  }
}


/** Configure on-die termination settings.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param portnum Port number of the shim.
 * @param freq_hz DDR frequency in Hertz.
 */
static void
config_odt(pos_t shimaddr, int portnum, long freq_hz)
{
  //
  // This method sets up the RTT values in mode registers, and also sets up
  // ODT control register values.
  //

  // ENHANCEME - ODT delay register programming support
  // ENHANCEME - ODT advance on, delay off control in control register

  //
  // Get the ODT tables for this configuration.
  //
  odt_tbl rd_table, wr_table;
  get_odt_tables(&rd_table, &wr_table, portnum, freq_hz);

  if (rd_table == NULL || wr_table == NULL)
  {
    DBG("Invalid ODT table, skipping config_odt\n");
    return;
  }

  //
  // Set ODT control values for writes and reads as we go; 1 control value
  // per rank accessed.
  //
  uint16_t odt_rd_bits[16] = { 0 };
  uint16_t odt_wr_bits[16] = { 0 };

  //
  // Consider each rank that can provide termination.
  //
  for (int odt_rank = 0; odt_rank < minfo->ctl_ranks; odt_rank++)
  {
    //
    // Reads
    //

    //
    // Evaluate all read ODT needs first to see if they need to constrain
    // nominal RTT.
    //

    //
    // A value of -1 means that this value isn't constrained yet.
    //
    int cfg_rtt_nom = -1;

    //
    // Consider each rank that might be accessed.
    //
    for (int access_rank = 0; access_rank < minfo->ctl_ranks; access_rank++)
    {
      //
      // Get the specified termination.
      //
      int term_rd = odt(rd_table, access_rank, odt_rank);
      int assert_odt_pin_rd = 0;

      //
      // Figure out if the ODT pin will/should be asserted or not.
      //
      if (rank2odt(odt_rank) < 0)
      {
        //
        // Odd-numbered ranks of quad-rank DIMMs will always have their ODT
        // input asserted; they are not connected to any Gx ODT control pin.
        //
        assert_odt_pin_rd = 1;
      }
      else
      {
        if (term_rd == 0)
          assert_odt_pin_rd = 0;
        else
        {
          assert_odt_pin_rd = 1;

          //
          // The controller needs to assert the ODT pin.  Set the bit in
          // the control value.
          //
          odt_rd_bits[access_rank] |= 1 << rank2odt(odt_rank);
        }
      }

      //
      // If the ODT pin will be asserted, make sure that the nominal RTT
      // value doesn't conflict with any earlier value and constrain it to
      // this value.
      //
      if (assert_odt_pin_rd)
      {
        if (cfg_rtt_nom >= 0 && cfg_rtt_nom != term_rd)
        {
          DBG("Nominal RTT already constrained\n");
          return;
        }
        else
          cfg_rtt_nom = term_rd;
      }
    }

    //
    // Writes
    //

    //
    // A value of -1 means that this value isn't constrained yet.
    //
    int cfg_rtt_wr = -1;

    //
    // Consider each rank that might be accessed.
    //
    for (int access_rank = 0; access_rank < minfo->ctl_ranks; access_rank++)
    {
      //
      // Get the specified termination.
      //
      int term_wr = odt(wr_table, access_rank, odt_rank);
      int assert_odt_pin_wr = 0;

      //
      // Figure out if the ODT pin will/should be asserted or not.
      //
      if (rank2odt(odt_rank) < 0)
      {
        //
        // Odd-numbered ranks of quad-rank DIMMs will always have their ODT
        // input asserted; they are not connected to any Gx ODT control pin.
        //
        assert_odt_pin_wr = 1;
      }
      else
      {
        if (term_wr == 0)
          assert_odt_pin_wr = 0;
        else
        {
          assert_odt_pin_wr = 1;

          //
          // The controller needs to assert the ODT pin.  Set the bit in
          // the control value.
          //
          odt_wr_bits[access_rank] |= 1 << rank2odt(odt_rank);
        }
      }

      //
      // If the ODT pin will be asserted, figure out how to set up that
      // termination value in a mode register.
      //
      if (assert_odt_pin_wr)
      {
        //
        // If the nominal RTT isn't already constrained based on the
        // termination requirements for reads, use it for writes because it
        // has less constraints on its value than write RTT has.
        //
        if (cfg_rtt_nom < 0)
        {
          //
          // Make sure the termination is a valid value for a write
          // termination using nominal RTT.
          //
          if (term_wr != RZQ / 2 && term_wr != RZQ / 4 && term_wr != RZQ / 6)
          {
            DBG("Invalid write termination value for nominal RTT\n");
            return;
          }
          else
            cfg_rtt_nom = term_wr;
        }
        else
        {
          //
          // If the specified termination isn't the already-selected
          // nominal RTT, we will have to try to use the write RTT for it.
          //
          if (term_wr != cfg_rtt_nom)
          {
            //
            // Dynamic ODT only applies to accesses to the rank providing
            // the termination.  If this is not an access to this same
            // rank, this is an error.
            //
            if (odt_rank != access_rank)
            {
              DBG("Write termination specified for non-accesed rank "
                  "different from nominal RTT\n");
              return;
            }
            else
            {
              //
              // Make sure the termination is a valid value for a write
              // termination using write RTT.
              //
              if (term_wr != RZQ / 2 && term_wr != RZQ / 4)
              {
                DBG("Invalid write termination value for write RTT\n");
                return;
              }
              else
                cfg_rtt_wr = term_wr;
            }
          }
        }
      }
    }

    //
    // If we haven't chosen an RTT value yet, disable it.
    //
    if (cfg_rtt_nom < 0)
      cfg_rtt_nom = 0;
    if (cfg_rtt_wr < 0)
      cfg_rtt_wr = 0;

    //
    // Rewrite the mode registers appropriately.
    //
    DBG("ODT rank %d: nominal RTT = %d, write RTT = %d\n",
        odt_rank, cfg_rtt_nom, cfg_rtt_wr);

    set_rtt_nom(shimaddr, cfg_rtt_nom, odt_rank);
    set_rtt_wr(shimaddr, cfg_rtt_wr, odt_rank);
  }

  //
  // Set up the ODT map registers.
  //
  cfg_wr(shimaddr.word, 0, MSH_DDR3_WRITE_ODT_30,
         ((uint64_t) odt_wr_bits[3] << 48) |
         ((uint64_t) odt_wr_bits[2] << 32) |
         ((uint64_t) odt_wr_bits[1] << 16) |
         ((uint64_t) odt_wr_bits[0] << 0));
  cfg_wr(shimaddr.word, 0, MSH_DDR3_WRITE_ODT_74,
         ((uint64_t) odt_wr_bits[7] << 48) |
         ((uint64_t) odt_wr_bits[6] << 32) |
         ((uint64_t) odt_wr_bits[5] << 16) |
         ((uint64_t) odt_wr_bits[4] << 0));
  cfg_wr(shimaddr.word, 0, MSH_DDR3_WRITE_ODT_B8,
         ((uint64_t) odt_wr_bits[11] << 48) |
         ((uint64_t) odt_wr_bits[10] << 32) |
         ((uint64_t) odt_wr_bits[9] << 16) |
         ((uint64_t) odt_wr_bits[8] << 0));
  cfg_wr(shimaddr.word, 0, MSH_DDR3_WRITE_ODT_FC,
         ((uint64_t) odt_wr_bits[15] << 48) |
         ((uint64_t) odt_wr_bits[14] << 32) |
         ((uint64_t) odt_wr_bits[13] << 16) |
         ((uint64_t) odt_wr_bits[12] << 0));

  cfg_wr(shimaddr.word, 0, MSH_DDR3_READ_ODT_30,
         ((uint64_t) odt_rd_bits[3] << 48) |
         ((uint64_t) odt_rd_bits[2] << 32) |
         ((uint64_t) odt_rd_bits[1] << 16) |
         ((uint64_t) odt_rd_bits[0] << 0));
  cfg_wr(shimaddr.word, 0, MSH_DDR3_READ_ODT_74,
         ((uint64_t) odt_rd_bits[7] << 48) |
         ((uint64_t) odt_rd_bits[6] << 32) |
         ((uint64_t) odt_rd_bits[5] << 16) |
         ((uint64_t) odt_rd_bits[4] << 0));
  cfg_wr(shimaddr.word, 0, MSH_DDR3_READ_ODT_B8,
         ((uint64_t) odt_rd_bits[11] << 48) |
         ((uint64_t) odt_rd_bits[10] << 32) |
         ((uint64_t) odt_rd_bits[9] << 16) |
         ((uint64_t) odt_rd_bits[8] << 0));
  cfg_wr(shimaddr.word, 0, MSH_DDR3_READ_ODT_FC,
         ((uint64_t) odt_rd_bits[15] << 48) |
         ((uint64_t) odt_rd_bits[14] << 32) |
         ((uint64_t) odt_rd_bits[13] << 16) |
         ((uint64_t) odt_rd_bits[12] << 0));
}


/////////////////////////////////////////////////////////////////////////////
// Begin config
/////////////////////////////////////////////////////////////////////////////

/** Start the shim configuration process.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param addr_mirror Address mirroring bitmap.
 * @param portnum Port number of the shim (for error messages).
 * @param freq_hz DDR frequency in Hertz.
 * @return 1 if configuration was successfully started, 0 otherwise.
 */
static int
begin_config(pos_t shimaddr, int addr_mirror, int portnum, long freq_hz)
{
  //
  // Initial config
  //

  DBG("Beginning configuration\n");

  //
  // Get our core VID code and then the voltage, as these values affect
  // certain configuration parameters.
  //
  core_vid = cfg_rd(__insn_mfspr(SPR_RSHIM_COORD), 0, RSH_VID_CORE);
  core_voltage_uv = VID_TO_UV(core_vid);

  //
  // Disable auto zq calibration and auto refresh because they interfere
  // with DRAM init, mode register writes and register DIMM control word
  // writes.
  //
  MSH_DDR3_ZQ_t mdz = { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_ZQ) };
  mdz.auto_cal_en = 0;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_ZQ, mdz.word);

  //
  // Control.
  //
  MSH_DDR3_CONTROL_t mdc = { .word = 0 };
  mdc.single_rank_cmd = minfo->rdimm || minfo->lrdimm;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_CONTROL, mdc.word);

  //
  // Device: set bank/row/column bits.
  //
  MSH_DDR3_DEVICE_t mdd = { .word = 0 };
  mdd.bankbits = MSH_DDR3_DEVICE__BANKBITS_VAL_BITS3;
  mdd.rowbits = MSH_DDR3_DEVICE__ROWBITS_VAL_BITS12 + minfo->row_bits - 12;
  mdd.colbits = MSH_DDR3_DEVICE__COLBITS_VAL_BITS10 + minfo->col_bits - 10;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_DEVICE, mdd.word);

  if (!config_timing_regs(shimaddr))
    return 0;

  //
  // DIMM configuration.
  //
  MSH_DDR3_DIMM_CFG_t mddc = { .word = 0 };
  mddc.regdimm = minfo->rdimm;

  if (minfo->dimm_ranks >= 4)
  {
    mddc.quad = 1;
    mddc.cke_mapping = MSH_DDR3_DIMM_CFG__CKE_MAPPING_VAL_QUAD;
  }

  msh_reg_override(portnum, freq_hz,
                   BI_MSH_REG_ENTRY_PARAMETER__VAL_ADDR_MIRROR, &addr_mirror);

  mddc.addr_mirror = addr_mirror;

  mddc.num_ranks = minfo->ctl_ranks;

  //
  // Compute the rank mapping mode, and the rank map.  Note: we don't do
  // mode 4 yet.
  //
  if (minfo->cs_per_slot == minfo->dimm_ranks)
    mddc.rank_mapping = MSH_DDR3_DIMM_CFG__RANK_MAPPING_VAL_MODE0;
  else if (minfo->cs_per_slot == 4 && minfo->dimm_ranks == 1)
    mddc.rank_mapping = MSH_DDR3_DIMM_CFG__RANK_MAPPING_VAL_MODE3;
  else if (minfo->cs_per_slot == 4 && minfo->dimm_ranks == 2)
    mddc.rank_mapping = MSH_DDR3_DIMM_CFG__RANK_MAPPING_VAL_MODE2;
  else if (minfo->cs_per_slot == 2 && minfo->dimm_ranks == 1)
    mddc.rank_mapping = MSH_DDR3_DIMM_CFG__RANK_MAPPING_VAL_MODE1;
  else
  {
    boot_printf("msh%d: no rank mapping mode (%d ranks, %d cs/slot), "
                "shim ignored\n", portnum, minfo->dimm_ranks,
                minfo->cs_per_slot);
    return 0;
  }

  cfg_wr(shimaddr.word, 0, MSH_DDR3_DIMM_CFG, mddc.word);

  //
  // PHY-related config
  //

  //
  // Mode control register.
  //
  MSH_DDR3_MODE_CONTROL_t mdmc = { .word = cfg_rd(shimaddr.word, 0,
                                                  MSH_DDR3_MODE_CONTROL) };

  mdmc.width_mode = (minfo->sdram_width_bits == 4);
  if (dimm_voltage != VOLT_1_5)
    mdmc.ddr3l_mode = 1;

  cfg_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc.word);

  //
  // PHY delay register.
  //
  MSH_DDR3_PHY_DELAY_t mdpd = { .word = 0 };
  //
  // Read latency; gets higher as the frequency goes up.
  //
  int rdlat = minfo->sodimm ? 8 :7;
  if (freq_hz > 666666666)
    rdlat = 9;
  else if (freq_hz > 533333333)
    rdlat = 8;
  msh_reg_override(portnum, freq_hz, BI_MSH_REG_ENTRY_PARAMETER__VAL_RDLAT,
                   &rdlat);

  mdpd.rdlat = rdlat;
  DBG("Setting rdlat to %d\n", rdlat);
  mdpd.rddata_en = 7;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_PHY_DELAY, mdpd.word);

  //
  // RX termination register.
  //
  MSH_DDR3_DQS_TX_CONTROL_t mddtc = { .word = 0 };
  mddtc.preamble = 2;
  mddtc.postamble = 1;  // Not sure if this field is used or not
  cfg_wr(shimaddr.word, 0, MSH_DDR3_DQS_TX_CONTROL, mddtc.word);

  //
  // Address/command/control control register
  //
  // Configure acc retime edge and init_value based on delay-line settings.
  //
  const int acc_delay0 = minfo->sodimm ? 16 : 12;
  const int acc_delay1 = minfo->sodimm ? 16 : 12;

  DBG("acc_delay 0/1 = %d/%d\n", acc_delay0, acc_delay1);

  write_acc_control(shimaddr, acc_delay0, acc_delay1);

  //
  // Address/command/control force low power register
  //
  cfg_wr(shimaddr.word, 0, MSH_DDR3_ACC_FORCE_LP_DRIVE, (1UL << 57) - 1);

  //
  // Baseline control: enable the shim.
  //
  static const MSH_BASELINE_CTL_t mbc =
  {{
    .enable = 1,
  }};
  cfg_wr(shimaddr.word, 0, MSH_BASELINE_CTL, mbc.word);

  return 1;
}

/////////////////////////////////////////////////////////////////////////////
// Config pads
/////////////////////////////////////////////////////////////////////////////

/** Configure PAD drive strengths.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param portnum Port number of the shim.
 * @param freq_hz DDR frequency in Hertz.
 */
static void
config_pads(pos_t shimaddr, int portnum, long freq_hz)
{
  //
  // Configure pads.
  //
  DBG("IO compensation and pad configuration\n");

  //
  // IO Compensation
  //

  //
  // Start the hardware state machine for the IO compensation calibration.
  // Read out the calibration result until it is done.
  //
  // p transistor is calibrated by TX_TH, REXT_P is connected to external
  // pull-down resistor.
  //
  // n transistor is calibrated by TX_TL, REXT_N is connected to external
  // pull-up resistor.
  //
  // ENHANCEME: driver impedance setting - to be investigated.
  //

  DBG("IO compensation\n");

  // Make sure we are out of bypass mode

  MSH_DDR3_COMP_BYP_t mdcb = { .word = 0 };
  mdcb.byp = 0;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_COMP_BYP, mdcb.word);

  // Start the IO compensation process

  MSH_DDR3_COMP_CAL_t mdcc = { .word = 0 };
  mdcc.start = 1;
  mdcc.settling = 0x7f;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_COMP_CAL, mdcc.word);

  // Wait until the calibration is done.
  do
    mdcc.word = cfg_rd(shimaddr.word, 0, MSH_DDR3_COMP_CAL);
  while (mdcc.start == 0);

  // Print results.
  MSH_DDR3_COMP_STS_t mdcs = { .word = cfg_rd(shimaddr.word, 0,
                                              MSH_DDR3_COMP_STS) };
  DBG("IO compensation calibration is done, tl %d, th %d\n",
      mdcs.tl, mdcs.th);

  int th = mdcs.th;
  int tl = mdcs.tl;

  //
  // On newer chips, we use slightly higher drive strength, since this has
  // been shown to improve performance, particularly with slow parts or
  // with two DIMMs.  This might be beneficial for older chips as well, but
  // we've only done sufficient characterization on newer ones, so we
  // continue to use the historic values on Gx36 A2 and earlier.
  //
  int target_drvh;
  int target_drvl;

  if (is_gx72 || is_a3_or_later)
  {
    target_drvh = (minfo->numdimms > 2) ? 30 : 34;
    target_drvl = (minfo->numdimms > 2) ? 30 : 34;
  }
  else
  {
    target_drvh = (minfo->numdimms > 2) ? 38 : 40;
    target_drvl = (minfo->numdimms > 2) ? 38 : 40;
  }

  int pad_drvh = (th * 30) / target_drvh;
  int pad_drvl = (tl * 30) / target_drvl;

  int target_termh = 75;
  int target_terml = 75;

  msh_reg_override(portnum, freq_hz,
                   BI_MSH_REG_ENTRY_PARAMETER__VAL_CTRL_TERM, &target_termh);
  msh_reg_override(portnum, freq_hz,
                   BI_MSH_REG_ENTRY_PARAMETER__VAL_CTRL_TERM, &target_terml);

  int pad_termh = (th * 30) / target_termh;
  int pad_terml = (tl * 30) / target_terml;

  DBG("pad_drv h/l 0x%x/0x%x pad_term h/l 0x%x/0x%x\n",
      pad_drvh, pad_drvl, pad_termh, pad_terml);

  MSH_DDR3_CK_PAD_CONTROL_t mdckpc =
  {{
    .oe = 0xf,
    .grpblk = 3,
    .drvh = pad_drvh,
    .drvl = pad_drvl,
    .duty = 0,
  }};

  cfg_wr(shimaddr.word, 0, MSH_DDR3_CK_PAD_CONTROL, mdckpc.word);

  MSH_DDR3_ACC_PAD_CONTROL_t mdapc =
  {{
    .grpblk = 3,
    .drvh = pad_drvh,
    .drvl = pad_drvl,
    .th = pad_termh,
    .tl = pad_terml,
  }};

  cfg_wr(shimaddr.word, 0, MSH_DDR3_ACC_PAD_CONTROL, mdapc.word);
  for (int i = MSH_DDR3_DATA0_PAD_CONTROL;
       i <= MSH_DDR3_DATA8_PAD_CONTROL; i += 8)
    cfg_wr(shimaddr.word, 0, i, mdapc.word);
}

/////////////////////////////////////////////////////////////////////////////
// Start init
/////////////////////////////////////////////////////////////////////////////

/** Initialize the memory controller and the register on any registered DIMMs.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param portnum Port number of the shim.
 * @param freq_hz DDR frequency in Hertz.
 */
static void
start_init(pos_t shimaddr, int portnum, long freq_hz)
{
  DBG("Asserting DFI_INIT\n");

  //
  // Disable auto init before asserting dfi_init which will allow the
  // controller state machine to come out of reset.  We allow this init to
  // assert reset because this is the first one.  We disable auto init
  // which will prevent both mode register writes and ZQ calibration.  We
  // prevent the mode register writes because, if this is a registered
  // DIMM, we haven't set up the register chip control words yet which is
  // necessary for the RAM mode register writes to work.
  //
  MSH_DDR3_USER_INIT_0_t mdui0 =
    { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_USER_INIT_0) };
  mdui0.init_mask_reset = 0;
  mdui0.autoinit_dis = 1;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_USER_INIT_0, mdui0.word);

  //
  // Assert DFI init to let the controller come out of reset and go through
  // an init sequence.
  //
  MSH_DDR3_MODE_CONTROL_t mdmc =
    { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL) };
  mdmc.dfi_init = 1;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_MODE_CONTROL, mdmc.word);

  //
  // Wait for controller's state machine to be done.
  //
  MSH_STATUS_t ms = { .word = 0 };
  while (!ms.init_done)
    ms.word = cfg_rd(shimaddr.word, 0, MSH_STATUS);

  //
  // The need for this delay is not quite fully understood (the controller
  // ought to be done at this point), but without it we see training
  // failures at some CPU core frequencies (e.g., 1 GHz).  The key events
  // which appear to need separation are the completion of DFI_INIT and
  // the write to RDIMM control word 10 which happens at the start of
  // config_rdimm().
  //
  // In testing, 2 us was sufficient, while 1 us was not; using 10 us is
  // just being paranoid.
  //
  drv_udelay(10);

  //
  // If we are working with registered DIMMs, init their control words.
  //
  if (minfo->rdimm)
    config_rdimm(shimaddr, freq_hz);

#ifdef LRDIMM
  if (minfo->lrdimm)
    config_lrdimm(shimaddr, freq_hz);
#endif

  //
  // Do the DRAM init again, this time NOT disabling the auto-init feature
  // so that the mode register writes occur as part of it.  We mask the
  // reset so that any control words written to registered DIMMs don't
  // get erased.
  //
  mdui0.word = cfg_rd(shimaddr.word, 0, MSH_DDR3_USER_INIT_0);
  mdui0.ctrl_init = 1;
  mdui0.init_mask_reset = 1;
  mdui0.autoinit_dis = 0;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_USER_INIT_0, mdui0.word);
  mdui0.ctrl_init = 0;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_USER_INIT_0, mdui0.word);

  ms.word = 0;

  // Wait for init "ack"
  while (!ms.init_ack)
    ms.word = cfg_rd(shimaddr.word, 0, MSH_STATUS);

  // Wait for init "done"
  while (!ms.init_done)
    ms.word = cfg_rd(shimaddr.word, 0, MSH_STATUS);
  
  //
  // The controller just wrote to the mode registers.  We now update the
  // shadow mode registers with the values that were written to them by
  // hardware; then when we read the values back later they'll be correct.
  //
  init_mode_regs(shimaddr);

#ifdef LRDIMM
  if (minfo->lrdimm)
    config_lrdimm_post(shimaddr, freq_hz);
#endif

  //
  // Configure ODT control - this also modifies the RTT values in the mode
  // registers.
  //
  config_odt(shimaddr, portnum, freq_hz);
}

/////////////////////////////////////////////////////////////////////////////
// Delay candidate testing
/////////////////////////////////////////////////////////////////////////////



/////////////////////////////////////////////////////////////////////////////
// Determine delays
/////////////////////////////////////////////////////////////////////////////

/** Do training and determine a working set of configuration settings.
 * @param shimaddr Tile coordinates of the memory shim.
 * @param portnum Port number of the shim (for error messages).
 * @param train_rank Which rank to place into leveling mode.
 * @param freq_hz Memory interface frequency (half of our T/s value).
 * @return Zero if training was successful, nonzero otherwise.
 */
static int
determine_delays(pos_t shimaddr, int portnum, int train_rank, long freq_hz)
{
  int num_d_lanes = num_data_lanes();
  int num_c_lanes = num_config_lanes();

  DBG("*** Leveling DDR3 interface ***\n");

  //
  // If we're using the RX DLL, reset it; if not, the master will just be
  // held in reset since that's the default state.  If the slaves are
  // individually controlled, we need to hold them in reset explicitly.
  //
  if (rx_dll_enabled)
    reset_rx_dll(shimaddr);
  else if (rx_slave_reset_supported)
    for (int lane = 0; lane < num_c_lanes; lane++)
      assert_rx_dll_slave_reset_lane(shimaddr, lane);

  //
  // Get RX gate delay candidates.
  //
  uint64_t gate_results[NLANE];
  DBG("Getting receive gate delay candidates\n");
  get_gate_delay_candidates(shimaddr, num_c_lanes, gate_results);

  //
  // The gate leveling process above should have found at least one set of
  // settings to try for each lane; make sure that it did.
  //
  for (int lane = 0; lane < num_c_lanes; lane++)
    if (gate_results[lane] == 0)
    {
      boot_printf("msh%d: lane %d did not have gate level settings "
                  "to consider, shim ignored\n", portnum, lane);
      return 1;
    }

  //
  // Set up the Rx DQ bit delays that we will use while searching for
  // working Tx DQS delays and Rx gate delays.
  //
  // If we are enabling the Rx DLL, we allow the search to use different Rx
  // DQS delay values, so we just use a Rx DQ bit delay value of 0.  Later,
  // when we do an integrated search for both Rx DQS delays and Rx DQ bit
  // delays, we will tune these values.
  //
  // If we are not enabling the Rx DLL, we configure the Rx DQ bit delays
  // using values that are a function of the operating speed and the extra
  // DQS delay on the board.
  //
  int bit_delay;
  if (rx_dll_enabled)
    bit_delay = 0;
  else
  {
    long cutover = is_a3_or_later ? 650000000 : 666666666;
    bit_delay = (freq_hz > cutover && minfo->dqs_offset_ps > 0) ? 4 : 0;
    DBG("Set RX bit delays to %d\n", bit_delay);
  }

  for (int lane = 0; lane < num_c_lanes; lane++)
    rx_bit_delays[lane] = bit_delay;

  config_rx_bit_delays(shimaddr, num_c_lanes);

  //
  // Keep track of that last working delay values found; it's useful to use
  // them to reorder the search list for the next DQS to find a working
  // value more quickly.  We set our initial "good" value at about the
  // middle of the range to try and minimize the search on the first lane.
  //
  good_tx_dqs_delay = 150;
  good_phase_delay = 0;

  DBG("\nSearching for final Rx gate delays and initial Tx delays\n\n");

  //
  // Get TX DQS delay candidates.
  //
  int tx_delays[MAX_DELAYS];
  int tx_ndelays;

  const int tx_delay_center = 150;
  tx_delays[0] = tx_delay_center;
  tx_ndelays = 1;
  for (int i = 8; i < 100; i += 8)
  {
    int delay = tx_delay_center + i;
    if (delay <= MAX_TX_DQS_DELAY && tx_ndelays < MAX_DELAYS)
      tx_delays[tx_ndelays++] = delay;
    delay = tx_delay_center - i;
    if (delay >= MIN_TX_DQS_DELAY && tx_ndelays < MAX_DELAYS)
      tx_delays[tx_ndelays++] = delay;
  }

  //
  // Find working Tx and gate delays.
  //
  int fail_vector = 0;

  //
  // First, we find a working set of Tx, Rx, and gate delays for each lane.
  // We don't really care about the Tx and Rx delays.  As soon as we're
  // done with this, we're going to throw both of those out, and run a
  // different set of algorithms to pick new ones, which will give us much
  // better values.  However, the gate delays we come up with here are the
  // ones we're really going to use.  The Tx and Rx delays just get set
  // because we need at least temporarily working values for those to find
  // the gate delays.
  //

  fail_vector = search_data_tx_and_gate_delays(shimaddr, num_d_lanes, tx_delays,
                                               tx_ndelays, gate_results);

  int max_delays[NLANE];
  int min_delays[NLANE];

  if (!fail_vector)
  {
    //
    // Find maximum working Tx delays for each lane.
    //

    DBG("\nSearching for max Tx delays for data lanes\n");

    fail_vector = find_data_tx_delay_limit(shimaddr, 1, max_delays,
                                           num_d_lanes);
  }

  if (!fail_vector)
  {
    //
    // Find minimum working Tx delays for each lane.
    //

    DBG("\nSearching for min Tx delays for data lanes\n");

    fail_vector = find_data_tx_delay_limit(shimaddr, 0, min_delays,
                                           num_d_lanes);
  }

  if (!fail_vector)
  {
    //
    // Find the midpoints and set those up as the delays we'll use.
    //
    DBG("\nChoosing Tx delays for data lanes\n");

    for (int lane = 0; lane < num_d_lanes; lane++)
    {
        int delay_range = max_delays[lane] - min_delays[lane];
        set_up_tx_delays(lane, min_delays[lane] + (delay_range / 2), 0);
        DBG("  Lane %2d: min = %d, max = %d, range = %d, mid = %d\n", lane,
            min_delays[lane], max_delays[lane], delay_range,
            tx_dqs_delays[lane]);
    }
  }

  if (!fail_vector)
  {
    //
    // Squeeze the delays for the data lanes.  Because they were found
    // above for each lane independently, as a set, they may not be
    // configurable with a common wrlat value.  To deal with this case, we
    // squeeze outliers so that the set of delays all fall within two
    // cycles and can be configured with a common wrlat.
    //
    if (squeeze_tx_delays(shimaddr, num_d_lanes))
      return 1;
    
    DBG("\nNudging Tx delays for data lanes\n");

    for (int lane = 0; lane < num_d_lanes; lane++)
      if (nudge_tx_delay(shimaddr, lane, tx_dqs_delays[lane]))
        return 1;

    //
    // Before using the Tx delays found to find the Rx delays, configure
    // them as a set with a common wrlat.
    //
    config_tx_delays(shimaddr, 0, num_d_lanes);
  }

  int target_width = 0;

  if (!fail_vector && rx_dll_enabled)
  {
    DBG("\nSearching for Rx DQS and DQ delays for data lanes\n");
    
    //
    // Find an Rx DQS delay and Rx DQ bit delay for each data lane.
    //

    //
    // Determine the target margin value that will be used if we are using
    // the margin search method.  If slave reset is supported, we set the
    // target to be impossibly high so that we will always let the search
    // hit the trailing edge.
    //
    if (rx_slave_reset_supported)
    {
      target_width = 50000;
      fail_vector = search_rx_delays_all_data_lanes_rsrs(shimaddr,
                                                         target_width);
    }
    else
    {
      target_width = 320;
      fail_vector = search_rx_delays_all_data_lanes(shimaddr, target_width);
    }
  }

  //
  // If we are going to be enabling ECC, configure the ECC lanes now.  We
  // can only enable ECC and train ECC lanes if the data lanes are good.
  //
  if (!fail_vector && minfo->ecc)
  {
    DBG("\nSearching for all delays for ECC lanes\n");

    //
    // Enable ECC
    //
    MSH_CONTROL_t mc = { .word = cfg_rd(shimaddr.word, 0, MSH_CONTROL) };
    mc.ecc_check = 1;
    mc.ecc_cor = 1;
    cfg_wr(shimaddr.word, 0, MSH_CONTROL, mc.word);

    if (!search_delays_ecc_lane(shimaddr, tx_delays, tx_ndelays,
                                gate_results, target_width))
    {
      fail_vector |= 1 << low_ecc_lane();
      if (high_ecc_lane() >= 0)
        fail_vector |= 1 << low_ecc_lane();
    }
  }

  if (fail_vector)
  {
    boot_printf("msh%d: some DQs could not be trained (mask 0x%x), "
                "shim ignored\n", portnum, fail_vector);

    return 1;
  }

  return 0;
}

/////////////////////////////////////////////////////////////////////////////
// Complete configuration
/////////////////////////////////////////////////////////////////////////////

/** Finish the shim configuration process.
 * @param shimaddr Tile coordinates of the memory shim.
 */
static void
complete_config(pos_t shimaddr)
{
  //
  // Complete configuration
  //
  DBG("Completing configuration\n");

  // Reset pointers in the PHY by pulsing the sync signal
  // This cleans up after training
  pulse_ptr_sync(shimaddr);

  //
  // Enable auto-ZQ.
  //
  MSH_DDR3_ZQ_t mdz = { .word = cfg_rd(shimaddr.word, 0, MSH_DDR3_ZQ) };
  mdz.auto_cal_en = 1;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_ZQ, mdz.word);

  //
  // Turn auto refresh on.
  //
  MSH_DDR3_CONTROL_t mdc = { .word = cfg_rd(shimaddr.word, 0,
                                            MSH_DDR3_CONTROL) };
  mdc.auto_ref = 1;
  cfg_wr(shimaddr.word, 0, MSH_DDR3_CONTROL, mdc.word);

  DBG("Configuration complete\n");

  //
  // Final controller configuration.
  //

  MSH_CONTROL_t mc = { .word = cfg_rd(shimaddr.word, 0, MSH_CONTROL) };

  //
  // If we have ECC memory, enable ECC correction and detection.
  //
  if (minfo->ecc)
  {
    mc.ecc_cor = 1;
    mc.ecc_check = 1;
  }

  //
  // Enable address hashing for banks & ranks based on the number of actual
  // ranks/banks present.  We always have at least 8 banks so we always
  // hash on bank address.
  //
  int addr_hash = 7;
  if (minfo->dimm_ranks > 1)
    addr_hash |= 0x8;
  if (minfo->dimm_ranks > 2)
    addr_hash |= 0x10;
  mc.addr_hash = addr_hash;

  cfg_wr(shimaddr.word, 0, MSH_CONTROL, mc.word);
}


/////////////////////////////////////////////////////////////////////////////
// Main memory config flow
/////////////////////////////////////////////////////////////////////////////

long
mshim_preconfig_shim(pos_t shimaddr, pos_t rshimaddr, uint32_t board_flags,
                     int speed)
{
  //
  // If we're on the simulator or FPGA, just return a fixed speed; the
  // code in mshim_config_shim_{sim,fpga} doesn't depend on minfo being
  // set.
  //
  if (sim_is_simulator())
    return 1333333333;

  if (board_flags & BOARD_FPGA)
    return 25000000;

  //
  // A speed of -1 means "disable this shim".  Similarly, no real shim can
  // be at (0,0), so those coordinates mean we didn't probe this shim.
  //
  if (speed < 0 || shimaddr.word == 0)
    return 0;

  //
  // Figure out chip version.
  //
  RSH_REV_ID_t rri = { .word = cfg_rd(rshimaddr.word, 0, RSH_REV_ID) };
  is_a2_or_later = rri.chip_rev_id >= 0x23;
  is_a3_or_later = rri.chip_rev_id >= 0x27;
  rx_slave_reset_supported = is_gx72 || is_a3_or_later;

  //
  // Get shim port number, which we'll need for the BIB lookup.
  //
  int portnum = msh_port(shimaddr);

  minfo = &minfos[portnum];

  if (board_flags & BOARD_BRINGUP_BOARD)
  {
    //
    // FIXME - don't use msh1 (soldered-down bank) on BuBs yet, since we
    // don't have a config for it.
    //
    if (portnum == 1)
    {
      cfg_wr(shimaddr.word, 0, MSH_BASELINE_CTL, 0);
      return 0;
    }
  }

  DBG("\nPreconfiguring mshim %d\n", portnum);

  //
  // Find the DIMM information based on the board information block.
  //
  bi_ptr_t resptr;
  uint32_t desc = bi_getparam(BI_TYPE_DIMM_MAP, portnum, &resptr, NULL);

  //
  // No DIMM_MAP entry, no memory.
  //
  if (desc == BI_NULL)
    return 0;

  struct bi_dimm_map* dm = resptr;

  int numdimms = BI_WDS(desc) - 1;

  //
  // Allocate mem_info structures, then fill one in for each DIMM.
  // If we find that a particular DIMM isn't there, stop looking; we
  // don't support non-contiguous sets of DIMMs.
  //
  struct mem_info* dimm_info = alloca(numdimms * sizeof (*dimm_info));

  for (int i = 0; i < numdimms; i++)
  {
    if (!get_dimm_info(&dm->map[i], &dimm_info[i], rshimaddr))
    {
      numdimms = i;
      break;
    }
  }

  //
  // No DIMMs, no memory.
  //
  if (!numdimms)
    return 0;

  //
  // Merge DIMM information into one set of values.
  //
  if (coalesce_mem_info(portnum, dimm_info, numdimms, minfo))
    return 0;

  DBG("mshim %d has %d DIMMs, %ld Mbytes total\n", portnum, numdimms,
      minfo->capacity_bytes >> 20);

  //
  // Update minfo with DQS skew and chip select information.
  //
  minfo->dqs_offset_ps = dm->dqs_offset * 10;
  minfo->cs_per_slot = dm->cs_per_slot;
  minfo->numdimms = numdimms;

  //
  // Calculate address mirroring info.  Note that this only applies to
  // unregistered DIMMs; we don't test that here because we already did
  // when setting dimm_info.rank_1_mirrored.
  //
  minfo->addr_mirror = 0;
  for (int i = 0; i < numdimms; i++)
    if (dimm_info[i].rank_1_mirrored)
      minfo->addr_mirror |= 2 << (minfo->dimm_ranks * i);

  //
  // Calculate frequency.  Note that this is the memory data rate
  // (transactions per second); later, in mshim_config_shim, we will be
  // using the actual DDR clock frequency, which is half this value.
  //
  long freq_tps = 2 * SCALE_FS / minfo->tCKmin_fs;

  //
  // Clip memory speed to the maximum supported by the board.
  //
  desc = bi_getparam(BI_TYPE_MAX_MEM_SPEED, portnum, &resptr, NULL);
  if (desc != BI_NULL)
  {
    struct bi_max_mem_speed* mms = resptr;

    //
    // This will eventually point at the table entry we'll use.  We start
    // by getting rid of any zero values at the end of the speed array.
    //
    int speed_index = 2 * BI_WDS(desc) - 1;
    while (speed_index >= 0 && mms->speed[speed_index] == 0)
      speed_index--;

    //
    // speed_index points at the last table value.  If the number of DIMMS
    // points to an earlier value, adjust speed_index to match.
    //
    speed_index = min(speed_index, minfo->numdimms - 1);

    //
    // Now clip the frequency to the table value.
    //
    long table_hz = mms->speed[speed_index] * 1000 * 1000;

    if (minfo->dimm_ranks >= 4)
    {
      //
      // Previously we had been limiting quad-rank DIMMs to 1066 MT/s in
      // all cases.  The behavior we've seen with a bit more testing is
      // more complex; it looks like quad-ranks are about a speed grade
      // slower than the equivalent dual-rank configuration.  Eventually we
      // might want to make this explicit in the BIB, but for now we're
      // just going to interpret the MAX_MEM_SPEED item as containing
      // dual-rank values, and derate them for quad-rank parts.  As a
      // special case, if the BIB says that dual-rank DIMMs only run at
      // 800, we just disallow quad-ranks.  This supports 4-socket configs,
      // where 4 dual-rank DIMMs will run at 800 but 4 quad-rank DIMMs
      // don't work at all.  This may need to be tweaked if it turns out
      // that there are configs where both types of DIMMs run at 800.
      //
      if (table_hz <= 800000000)
      {
        boot_printf("msh%d: too many quad-rank DIMMs, shim ignored\n", portnum);
        return 0;
      }
      else if (table_hz <= 1067000000)
        table_hz = 800000000;
      else if (table_hz <= 1333000000)
        table_hz = 1066666666;
      else if (table_hz <= 1600000000)
        table_hz = 1333333333;
    }

    freq_tps = min(freq_tps, table_hz);
  }

  //
  // Clip memory speed to the default, or the user-requested value, if any.
  //
  if (!speed)
    speed = (is_a3_or_later || is_gx72) ? 2133
                                        : is_a2_or_later ? 1333
                                                         : 1066;
  if (speed && freq_tps > speed * 1000 * 1000)
    freq_tps = speed * 1000 * 1000;

  //
  // Finally, clip memory speed to the maximum supported by the current
  // version of the configuration code.  At this point, BUBs are limited to
  // 800 MT/s, and quad-rank DIMMs to 1066 MT/s.

  //
  // As a special case, the BUB won't do over 800 MT/s.
  //
  if (board_flags & BOARD_BRINGUP_BOARD)
    freq_tps = min(freq_tps, 800 * 1000 * 1000);

  DBG("DIMM voltages 0x%x\n", minfo->voltages);
  DBG("DIMM speed %ld t/s\n", freq_tps);

  return freq_tps;
}


void
mshim_config_ddr_voltage(pos_t rshimaddr, uint32_t board_flags)
{
  //
  // We don't need to set DDR voltage on the simulator or FPGA.
  //
  if (sim_is_simulator() || (board_flags & BOARD_FPGA))
    return;

  //
  // Scan through all of the memory shims and find the set of supported
  // voltages.
  //
  dimm_voltage = ~0;

  for (int i = 0; i < MAX_MSHIMS; i++)
    if (minfos[i].voltages)
        dimm_voltage &= minfos[i].voltages;

  //
  // Get the board's memory voltage limits, and clip the set of allowed
  // voltages if needed.
  //
  bi_ptr_t resptr;

  if (bi_getparam(BI_TYPE_MEM_VOLT_RANGE, 0, &resptr, NULL) != BI_NULL)
  {
    struct bi_mem_volt_range* mvr = resptr;
    if (mvr->vmin > 1500000 || mvr->vmax < 1500000)
      dimm_voltage &= ~VOLT_1_5;
    if (mvr->vmin > 1350000 || mvr->vmax < 1350000)
      dimm_voltage &= ~VOLT_1_35;
    if (mvr->vmin > 1200000 || mvr->vmax < 1200000)
      dimm_voltage &= ~VOLT_1_2;
  }

  //
  // Pick just one voltage, and then set the VID pins to use it.  For now,
  // we pick the highest supported voltage for performance and stability
  // reasons.  We may eventually want an option to pick lower voltages to
  // save power, maybe in the BIB, or even make that the default.
  //
  if (dimm_voltage & VOLT_1_5)
  {
    DBG("DIMM voltage 1.5 V\n");
    dimm_voltage = VOLT_1_5;
    cfg_wr(rshimaddr.word, 0, RSH_VID_DDR, VID_1_5);
  }
  else if (dimm_voltage & VOLT_1_35)
  {
    DBG("DIMM voltage 1.35 V\n");
    dimm_voltage = VOLT_1_35;
    cfg_wr(rshimaddr.word, 0, RSH_VID_DDR, VID_1_35);
  }
  else if (dimm_voltage & VOLT_1_2)
  {
    DBG("DIMM voltage 1.2 V\n");
    dimm_voltage = VOLT_1_2;
    cfg_wr(rshimaddr.word, 0, RSH_VID_DDR, VID_1_2);
  }
  else
  {
    boot_printf("boot_panic: no common voltage for all memory shims\n");
    boot_error(BOOT_ERR_MSH_CFG);
  }

  //
  // Wait for the voltage change to settle.
  //
  // FIXME: Should the settle time be a board parameter of some sort?  Or
  // should we just use the algorithm we use when slewing the core voltage?
  //
  const int vddr_settle_time = 50000;
  drv_udelay(vddr_settle_time);
}


int64_t
mshim_config_shim(pos_t shimaddr, pos_t rshimaddr, uint32_t board_flags,
                  /* int */ long speed)
{
  //
  // If we're on the simulator or FPGA, call those routines instead.
  //
  if (sim_is_simulator())
    return mshim_config_shim_sim(shimaddr, rshimaddr, board_flags, speed);

  if (board_flags & BOARD_FPGA)
    return mshim_config_shim_fpga(shimaddr, rshimaddr, board_flags, speed);

#if defined(MSH_DEBUG) || defined(MSH_TIMING)
  cfg_start_uptime = get_uptime();
#endif

  if (speed == 0)
    return 0;

  //
  // Get shim port number, which determines which minfo struct to use,
  // and is also used for messages.
  //
  MSH_DEV_INFO_t inforeg = { .word = cfg_rd(shimaddr.word, 0, MSH_DEV_INFO) };
  int portnum = inforeg.instance;

  minfo = &minfos[portnum];

  DBG("\nConfiguring mshim %d\n", portnum);

  //
  // Calculate frequency.  Note that this is the memory clock speed, which
  // is half of the memory data rate (transactions per second), and also
  // half of the mshim's internal clock speed (since the mshim, internally,
  // does not do DDR signaling).  When we set mshim PLLs, etc., we will
  // multiply this by two.
  //
  long freq_hz = speed / 2;

  DBG("Final dimm freq %ld Hz\n", freq_hz);

  //
  // Now that we know the frequency, calculate any DQ offset.
  //
  if (minfo->dqs_offset_ps)
  {
    target_dq_offset = ((minfo->dqs_offset_ps * freq_hz * 64) +
                        500000000000L) / 1000000000000;
    DBG("Target DQ offset %d\n", target_dq_offset);
  }


  //
  // Calculate final period and spin up the clocks.
  //
  tCK_fs = SCALE_FS / freq_hz;
  config_clocks(shimaddr, freq_hz);

#ifndef HOLD_RX_DLL_IN_RESET
  //
  // Right now, we only enable the RX DLL on A2 parts with no DQS skew.
  //
  rx_dll_enabled = is_a2_or_later && !minfo->dqs_offset_ps;
#endif

  if (is_a2_or_later)
  {
    DBG("Chip is A2 or later\n");

    int is_1066 = (freq_hz >= 500000000 && freq_hz < 550000000);

    dqs_enc_table = is_1066 ? dqs_enc_table_a2_1066 : dqs_enc_table_default;
    dqs_avoid_table = dqs_avoid_table_a2;
    dq_avoid_table = dq_avoid_table_a2;
  }

  if (is_a3_or_later)
    DBG("Chip is A3 or later\n");

  //
  // On boards with more modern chips, that have no board-induced DQ/DQS
  // skew, we tweak the DQ and DQS delays slightly at high speeds to give
  // us more setup time on DQ relative to DQS.
  //
  if ((is_gx72 || is_a3_or_later) && minfo->dqs_offset_ps == 0 &&
      freq_hz > 700 * 1000 * 1000)
  {
    minfo->dqs_tweak = 1;
    minfo->dq_tweak = -4;
  }

  if (!begin_config(shimaddr, minfo->addr_mirror, portnum, freq_hz))
    return 0;

  config_pads(shimaddr, portnum, freq_hz);

  //
  // Bring the DDR interface out of reset and initialize the RAMs.
  //
  reset_tx_acc_dll(shimaddr);
  pulse_ptr_sync(shimaddr);

  //
  // Clear any pending interrupts.
  //
  (void) cfg_rd(shimaddr.word, 0, MSH_INT_VEC0_RTC);

  start_init(shimaddr, portnum, freq_hz);

  DBG("Mshim init is done\n");

  //
  // Exit if any command/address parity errors seen.
  //
  if (any_errors(shimaddr) < 0)
  {
    boot_printf("msh%d: address/command parity errors during init, "
                "shim ignored\n", portnum);
    cfg_wr(shimaddr.word, 0, MSH_BASELINE_CTL, 0);
    return -1;
  }

  init_bist_timing();

  //
  // Determine various delay parameters; if that fails, we can't use this shim.
  //
  if (determine_delays(shimaddr, portnum, 0, freq_hz))
  {
    cfg_wr(shimaddr.word, 0, MSH_BASELINE_CTL, 0);
    return -1;
  }

#ifdef MSH_DEBUG
  boot_printf("Configuration delay values:\n");
  boot_printf("%5s %10s %10s %10s %10s %10s %10s %7s\n",
              "", "tx log", "", "", "",
              "rx log", "rx log", "rx bit");
  boot_printf("%5s %10s %10s %10s %10s %10s %10s %7s\n",
              "lane", "dqs dly", "dq off", "phase", "fine",
              "dqsp dly", "dqsn dly", "delay");
  for (int lane = 0; lane < num_config_lanes(); lane++)
  {
    boot_printf("%5d %10d %10d %10d %10d %10d %10d %7d\n", lane,
                tx_dqs_delays[lane], tx_dq_delay_offsets[lane],
                rx_gate_phase_delays[lane], rx_gate_fine_delays[lane],
                rx_dqsp_delays[lane], rx_dqsn_delays[lane],
                rx_bit_delays[lane]);
  }

#if 0
  //
  // If enabled, dump our configuration data in a way that makes it easy
  // to feed back into the BTK.
  //
  boot_printf("msh[%d].clkgen_pll_freq=%ld\n", portnum, 2 * freq_hz);
  boot_printf("msh[%d].verbose=True\n", portnum);

  boot_printf("msh[%d].bypass_write_leveling=True\n", portnum);

  boot_printf("msh[%d].tx_dqs_delays=[", portnum);
  for (int lane = 0; lane < num_config_lanes(); lane++)
    boot_printf("%d,", tx_dqs_delays[lane]);
  boot_printf("]\n");

  boot_printf("msh[%d].tx_dq_delay_offsets=[", portnum);
  for (int lane = 0; lane < num_config_lanes(); lane++)
    boot_printf("%d,", tx_dq_delay_offsets[lane]);
  boot_printf("]\n");

  boot_printf("msh[%d].bypass_gate_leveling=True\n", portnum);

  boot_printf("msh[%d].rx_gate_phase_delays=[", portnum);
  for (int lane = 0; lane < num_config_lanes(); lane++)
    boot_printf("%d,", rx_gate_phase_delays[lane]);
  boot_printf("]\n");

  boot_printf("msh[%d].rx_fine_delays=[", portnum);
  for (int lane = 0; lane < num_config_lanes(); lane++)
    boot_printf("%d,", rx_gate_fine_delays[lane]);
  boot_printf("]\n");

  if (rx_dll_enabled)
  {
    boot_printf("msh[%d].bypass_read_leveling=True\n", portnum);
    boot_printf("msh[%d].rx_dqsn_delays=[", portnum);
    for (int lane = 0; lane < num_config_lanes(); lane++)
      boot_printf("%d,", rx_dqsn_delays[lane]);
    boot_printf("]\n");
    boot_printf("msh[%d].rx_dqsp_delays=[", portnum);
    for (int lane = 0; lane < num_config_lanes(); lane++)
      boot_printf("%d,", rx_dqsp_delays[lane]);
    boot_printf("]\n");
  }
#endif

#endif

  complete_config(shimaddr);

  //
  // Test the DDR3 RAM attached to this shim.
  //
  int64_t size = minfo->capacity_bytes;
  int post_failed = post_ram_quick(size, shimaddr);
  if (post_failed)
  {
    boot_error(POST_ERR_QUICK_DRAM);
  }

  //
  // If we couldn't successfully read or write _anything_, or if the POST
  // test of the RAM failed, disable the shim and return -1.
  //
  if (size < 1L << MSH_MIN_SIZE_SHIFT || post_failed)
  {
    cfg_wr(shimaddr.word, 0, MSH_BASELINE_CTL, 0);
    return -1;
  }

#if 0
  //
  // This is sometimes useful for debugging memory shim configuration code.
  //
  switch (portnum)
  {
  case 0:
    // Modify size here based on the shim...
  case 1:
  case 2:
  case 3:
  }
#endif

#if defined(MSH_DEBUG) || defined(MSH_TIMING)
  int config_ms = (1000 * (get_uptime() - cfg_start_uptime)) / REFCLK;
  boot_printf("Memory config for msh%d took %d.%03ds\n", portnum,
              config_ms / 1000, config_ms % 1000);
  fini_bist_timing();
#endif

  //
  // The shim is OK, so return its size.  Note that our caller will
  // configure the shim's address range register.
  //
  return size;
}


/** Do the next phase of an mshim zero operation.
 * @param shimaddr Address of the memory shim.
 * @param state State structure.
 * @return 1 if we're done, 0 if not.
 */
static int
mshim_zero_next(pos_t shimaddr, struct mshim_zero_state* state)
{
  //
  // Adjust our state based on the BIST operation that just finished.
  //
  state->start += state->cur_bytes;
  state->rem_bytes -= state->cur_bytes;

  //
  // If there's nothing left to do, say so.
  //
  if (state->rem_bytes == 0)
  {
    //
    // Clear any ECC interrupts we might have gotten during zeroing; don't
    // think we will, but doesn't hurt to be sure.
    //
    (void) cfg_rd(shimaddr.word, 0, MSH_INT_VEC0_RTC);
    return 1;
  }

  //
  // The BIST engine can only handle lengths that are powers of two, so we
  // just use the low bit of the remaining length on each pass.
  //
  state->cur_bytes = 1L << __builtin_ctzl(state->rem_bytes);

  //
  // Configure & start the BIST engine.
  //
  bist_setup(shimaddr, 1, 3, 0, 0, 0, 7, 0, 0, state->start >> 6,
             (state->start + state->cur_bytes - 1) >> 6, ~0L, 0);
  DBG("Zeroing %lld MB starting at 0x%llx\n", state->cur_bytes >> 20,
      state->start);
  cfg_wr(shimaddr.word, 0, MSH_BIST_RUN, 1);

  return 0;
}


void
mshim_zero_start(pos_t shimaddr, int64_t bytes, struct mshim_zero_state* state)
{
  //
  // Set up our state as if we just finished a zero-length step, then call
  // the next routine to kick off the first one.
  //
  state->start = 0;
  state->cur_bytes = 0;
  state->rem_bytes = bytes;
  mshim_zero_next(shimaddr, state);
}


int
mshim_zero_done(pos_t shimaddr, struct mshim_zero_state* state)
{
  if (cfg_rd(shimaddr.word, 0, MSH_BIST_RUN) == 0)
    return 0;

  return mshim_zero_next(shimaddr, state);
}
