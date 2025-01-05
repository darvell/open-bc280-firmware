/* Open firmware for Aventon BC280-class displays.
 * Link as an application image located at 0x08010000 so the stock bootloader
 * (validate_firmware_header + jump_to_application_firmware) will accept and jump.
 */

ENTRY(Reset_Handler)

MEMORY
{
  /* 256KB total flash, 64KB bootloader => 192KB app region */
  FLASH (rx) : ORIGIN = 0x08010000, LENGTH = 0x00030000
  /* Bootloader masks SP with 0x2FFE0000 and expects 0x20000000.
     Keep initial SP below 0x20020000 to satisfy the check. */
  SRAM  (rwx): ORIGIN = 0x20000000, LENGTH = 0x00018000
}

/* Place stack within the default 96 KiB SRAM window so mask passes. */
_stack_top = ORIGIN(SRAM) + LENGTH(SRAM) - 0x10;
__openfw_image_start = ORIGIN(FLASH);

SECTIONS
{
  .isr_vector ORIGIN(FLASH) :
  {
    KEEP(*(.isr_vector))
  } > FLASH

  .text :
  {
    *(.text*)
    *(.rodata*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame*)
  } > FLASH

  .ARM.exidx :
  {
    *(.ARM.exidx*)
  } > FLASH

  /* Align load address for .data to avoid overlap with .ARM.exidx */
  . = ALIGN(4);

  .data : AT (ALIGN(ADDR(.text) + SIZEOF(.text) + SIZEOF(.ARM.exidx), 4))
  {
    _sdata = .;
    *(.data*)
    _edata = .;
  } > SRAM

  _sidata = LOADADDR(.data);

  __openfw_meta_start = (LOADADDR(.data) + SIZEOF(.data) + 3) & ~3;
  .openfw_meta __openfw_meta_start :
  {
    KEEP(*(.openfw_meta))
  } > FLASH
  __openfw_meta_end = .;

  .bss (NOLOAD) :
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    _ebss = .;
  } > SRAM

  /DISCARD/ :
  {
    *(.comment)
    *(.note*)
  }
}
