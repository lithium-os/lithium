ENTRY(_start)

/*
 * LithiumOS linker script
 *
 * (x86_64) higher-half
 *
 * Script partially edited by AI
 */

PHDRS
{
  text   PT_LOAD FLAGS(5);  /* R X */
  rodata PT_LOAD FLAGS(4);  /* R   */
  data   PT_LOAD FLAGS(6);  /* R W */
}

SECTIONS
{
  . = 0xFFFFFFFF80000000;

  .limine_requests : ALIGN(16)
  {
    KEEP(*(.limine_requests))
  } :rodata

  . = ALIGN(0x1000);
  .text : ALIGN(0x1000)
  {
    *(.text .text.*)
  } :text

  . = ALIGN(0x1000);
  .rodata : ALIGN(0x1000)
  {
    *(.rodata .rodata.*)
  } :rodata

  . = ALIGN(0x1000);
  .data : ALIGN(0x1000)
  {
    *(.data .data.*)
  } :data

  .bss : ALIGN(0x1000)
  {
    *(COMMON)
    *(.bss .bss.*)
  } :data
}
