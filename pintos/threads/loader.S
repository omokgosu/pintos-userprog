/* 이 파일은 MIT의 6.828 과정에서 사용된 소스 코드에서 파생되었습니다.
   원본 저작권 고지는 아래에 전체적으로 재현되어 있습니다. */

/*
 * Copyright (C) 1997 Massachusetts Institute of Technology 
 *
 * This software is being provided by the copyright holders under the
 * following license. By obtaining, using and/or copying this software,
 * you agree that you have read, understood, and will comply with the
 * following terms and conditions:
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose and without fee or royalty is
 * hereby granted, provided that the full text of this NOTICE appears on
 * ALL copies of the software and documentation or portions thereof,
 * including modifications, that you make.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS," AND COPYRIGHT HOLDERS MAKE NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED. BY WAY OF EXAMPLE,
 * BUT NOT LIMITATION, COPYRIGHT HOLDERS MAKE NO REPRESENTATIONS OR
 * WARRANTIES OF MERCHANTABILITY OR FITNESS FOR ANY PARTICULAR PURPOSE OR
 * THAT THE USE OF THE SOFTWARE OR DOCUMENTATION WILL NOT INFRINGE ANY
 * THIRD PARTY PATENTS, COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS. COPYRIGHT
 * HOLDERS WILL BEAR NO LIABILITY FOR ANY USE OF THIS SOFTWARE OR
 * DOCUMENTATION.
 *
 * The name and trademarks of copyright holders may NOT be used in
 * advertising or publicity pertaining to the software without specific,
 * written prior permission. Title to copyright in this software and any
 * associated documentation will at all times remain with copyright
 * holders. See the file AUTHORS which should have accompanied this software
 * for a list of all copyright holders.
 *
 * This file may be derived from previously copyrighted software. This
 * copyright applies only to those changes made by the copyright
 * holders listed in the AUTHORS file. The rest of this file is covered by
 * the copyright notices, if any, listed below.
 */

#include "threads/loader.h"
	
#### 커널 로더

#### 이 코드는 하드디스크의 첫 번째 섹터에 저장되어야 합니다.
#### BIOS가 실행되면, 이 코드를 물리 주소 0x7c00-0x7e00 (512바이트)에 로드합니다.
#### 그런 다음 실제 모드에서 이 코드의 시작점으로 점프합니다.
#### 이 코드는 보호 모드(32비트 모드)로 전환하여 모든 메모리에 접근할 수 있게 하고,
#### 커널을 메모리에 로드한 후 start.S가 링크된 커널의 첫 번째 바이트로 점프합니다.
	
/* 제어 레지스터 0의 플래그들 */
#define CR0_PE 0x00000001      /* 보호 모드 활성화 */
#define CR0_EM 0x00000004      /* (부동소수점) 에뮬레이션 */
#define CR0_PG 0x80000000      /* 페이징 */
#define CR0_WP 0x00010000      /* 커널 모드에서 쓰기 보호 활성화 */



.globl start
start:
	
# 코드는 16비트 세그먼트인 실제 모드에서 실행됩니다.
	.code16

# 인터럽트를 비활성화합니다. 보호 모드에서 훨씬 나중까지
# 인터럽트를 처리할 준비가 되지 않기 때문입니다.
# 문자열 명령어는 위쪽으로 진행됩니다 (예: 아래의 "rep stosl").

	cli
	cld

# 데이터 세그먼트를 설정합니다.

	subw %ax, %ax
	movw %ax, %es
	movw %ax, %ds

# 스택 세그먼트를 설정합니다.
# 스택은 우리로부터 시작하여 아래쪽으로 증가합니다.
# 우리는 스택을 사용하지 않지만, BIOS를 호출하는데
# BIOS가 스택을 사용할 수 있습니다.


	movw %ax, %ss
	movw $0x7c00, %sp
	
#### Enable A20.  Address line 20 is tied to low when the machine
#### boots, which prevents addressing memory about 1 MB.  This code
#### fixes it.
	
# 상태 레지스터가 바쁜 동안 폴링합니다.

1:	inb $0x64, %al
	testb $0x2, %al
	jnz 1b

# 출력 포트 쓰기 명령을 보냅니다.


	movb $0xd1, %al
	outb %al, $0x64

# 상태 레지스터가 바쁜 동안 폴링합니다.

1:	inb $0x64, %al
	testb $0x2, %al
	jnz 1b

# Enable A20 line.

	movb $0xdf, %al
	outb %al, $0x60

#### 인터럽트 15h 기능 e820h를 통해 메모리 크기를 얻습니다.
#### 이제 우리의 pintos는 x86_64에서 실행되므로, "확장 메모리 크기 얻기"
#### 작업은 우리의 목적에 충분하지 않습니다.
	mov $0xe820, %eax  # command
	mov $(E820_MAP4), %edi  # dst
	xor %ebx, %ebx
	mov $0x534d4150, %edx # magic
	mov $24, %ecx
	int $0x15
	cmp %eax, %edx
	test %ebx, %ebx
	je panic
	mov $24, %ebp

parse_e820:
	mov %ecx, -4(%edi)
	add $24, %edi
	mov $0xe820, %eax
	mov $24, %ecx
	int $0x15
	jc e820_parse_done
	add $24, %ebp
	test %ebx, %ebx
	jne parse_e820

e820_parse_done:
	mov %ecx, -4(%edi)
	movl $0x40, MULTIBOOT_FLAG
	movl %ebp, MULTIBOOT_MMAP_LEN
	movl $(E820_MAP), MULTIBOOT_MMAP_ADDR # e820

