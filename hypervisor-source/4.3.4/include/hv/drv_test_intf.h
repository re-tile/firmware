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
 * Interface definitions for the test driver.
 */

#ifndef _SYS_HV_DRV_TEST_INTF_H
#define _SYS_HV_DRV_TEST_INTF_H


/* Offsets within control device */

/** Read and write buffer */
#define TEST_CTL_READWRITE_OFF   0

/** Write buffer, but increment bytes by one */
#define TEST_CTL_PLUSONE_OFF     0x1000

/** Write buffer, but decrement bytes by one; uses driver messaging and
 *  second dedicated tile */
#define TEST_CTL_MINUSONE_OFF    0x2000

/** Write to cause instant interrupt; read to get value of last one and
 *  clear it */
#define TEST_CTL_INSTINTR_OFF    0x10000

/** Write to cause delayed interrupt; read to get value of last one and
 *  clear it */
#define TEST_CTL_DELAYINTR_OFF   0x20000

/** Read to get address of the test_shared_data struct */
#define TEST_CTL_SHAREDATA_OFF   0x30000

/** Read to get index of first fastio routine */
#define TEST_CTL_FASTIO_IDX_OFF  0x40000

/** Read to get value saved by first fastio routine */
#define TEST_CTL_FASTIO_A_OFF    0x50000

/** Read to get index of first one-shot/level-sensitive interrupt */
#define TEST_CTL_INTRNUM_OFF     0x60000

/** Write a struct test_mmio_op to cause MMIO permit operation to occur */
#define TEST_CTL_MMIO_P_OFF      0x70000

/** Write a struct test_mmio_op to cause MMIO deny operation to occur */
#define TEST_CTL_MMIO_D_OFF      0x80000

/** Write a struct test_mmio_op to cause MMIO okay operation to occur */
#define TEST_CTL_MMIO_O_OFF      0x90000

/** Write a struct test_mmio_op to cause MMIO okay operation to occur on
 *  a remote tile */
#define TEST_CTL_MMIO_OR_OFF     0xa0000

/** Read and write the shared buffer on the local tile */
#define TEST_CTL_RW_SHR_LOC_OFF  0x100000

/** Read and write the shared buffer on the shared tile */
#define TEST_CTL_RW_SHR_SHR_OFF  0x200000

/** Data for TEST_CTL_MMIO_?_OFF */
struct test_mmio_op
{
  unsigned long pa;     /**< Physical address offset */
  unsigned long len;    /**< Length of region */
  unsigned int shim_x;  /**< I/O shim X coordinate */
  unsigned int shim_y;  /**< I/O shim Y coordinate */
};

/** Data in the client-shared memory region */
struct test_shared_data
{
  uint32_t last_instant_intr;   /**< Last value received by the instant
                                     interrupt routine */
  uint32_t last_delayed_intr;   /**< Last value received by the delayed
                                     interrupt routine */
};

#define TEST_INTR_CHAN_WIDTH     7        /**< Number of bits of intchan offset
                                               included in writes to
                                               INSTINTR_OFF/DELAYINTR_OFF */

#define TEST_INTCHAN_MSGINTR          0  /**< Chan for message interrupts */
#define TEST_INTCHAN_LVLINTR_RAISE    1  /**< Chan for raising dev intrs */
#define TEST_INTCHAN_LVLINTR_LOWER    2  /**< Chan for lowering dev intrs */
#define TEST_INTCHAN_NUM              3  /**< Number of intchans */

/** Number of registers in the iorpc test driver. */
#define TEST_IORPC_NUM_REGS 4

/** Data structure used by the iorpc test driver. */
struct test_iorpc_regs
{
  unsigned long regs[TEST_IORPC_NUM_REGS];  /**< Storage modified via iorpc calls. */
};

/** A really bug structure used by the iorpc test driver. */
struct test_data_array
{
  unsigned char data[2048];      /**< A big block of bytes. */
};

#endif /* _SYS_HV_DRV_TEST_INTF_H */
