/*
 * Tomline on the Streams hardware (STM32F103R8T6).
 *
 * Same flash layout as stmlib/linker_scripts/stm32f10x_flash_md_application.ld
 * (app at 0x08004000, 112 KB). Only the stack changes: the stock script gives
 * it 512 bytes, between .data and .bss. Tomline's main loop does its knob
 * maths in software floating point, and with both interrupts on top the
 * stack went past 512 bytes and overwrote variables (the "freeze when
 * turning knobs"). Here the stack gets 4 KB at the bottom of RAM: if it ever
 * overflowed it would run off the start of RAM instead of silently
 * overwriting variables. Check with tomline/tools/stack_usage.py.
 */

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08004000, LENGTH = 112K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

SECTIONS
{
  .text :
  {
    _start = .;
    KEEP(*(.isr_vector))
    *(.text*)
    *(.rodata*)
    *(.flashtext*)
    KEEP (*(.init))
    KEEP (*(.fini))
    _etext = .;
    _sidata = .;
  } > FLASH

  .stack (NOLOAD) :
  {
    _sstack = .;
    . = . + 4096;
    _estack = .;
  } > RAM

  .data : AT (ADDR(.text) + SIZEOF(.text))
  {
    _sdata = .;
    *(.ramtext*)
    . = ALIGN(4);
    *(.data*)
    _edata = .;
  } > RAM

  .bss :
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    _ebss = .;
  } > RAM
}
