/*
 * Linker script for the level-1 booter.
 * Tokens of the form @XXX@ are replaced during the build process, see the
 * Makefile rules.
 */
OUTPUT_ARCH(tile)
ENTRY(_start)
PHDRS
{
  /*
   * We place everything in the text segment, since we have no memory
   * protection in the booter anyway.
   */
  text PT_LOAD ;
}
SECTIONS
{
  .text    @BASE@:
	{ *(.text .text.*) } :text =0
  .data :
  {
    *(.rodata .rodata.* .data .data.*);
    . = ALIGN(8);
    PROVIDE (srom_table = .);
    KEEP(*(.srom_table))
    PROVIDE (srom_table_end = .);
  }
  . = ALIGN(64);
  .bss                :
	{ *(.bss .bss.*) . = ALIGN(16); }
  . = ALIGN(16);

  PROVIDE (_stext = ADDR(.text));
  PROVIDE (_etext = ADDR(.text) + SIZEOF(.text));
  PROVIDE (_sdata = ADDR(.data));
  PROVIDE (_edata = ADDR(.data) + SIZEOF(.data));
  PROVIDE (_sbss = ADDR(.bss));
  PROVIDE (_ebss = ADDR(.bss) + SIZEOF(.bss));
  PROVIDE (_lh_start = 0x@LHSTART@);
  PROVIDE (_lh_end = 0x@LHEND@);

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
