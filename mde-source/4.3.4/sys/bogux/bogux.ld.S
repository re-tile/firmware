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
 *
 * Linker script for bogux.
 */

#include <hv/hypervisor.h>

#include "mem_layout.h"

OUTPUT_ARCH(tile)
ENTRY(_start)
PHDRS
{
  intrpt1 PT_LOAD ;
  text PT_LOAD ;
  data PT_LOAD ;
}
SECTIONS
{
  . = MEM_CODE_VA;
  _stext = .;
  .intrpt1 : AT ( ADDR(.intrpt1) - MEM_CODE_PA_ADJUST )
  {
    *(.intrpt1)
  } :intrpt1 =0

  .hvglue (MEM_CODE_VA+HV_GLUE_START_CPA) (NOLOAD) :
  {
    *(.hvglue)
  } :data

  . = ALIGN(65536);
  .text : AT ( ADDR(.text) - MEM_CODE_PA_ADJUST )
  {
    *(.text .text.*)
  } : text =0
  _etext = .;

  . = MEM_DATA_VA;
  _sdata = .;
  .rodata : AT ( ADDR(.rodata) - MEM_DATA_PA_ADJUST )
  {
    *(.rodata .rodata.*)
  } : data
  .data : AT ( ADDR(.data) - MEM_DATA_PA_ADJUST )
  {
    . = ALIGN(CHIP_L2_LINE_SIZE());
    *(.l2aligned)
    *(.data .data.*)
  } :data
  _edata = .;

  . = ALIGN(64);
  _sbss = .;
  .bss : AT ( ADDR(.bss) - MEM_DATA_PA_ADJUST )
  {
     *(.bss .bss.*)
     . = ALIGN(16);
  } :data
  . = ALIGN(16);
  _ebss = .;
  _end = .;

  /* These sections don't get mapped into program headers, so they're not
     actually loaded, though they may take a little space in the binary.
     We put them in the "data" phdr to keep the linker quiet, even though
     in practice since they are discontiguous they are not included.
     Ideally we would be able to mark them as "W" sections not "WA", but
     the compiler doesn't seem to grok creating unallocated sections
     with __attribute__. */

  .tilestate MEM_TILESTATE_VA (NOLOAD) :
  {
    _stilestate = .;
    *(.tilestate.1) *(.tilestate.2) *(.tilestate)
    _etilestate = .;
  } :data

  .locks MEM_LOCKS_VA (NOLOAD) :
  {
    _slocks = .;
    *(.locks .locks.*)
    _elocks = .;
  } :data

  /* Stabs debugging sections.  */
  .stab          0 : { *(.stab) }
  .stabstr       0 : { *(.stabstr) }
  .stab.excl     0 : { *(.stab.excl) }
  .stab.exclstr  0 : { *(.stab.exclstr) }
  .stab.index    0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment       0 : { *(.comment) }
  /* DWARF debug sections.
     Symbols in the DWARF debugging sections are relative to the beginning
     of the section so we begin them at 0.  */
  /* DWARF 1 */
  .debug          0 : { *(.debug) }
  .line           0 : { *(.line) }
  /* GNU DWARF 1 extensions */
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  /* DWARF 1.1 and DWARF 2 */
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  /* DWARF 2 */
  .debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }
  /* SGI/MIPS DWARF 2 extensions */
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames  0 : { *(.debug_varnames) }
  /DISCARD/ : { *(.note.GNU-stack) }
}
