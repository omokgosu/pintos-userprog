#include "threads/loader.h"

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)			/* 커널은 "start" 심볼에서 시작합니다. */

SECTIONS
{
  /* 커널 베이스의 가상 주소를 지정합니다. */
	. = LOADER_KERN_BASE + LOADER_PHYS_BASE;

	PROVIDE(start = .);
  /* 커널은 코드로 시작하고, 그 다음에 읽기 전용 데이터와 쓰기 가능한 데이터가 옵니다. */
	.text : AT(LOADER_PHYS_BASE) {
		*(.entry)
		*(.text .text.* .stub .gnu.linkonce.t.*)
	} = 0x90
	.rodata         : { *(.rodata .rodata.* .gnu.linkonce.r.*) }

	. = ALIGN(0x1000);
	PROVIDE(_end_kernel_text = .);

  .data : { *(.data) *(.data.*)}

  /* BSS(0으로 초기화된 데이터)는 다른 모든 것 다음에 옵니다. */
  PROVIDE(_start_bss = .);
  .bss : { *(.bss) }
  PROVIDE(_end_bss = .);

  PROVIDE(_end = .);

	/DISCARD/ : {
		*(.eh_frame .note.GNU-stack .stab)
	}
}
