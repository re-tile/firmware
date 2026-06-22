/* Linker script for hypervisor. */
OUTPUT_ARCH(tile)
ENTRY(_start)
PHDRS
{
  text PT_LOAD ;
  data PT_LOAD ;
  shared PT_LOAD ;
  hvgdbtext PT_LOAD ;
}

/*
 * We reserve a set of memory regions for various virtual address uses,
 * as well as one which is used by physical addresses.
 */
MEMORY
{
  /* Virtual memory. */

  /* Text */
  text (rx) :     ORIGIN = 0xFFFFFFFC00000000, LENGTH = 1M

  /* Data */
  data (rw) :     ORIGIN = 0xFFFFFFFC00100000, LENGTH = 1M

  /* Data shared between tiles */
  sdata (rw) :    ORIGIN = 0xFFFFFFFD00000000, LENGTH = 1M

  /* Debugger text */
  dbgtext (rx) :  ORIGIN = 0xFFFFFFFE00000000, LENGTH = 1M

  /*
   * Physical memory; used only for LMAs, via AT>.  Everything which is loaded
   * uses the same physical address range.  This is really just the 0-based
   * offset within the output image; the bits don't necessarily get loaded
   * at that physical address.
   */
  physmem (rwx) : ORIGIN = 0,                  LENGTH = 1M
}

SECTIONS
{
  /*
   * Interrupt vectors.
   */
  .intrpt :
  {
    PROVIDE (_stext = .);
    KEEP(*(.intrpt))
  } >text AT>physmem :text

  PROVIDE (_stext_phys = LOADADDR(.intrpt));

  /*
   * Code.
   */
  .text :
  {
    *(.fasttext .fasttext.*)
    *(.text .text.*)
    PROVIDE (_etext = .);
  } >text AT>physmem :text =0

  PROVIDE (_etext_phys = LOADADDR(.text) + SIZEOF(.text));

  /*
   * Stack area.  We put this at the very start of the data segment, so
   * that if we go off the end of the downward-growing stack, we don't
   * scribble over other data.
   */
  _stack_size = 0xF000;
  _dblflt_stack_size = 0x1000;

  .stacks (NOLOAD) :
  {
    /*
     * Note that _estack and _dblflt_stack are the initial values of normal and
     * double-fault stack pointers, respectively, since the stack grows down.
     */
    _sstack = .;
    . = . + _stack_size;
    _estack = .;

    . = . + _dblflt_stack_size;
    _dblflt_stack = .;
  } >data

  /*
   * Read-only data.  This isn't actually mapped read-only right now, but
   * we keep it a separate section so that we can easily change that later.
   */
  .rodata : ALIGN(64)
  {
    PROVIDE (_sdata = .);
    *(.rodata .rodata.*)
  } >data AT>physmem :data =0

  PROVIDE (_sdata_phys = LOADADDR(.rodata));

  /*
   * Read-write data.
   */
  .data : ALIGN(64)
  {
    *(.data .data.*)
  } >data AT>physmem :data

  /*
   * Temperature sensor table.
   */
  .temp_sensor_table : ALIGN(64)
  {
    PROVIDE (temp_sensor_table_start = .);
    KEEP(*(.temp_sensor_table))
    PROVIDE (temp_sensor_table_end = .);
  } >data AT>physmem :data

  /*
   * Device table.
   */
  .device_table : ALIGN(64)
  {
    PROVIDE (devices = .);
    KEEP(*(.device_table_base))
    KEEP(*(.device_table))
    KEEP(*(.device_table_end))
    PROVIDE (devices_end = .);
  } >data AT>physmem :data

  /*
   * SROM table.
   */
  .srom_table : ALIGN(64)
  {
    PROVIDE (srom_table = .);
    KEEP(*(.srom_table))
    PROVIDE (srom_table_end = .);
  } >data AT>physmem :data

  /*
   * Right before this comment would be a good spot to add any new tables.
   * (Doing it after the driver table below would mean you'd have to
   * change the definitions of _edata and _edata_phys.)
   */

  /*
   * Device driver table.
   */
  .driver_table : ALIGN(64)
  {
    PROVIDE (driver_table_start = .);
    KEEP(*(.driver_table))
    PROVIDE (driver_table_end = .);
    PROVIDE (_edata = .);
  } >data AT>physmem :data

  PROVIDE (_edata_phys = LOADADDR(.driver_table) + SIZEOF(.driver_table));

  /*
   * Uninitialized data.
   */
  .bss (NOLOAD) : ALIGN(64)
  {
    PROVIDE (_sbss = .);
    *(.bss .bss.*)
    *(COMMON)
    PROVIDE (_ebss = .);
  } >data

  /*
   * Data shared between tiles.
   */
  .shared : ALIGN(64)
  {
    PROVIDE (_sshared = .);
    *(.shared .shared.*)
    PROVIDE (_eshared = .);
  } >sdata AT>physmem :shared

  PROVIDE (_sshared_phys = LOADADDR(.shared));
  PROVIDE (_eshared_phys = LOADADDR(.shared) + SIZEOF(.shared));

  /*
   * Debug stub interrupt vector table.  Put physical memory for it contiguous
   * with hv memory.
   */
  .dbg_text : ALIGN(0x10000)
  {
    PROVIDE (_sdbg_text = .);
    KEEP(*(.dbg_text_intvec))
    *(.dbg_text.*)
    PROVIDE (_edbg_text = .);
  } >dbgtext AT>physmem :hvgdbtext

  PROVIDE (_sdbg_text_phys = LOADADDR(.dbg_text));
  PROVIDE (_edbg_text_phys = LOADADDR(.dbg_text) + SIZEOF(.dbg_text));

  _dbg_data = 0xFFFFFFFE00100000;

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
