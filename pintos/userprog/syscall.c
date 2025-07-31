#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

struct lock filesys_lock;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
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

    lock_init(&filesys_lock);
}

/* power_off()를 호출하여 Pintos를 종료합니다. */
void
halt() {
	power_off();
}

/* 
    현재 사용자 프로그램을 종료하고 인자로 전달된 status 값을 전달합니다.
    이 값은 부모 프로세스가 wait() 시스템 콜을 통해 회수할 수 있으며
    일반적으로 status == 0 은 성공 status != 0 은 오류를 의미합니다.    
*/
void
exit(int status) {
    struct thread *t = thread_current();
    
    t->exit_status = status;
    t->exited = true;
    
    thread_exit();
}

/* 현재 프로세스를 복제해서 자식 프로세스를 생성합니다. */
tid_t
fork(const char *name , struct intr_frame*if_) {
    return process_fork(name, if_);
}

/*
    cmd_line에 주어진 실행 파일 이름을 기반으로,
    현재 프로세스를 해당 실행파일로 바꿉니다.
    인자들도 함께 전달됩니다.
*/
void
exec(void *file_name) {
    /* 실행파일 로드 실패 */
    char *fn_copy = palloc_get_page(0);

    if (fn_copy == NULL) exit(-1);

    strlcpy(fn_copy, file_name, PGSIZE); 

    if (process_exec(fn_copy) == -1) exit(-1);
}

/*
    wait 시스템 콜은 특정 자식 프로세스 pid 가 종료될 때 까지 기다리고,
    그 자식이 exit()에 넘긴 종료 상태값을 반환합니다.
*/
int
wait (tid_t child_tid) {
    return process_wait(child_tid);
};

/* 초기 크기가 initial_size 바이트인 새 파일을 생성합니다. */
bool 
create (const char *file, unsigned initial_size) {
    if ( file == NULL ) exit(-1);

    lock_acquire(&filesys_lock);
    bool success = filesys_create(file, initial_size);
    lock_release(&filesys_lock);

    return success;
}

/* 이름이 file인 파일을 삭제합니다. */
bool
remove (const char *file) {
    if ( file == NULL ) exit(-1);

    lock_acquire(&filesys_lock);
    bool success = filesys_remove(file);
    lock_release(&filesys_lock);

    return success;
}

/* fd 라고 하는 음이아닌 정수를 반환하거나 파일을 열 수 없는 경우 -1 을 반환합니다. */
int open (const char *file) {
    struct file *opened_file = NULL;

    lock_acquire(&filesys_lock);
    opened_file = filesys_open(file);
    lock_release(&filesys_lock);
    
    if ( opened_file == NULL ) return -1;

    struct thread *t = thread_current();

    for (int i = 2; i < FDT_CNT; i++ ) {
        if ( t->fdt[i] == NULL ) {
            t->fdt[i] = malloc( sizeof( struct file_descriptor ));
            t->fdt[i]->fd = i;
            t->fdt[i]->type = FD_FILE;
            t->fdt[i]->file = opened_file;
            return i;
        }
    }

    return -1;
}

/* file의 size를 byte 로 받아옵니다. */
int filesize (int fd) {
    if ( fd < 0 || fd >= FDT_CNT || thread_current()->fdt[fd] == NULL ) {
        exit (-1);
    }

    lock_acquire(&filesys_lock);
    int read_byte = file_length( thread_current()->fdt[fd]->file ); 
    lock_release(&filesys_lock);
    
    return read_byte;
}

/* 
    fd의 파일정보를 size 만큼 buffer로 받아옵니다.
    실제로 읽은 바이트 수를 반환하며, 읽은 바이트 수만큼 file 위치를 전진시킵니다.
*/
int read (int fd, void *buffer, unsigned size) {
    if ( fd >= FDT_CNT ) exit(-1);
        

    if ( fd == 0 ) {
        for (int i = 0; i < size; i++) {
            ((char *)buffer)[i] = input_getc();
        }

        return size;
    } else {
        struct file* read_file = thread_current()->fdt[fd]->file;

        if ( read_file == NULL ) exit(-1);
        
        lock_acquire(&filesys_lock);
        int read_byte = file_read(read_file, buffer, size); 
        lock_release(&filesys_lock);

        return read_byte;
    }
};

