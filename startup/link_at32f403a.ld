/**
 * Linker script for AT32F403ARGT7 (open-firmware as bootloader app)
 *
 * AT32F403ARGT7 memory:
 * - Flash: 1024KB (0x100000) at 0x08000000
 * - SRAM:  96KB (0x18000) at 0x20000000
 *
 * Layout as app under OEM bootloader:
 * - Bootloader: 0x08000000 - 0x0800FFFF (64KB)
 * - App (us):   0x08010000 - 0x080FFFFF (960KB)
 *
 * Bootloader validates firmware header at 0x08010000 and jumps.
 * Initial SP must satisfy bootloader mask check (SP & 0x2FFE0000 == 0x20000000).
 */

ENTRY(Reset_Handler)

MEMORY
{
  /* App region: 960KB after 64KB bootloader */
  FLASH (rx)  : ORIGIN = 0x08010000, LENGTH = 0x000F0000
  /* Full 96KB SRAM - stack at top satisfies bootloader SP mask check */
  SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 0x00018000
}

/* Place stack at end of SRAM, satisfies bootloader mask */
_stack_top = ORIGIN(SRAM) + LENGTH(SRAM);
__openfw_image_start = ORIGIN(FLASH);

SECTIONS
{
  /* Vector table at start of app flash region */
  .isr_vector ORIGIN(FLASH) :
  {
    KEEP(*(.isr_vector))
  } > FLASH

  /* Code and read-only data */
  .text :
  {
    *(.text*)
    *(.rodata*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame*)

    /* ARM exception unwinding (not used, but needed for linking) */
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > FLASH

  /* Ensure data section is aligned */
  . = ALIGN(4);
  _etext = .;

  /* Initialized data - stored in flash, copied to SRAM at startup */
  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > SRAM AT> FLASH

  /* Open-firmware metadata section (version info, etc) */
  __openfw_meta_start = (LOADADDR(.data) + SIZEOF(.data) + 3) & ~3;
  .openfw_meta __openfw_meta_start :
  {
    KEEP(*(.openfw_meta))
  } > FLASH
  __openfw_meta_end = .;

  /* Uninitialized data - zeroed at startup */
  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > SRAM

  /* Heap starts after BSS, grows upward toward stack */
  . = ALIGN(4);
  _end = .;
  __heap_start = _end;
  __heap_end = _stack_top - 0x400; /* Reserve 1KB for stack */

  /* Discard debug sections to save space */
  /DISCARD/ :
  {
    *(.comment)
    *(.note*)
    *(.debug*)
  }
}
