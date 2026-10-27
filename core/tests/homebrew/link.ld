/* Homebrew XEX layout: 64K-aligned base 0x82000000, first 0x1000 bytes are
 * reserved for the PE headers written by mkxex. */
ENTRY(fsr_selftest)
SECTIONS
{
  . = 0x82001000;
  .text : { *(.text.thunks) *(.text .text.*) }
  . = ALIGN(0x1000);
  .rodata : { *(.rodata .rodata.* .rodata1) }
  . = ALIGN(0x1000);
  .data : { *(.data .data.* .sdata .sdata.*) }
  .bss : { *(.sbss .sbss.* .bss .bss.* COMMON) }
  /DISCARD/ : { *(.comment) *(.note .note.*) *(.eh_frame .eh_frame_hdr) }
}