/* buffer에서 length 만큼 fd 파일에 작성합니다. */
int
write ( int fd, const void *buffer, unsigned length ) {
	
    if ( fd < 0 || fd >= FDT_CNT || thread_current()->fdt[fd] == NULL ) {
        return -1;
    } else if (fd == 1) {
		putbuf((char *) buffer, length);
		return length;
	} else {
        struct file* read_file = thread_current()->fdt[fd]->file;

        if ( read_file == NULL ) return -1;

        lock_acquire(&filesys_lock);
        int write_byte = file_write(read_file, buffer, length); 
        lock_release(&filesys_lock);

        return write_byte;
    }
}

/* fd로 열린 파일에 대해, 다음 읽기 또는 쓰기 작업이 수행될 위치를 position 바이트 지점으로 변경합니다.*/
void seek (int fd, unsigned position){
    if ( fd < 0 || fd >= FDT_CNT ) {
        exit (-1);
    }

    struct file* read_file = thread_current()->fdt[fd]->file;

    if ( read_file == NULL ) exit(-1);

    lock_acquire(&filesys_lock);
    file_seek(read_file, position);
    lock_release(&filesys_lock);
}

/* fd로 열린 파일에 대해 다음에 읽거나 쓸 바이트의 위치를 반환합니다. ( 즉, 현재 위치 )*/
unsigned tell (int fd) {
    if ( fd < 0 || fd >= 512 ) {
        exit (-1);
    }

    struct file* read_file = thread_current()->fdt[fd]->file;

    if ( read_file == NULL ) exit(-1);

    lock_acquire(&filesys_lock);
    file_tell(read_file);
    lock_release(&filesys_lock);
}

/* fd로 열린 파일을 닫습니다. */
void close (int fd) {
    struct thread *t = thread_current();
    
    if ( fd < 0 || fd >= FDT_CNT || thread_current()->fdt[fd] == NULL ) {
        exit (-1);
    }

    struct file* read_file = thread_current()->fdt[fd]->file;

    if ( read_file != NULL ) {
        lock_acquire(&filesys_lock);
        file_close(read_file);
        free(thread_current()->fdt[fd]);
        thread_current()->fdt[fd] = NULL;
        lock_release(&filesys_lock);
    } 
}

/* 인자로 전달받은 값에 사용자 주소가 입력되었을 경우, 유효한 주소인지 검사합니다. */
void
validate_address(vaddr) {
    if ( 
        vaddr == NULL || // 주소가 NULL 포인터
        !is_user_vaddr(vaddr) || // 주소가 유저공간이 아님
        pml4_get_page(thread_current()->pml4 , vaddr) == NULL // 주소가 할당되지 않았음
    ) {
        exit(-1);
    }
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	 uint64_t sys_num = f->R.rax; // 시스템 콜 번호 가져오기

	switch (sys_num)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		int status = f->R.rdi;
		exit(status);
		break;
    case SYS_FORK:
        char *name = f->R.rdi;
        validate_address(name);
        f->R.rax = fork(name, f);
        break;
    case SYS_EXEC:
        void *file_name = f->R.rdi;
        validate_address(file_name);
        exec(file_name);
        break;
    case SYS_WAIT:
        int tid = f->R.rdi;
        f->R.rax = wait(tid);
        break;
    case SYS_CREATE:
        char *create_file_name = f->R.rdi;
        unsigned initial_size = f->R.rsi;
        validate_address(create_file_name);
        f->R.rax = create (create_file_name, initial_size);
        break;
    case SYS_REMOVE:
        char *remove_file_name = f->R.rdi;
        validate_address(remove_file_name);
        f->R.rax = remove(remove_file_name);
        break;
    case SYS_OPEN:
        char *open_file_name = f->R.rdi;
        validate_address(open_file_name);
        f->R.rax = open(open_file_name);
        break;
    case SYS_FILESIZE:
        int filesize_fd = f->R.rdi;
        f->R.rax = filesize(filesize_fd);
        break;
    case SYS_READ:
        int read_fd = f->R.rdi;
        void *read_buffer = f->R.rsi;
        unsigned size = f->R.rdx;
        validate_address(read_buffer);
        f->R.rax = read(read_fd, read_buffer, size);
        break;
	case SYS_WRITE:
        int write_fd = f->R.rdi;
        void *buffer = f->R.rsi;
        unsigned length = f->R.rdx;
        validate_address(buffer);
		f->R.rax = write(write_fd, buffer, length);
		break;
    case SYS_SEEK:
        int seek_fd = f->R.rdi;
        unsigned position = f->R.rsi;
        seek(seek_fd, position);
        break;
    case SYS_TELL:
        int tell_fd = f->R.rdi;
        tell(tell_fd);
        break;
    case SYS_CLOSE:
        int close_fd = f->R.rdi;
        close(close_fd);
        break;
	default:
		thread_exit ();
	}
	
	// thread_exit ();
}