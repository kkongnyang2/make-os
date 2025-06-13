/* link.ld — QEMU virt 머신용, 커널을 0x00080000에 배치 */
ENTRY(_start)

SECTIONS
{
  . = 0x00080000;        /* -kernel 로드 주소 */

  .text   : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data   : { *(.data*)  }
  .bss    : { *(.bss*)   }

  /DISCARD/ : { *(.note.*) *(.comment.*) }
}
