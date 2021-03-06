#include "config.h"

ENTRY(start)
OUTPUT_FORMAT(elf32-m68k)
STARTUP(target/coldfire/crt0.o)

#define DRAMSIZE (MEMORYSIZE * 0x100000)

#if defined(IAUDIO_X5) || defined(IAUDIO_M5)
#define IRAMORIG 0x10000000
#define IRAMSIZE 0x20000
#elif defined(IAUDIO_M3)
#define IRAMORIG 0x10000000
#define IRAMSIZE 0x18000
#endif
#define DRAMORIG 0x31000000
#ifdef HAVE_DUALBOOT
#define FLASHORIG 0x00150000
#else
#define FLASHORIG 0x00010000
#endif
#define FLASHSIZE 4M - FLASHORIG

MEMORY
{
   DRAM : ORIGIN = DRAMORIG, LENGTH = DRAMSIZE
   IRAM : ORIGIN = IRAMORIG, LENGTH = IRAMSIZE
   FLASH : ORIGIN = FLASHORIG, LENGTH = FLASHSIZE
}

SECTIONS
{
    .vectors :
    {
        KEEP(*(.startup*));
        _datacopy = .;
    } > FLASH

    .data : AT ( _datacopy )
    {
        _datastart = .;
        KEEP(*(.resetvectors));
        *(.resetvectors);
        KEEP(*(.vectors));
        *(.vectors);
        . = ALIGN(0x200);
        *(.icode)
        *(.irodata)
        *(.idata)
        *(.data*)
        . = ALIGN(0x4);
        _dataend = .;
        . = ALIGN(0x10);  /* Maintain proper alignment for .text section */
    } > IRAM

    /* TRICK ALERT! Newer versions of the linker don't allow output sections
       to overlap even if one of them is empty, so advance the location pointer
       "by hand" */
    .text LOADADDR(.data) + SIZEOF(.data) :
    {
        *(.init.text)
        *(.text*)
        . = ALIGN(0x4);
    } > FLASH

    .rodata :
    {
        *(.rodata*)
        . = ALIGN(0x4);
        _iramcopy = .;
    } > FLASH

    .stack :
    {
       *(.stack)
       _stackbegin = .;
       stackbegin = .;
       . += 0x2000;
       _stackend = .;
       stackend = .;
    } > IRAM

    .bss DRAMORIG+0x800000 (NOLOAD) :
    {
       _edata = .;
        *(.ibss)
        *(.bss*)
        *(COMMON)
       _end = .;
    } > DRAM
}
