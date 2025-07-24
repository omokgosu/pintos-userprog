#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "userprog/process.h" // fork() 때문에 추가
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "filesys/file.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

static struct lock filesys_lock;

/* 시스템 콜.
 *
 * 이전에는 시스템 콜 서비스가 인터럽트 핸들러에 의해 처리되었습니다
 * (예: 리눅스의 int 0x80). 하지만 x86-64에서는 제조사가 시스템 콜을
 * 요청하는 효율적인 경로인 `syscall` 명령어를 제공합니다.
 *
 * syscall 명령어는 모델 특정 레지스터(MSR)에서 값을 읽어서 작동합니다.
 * 자세한 내용은 매뉴얼을 참조하세요. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* 인터럽트 서비스 루틴은 syscall_entry가 사용자 스택을 커널 모드
	 * 스택으로 교체할 때까지 어떤 인터럽트도 처리하지 않아야 합니다.
	 * 따라서 FLAG_FL을 마스크했습니다. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}



void halt() {
	power_off();
}

void exit (int status) {
	struct thread *t = thread_current();
	t->is_exited = true;
	t->exit_status = status;
	
	thread_exit();
}

tid_t fork (const char *thread_name) {
	struct thread *t = thread_current();

	return process_fork(thread_name, &t->tf);

}


int exec (const char *cmd_line) {

}

bool create (const char *file, unsigned initial_size) {
	struct thread *t = thread_current();
	
	if (file == NULL)
		exit(-1);

	if (pml4_get_page(t->pml4, file) == NULL)
		exit(-1);

	if (strlen(file) == 0)
	 	return false;

	return filesys_create(file, initial_size);
}

int open (const char *file) {
	struct thread *t = thread_current();
	int fd;

	if (file == NULL)
		exit(-1);

	if (pml4_get_page(t->pml4, file) == NULL)
		exit(-1);

	if (strlen(file) == 0)
	 	return -1;
		
	struct file *opened_file = filesys_open(file);	

	if (opened_file == NULL)
		return -1;

	fd = t->next_fd++;
	t->fdt[fd] = opened_file;

	
	return fd;
}

int filesize (int fd) {
	struct thread *t = thread_current();

	if (fd < 3 || fd > 63 || t->fdt[fd] == NULL)
		return -1;
	
	return file_length(t->fdt[fd]);

}

void close (int fd) {
	struct thread *t = thread_current();

	if (fd < 0 || fd > 63)
		exit(-1);

	if (t->fdt[fd] == NULL)
		return -1;

	t->fdt[fd] = NULL;
	t->next_fd--;

}

int read (int fd, const void *buffer, unsigned length) {
	struct thread *t = thread_current();
	struct file *read_file;

	if (fd < 0 || fd > 63 || length == 0 || buffer == NULL)
		return 0;
	
	if (fd == 0)
		return input_getc();
	
	if (pml4_get_page(t->pml4, buffer) == NULL)
		exit(-1);

	if (fd > 2) {
		if (t->fdt[fd] == NULL)
			return 0;
		read_file = t->fdt[fd];
		return file_read(read_file, buffer, length);
	}
		
}

int write (int fd, const void *buffer, unsigned length) {
	// 1. 접근 관점: fd의 상태에 따라 구현
	// 2. 유저 메모리에 안전하게 접근 buffer가 유저 메모리 공간
	//	  	helper 함수를 만들어 메모리를 검증하고 복사한다.
	// 3. 동시성 고려: 내부적으로 공유 리소스를 건드릴 수 있음.
	//		lock을 사용해 상호 배제(파일 시스템 작업할 때 락 필요)
	// 4. 반환값의 의미를 생각해라: 에러 -1 or 실제로 쓴 바이트 수
	// 5. 사이즈가 0인 경우
	// 6. NULL 포인터인 경우
	// 7. 이미 닫힌 fd를 참조하는 경우 (예외 처리)
	// 8. fd 테이블에서 fd에 해당하는 파일 객체를 찾을 수 있는가? 검증 필요
	if (fd <= 0 || length == 0 || buffer == NULL) return -1;
	if (fd == 1) {
		/// TODO: 표준 출력 (콘솔).  
		// return 읽은 바이트 수
		putbuf((char *) buffer, length);
		return length;
	}
	return -1;
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// printf ("system call!\n");
	uint64_t sys_num = f->R.rax; // 시스템 콜 번호 가져오기
	char *file;
	int fd;
	/* f에서 전달 받은 argument들을 가져온다. */
	switch (sys_num)
	{
	case SYS_HALT:
		/// TODO: halt() code 시스템 종료: [2, 4]
		halt();
		break;
	case SYS_EXIT:
		/// TODO: exit() code 에러: [2, 9]
		int status = (int) f->R.rdi; // 인자 가져오기
		exit(status);
		break;
	case SYS_FORK:
		char *thread_name = (char *) f->R.rdi;
		fork(thread_name);
		break;
	case SYS_EXEC:
		char *cmd_line = (char *) f->R.rdi;
		exec(cmd_line);
		break;
	case SYS_CREATE:
		file = (char *) f->R.rdi;
		unsigned initial_size = (unsigned) f->R.rsi;	
		f->R.rax = create(file, initial_size);
		break;
	case SYS_OPEN:
		file = (char *) f->R.rdi;
		f->R.rax = open(file);
		break;
	case SYS_FILESIZE:
		fd = (char *) f->R.rdi;
		f->R.rax = filesize(fd);
		break;
	case SYS_CLOSE:
		fd = f->R.rdi;
		close(fd);
		break;
	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	default:
		thread_exit ();
	}
	
	// thread_exit ();
}