#### 보호 모드로 전환합니다.

# 인터럽트는 여전히 비활성화되어 있습니다.

# GDTR을 우리의 GDT로 가리킵니다. 보호 모드에는 GDT가 필요합니다.
# GDT 디스크립터의 모든 32비트가 로드되도록 하기 위해 data32 접두사가 필요합니다
# (기본값은 24비트만 로드).

	data32 lgdt gdtdesc

# 그런 다음 CR0에서 다음 비트들을 켭니다:
#    PE (보호 모드 활성화): 보호 모드를 켭니다.
#    PG (페이징): 페이징을 켭니다.
#    WP (쓰기 보호): 설정되지 않으면, 링 0 코드가 페이지 테이블의
#       쓰기 보호 비트를 무시합니다 (!).
#    EM (에뮬레이션): 부동소수점 명령어를 트랩하도록 강제합니다.
#       우리는 부동소수점을 지원하지 않습니다.
	
	movl %cr0, %eax
	orl $CR0_PE, %eax
	movl %eax, %cr0
	
# 이제 16비트 세그먼트에서 보호 모드에 있습니다. CPU는 여전히
# %cs의 세그먼트 디스크립터에 실제 모드 코드 세그먼트를 캐시하고 있습니다.
# %cs를 다시 로드해야 하며, 가장 쉬운 방법은 far jump를 사용하는 것입니다.
# 32비트 세그먼트에 있지 않기 때문에 32비트 오프셋으로 점프하려면
# data32 접두사가 필요합니다.

	data32 ljmp $SEL_KCSEG, $protcseg

# 이제 32비트 세그먼트에서 보호 모드에 있습니다.

	.code32

# 다른 모든 세그먼트 레지스터와 스택 포인터를 다시 로드하여
# 새로운 GDT를 가리키도록 합니다.

protcseg:
	movw $SEL_KDSEG, %ax
	movw %ax, %ds		
	movw %ax, %es		
	movw %ax, %fs		
	movw %ax, %gs		
	movw %ax, %ss
	movl $LOADER_PHYS_BASE + 0x30000, %esp

#### IDE 컨트롤러를 직접 조작하여 물리 주소 LOADER_PHYS_BASE에서
#### 시작하는 커널을 로드합니다.

	movl $1, %ebx
	movl $LOADER_PHYS_BASE, %edi

# 데이터를 폴링할 것이므로 IDE 컨트롤러의 인터럽트 전달을 비활성화합니다.
# (이를 하지 않으면, 부팅 중에 인터럽트 컨트롤러를 리셋한 후에도
# Bochs 2.2.6이 나중에 IDE 인터럽트를 전달하지 않습니다.
# IDE 컨트롤러도 리셋했더라도 마찬가지입니다.)

	movw $0x3f6, %dx
	movb $0x02, %al
	outb %al, %dx
	
read_sector:

# 컨트롤러가 바쁜 동안 상태 레지스터를 폴링합니다.

	movl $0x1f7, %edx
1:	inb %dx, %al
	testb $0x80, %al
	jnz 1b

# 단일 섹터를 읽습니다.

	movl $0x1f2, %edx
	movb $1, %al
	outb %al, %dx

# 하위 28비트에 쓸 섹터 번호.
# 상위 4비트에 LBA 모드, 장치 0.

	movl %ebx, %eax
	andl $0x0fffffff, %eax
	orl $0xe0000000, %eax

# %eax를 포트 0x1f3...0x1f6에 덤프합니다.

	movl $4, %ecx
1:	incw %dx
	outb %al, %dx
	shrl $8, %eax
	loop 1b

# 명령 레지스터에 READ 명령.

	incw %dx
	movb $0x20, %al
	outb %al, %dx

# 컨트롤러가 바쁜 동안 상태 레지스터를 폴링합니다.

1:	inb %dx, %al
	testb $0x80, %al
	jnz 1b

# 데이터가 준비될 때까지 상태 레지스터를 폴링합니다.

1:	inb %dx, %al
	testb $0x08, %al
	jz 1b

# 섹터를 전송합니다.

	movl $256, %ecx
	movl $0x1f0, %edx
	rep insw

# 다음 섹터.

	incl %ebx
	cmpl $KERNEL_LOAD_PAGES*8 + 1, %ebx
	jnz read_sector

#### Jump to kernel entry point.
	movl $LOADER_PHYS_BASE, %eax
	call *%eax
	jmp panic

#### GDT

gdt:
	.quad 0x0000000000000000	# null seg
	.quad 0x00cf9a000000ffff	# code seg
	.quad 0x00cf92000000ffff  # data seg
	
gdtdesc:
	.word	0x17			# sizeof (gdt) - 1
	.long	gdt

#### Fatal error.
#### Print panic_message (with help from the BIOS) and spin.

panic:  .code16			# We only panic in real mode.
	movw $panic_message, %si
	movb $0xe, %ah
	subb %bh, %bh
1:	lodsb
	test %al, %al
2:	jz 2b			# Spin.
	int $0x10
	jmp 1b

panic_message:
	.ascii "Panic!"
	.byte 0

#### Command-line arguments and their count.
#### This is written by the `pintos' utility and read by the kernel.
#### The loader itself does not do anything with the command line.
	.org LOADER_ARG_CNT - LOADER_BASE
arg_cnt:
	.long 0
	.org LOADER_ARGS - LOADER_BASE
args:
	.fill 0x80, 1, 0

#### Boot-sector signature.
#### The BIOS checks that this is set properly.
	.org LOADER_SIG - LOADER_BASE
	.word 0xaa55
