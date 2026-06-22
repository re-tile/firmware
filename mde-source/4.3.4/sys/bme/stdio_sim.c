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
 * Output of characters via magic simulator SPRs.
 */

#include <stdio.h>

#include <arch/sim.h>

/** Write to the simulator console.
 * @param s String to write.
 * @param len Number of characters to write.
 * @return Number of characters written.
 */
static int
_bme_sim_write(char* s, int len, unsigned int offset, void* private)
{
  for (int i = 0; i < len; i++)
  {
    __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_PUTC |
                 (s[i] << _SIM_CONTROL_OPERATOR_BITS));
  }                 
  if (len)
    __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_PUTC |
                 (SIM_PUTC_FLUSH_STRING << _SIM_CONTROL_OPERATOR_BITS));

  return (len);
}


/** Simulator file operations vector. */
static struct _file_ops _bme_sim_fops =
{
  .write = _bme_sim_write,
};

/** Buffer for simulator output file. */
static char _bme_sim_outbuf[256];

/** Simulator output file. */
FILE bme_stdout_sim =
{
  .buf = _bme_sim_outbuf,
  .len = sizeof (_bme_sim_outbuf),
  .ptr = _bme_sim_outbuf,
  .wrem = sizeof (_bme_sim_outbuf),
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_W,
  .ops = &_bme_sim_fops
};
