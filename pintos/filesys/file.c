#include "filesys/file.h"
#include <debug.h>
#include "filesys/inode.h"
#include "threads/malloc.h"

/* 열린 파일 구조체 */
struct file {
	struct inode *inode;        /* 파일의 inode */
	off_t pos;                  /* 현재 위치 */
	bool deny_write;            /* file_deny_write()가 호출되었는가? */
};

/* 주어진 INODE에 대한 파일을 열고, 해당 inode의 소유권을 가져가며
 * 새로운 파일을 반환. 할당이 실패하거나 INODE가 null이면
 * null 포인터를 반환 */
struct file *
file_open (struct inode *inode) {
	struct file *file = calloc (1, sizeof *file);
	if (inode != NULL && file != NULL) {
		file->inode = inode;
		file->pos = 0;
		file->deny_write = false;
		return file;
	} else {
		inode_close (inode);
		free (file);
		return NULL;
	}
}

/* FILE과 동일한 inode에 대한 새로운 파일을 열고 반환
 * 실패 시 null 포인터를 반환 */
struct file *
file_reopen (struct file *file) {
	return file_open (inode_reopen (file->inode));
}

/* 속성을 포함한 파일 객체를 복제하고 FILE과 동일한 inode에 대한
 * 새로운 파일을 반환. 실패 시 null 포인터를 반환 */
struct file *
file_duplicate (struct file *file) {
	struct file *nfile = file_open (inode_reopen (file->inode));
	if (nfile) {
		nfile->pos = file->pos;
		if (file->deny_write)
			file_deny_write (nfile);
	}
	return nfile;
}

/* FILE을 닫음 */
void
file_close (struct file *file) {
	if (file != NULL) {
		file_allow_write (file);
		inode_close (file->inode);
		free (file);
	}
}

/* FILE에 캡슐화된 inode를 반환 */
struct inode *
file_get_inode (struct file *file) {
	return file->inode;
}

/* FILE에서 SIZE 바이트를 BUFFER로 읽어옴
 * 파일의 현재 위치에서 시작
 * 실제로 읽은 바이트 수를 반환하며,
 * 파일 끝에 도달하면 SIZE보다 적을 수 있음
 * 읽은 바이트 수만큼 FILE의 위치를 전진시킴 */
off_t
file_read (struct file *file, void *buffer, off_t size) {
	off_t bytes_read = inode_read_at (file->inode, buffer, size, file->pos);
	file->pos += bytes_read;
	return bytes_read;
}

/* FILE에서 SIZE 바이트를 BUFFER로 읽어옴
 * 파일의 FILE_OFS 오프셋에서 시작
 * 실제로 읽은 바이트 수를 반환하며,
 * 파일 끝에 도달하면 SIZE보다 적을 수 있음
 * 파일의 현재 위치는 영향받지 않음 */
off_t
file_read_at (struct file *file, void *buffer, off_t size, off_t file_ofs) {
	return inode_read_at (file->inode, buffer, size, file_ofs);
}

/* BUFFER에서 SIZE 바이트를 FILE에 씀
 * 파일의 현재 위치에서 시작
 * 실제로 쓴 바이트 수를 반환하며,
 * 파일 끝에 도달하면 SIZE보다 적을 수 있음
 * (일반적으로 이 경우 파일을 확장하지만, 파일 확장은
 * 아직 구현되지 않음)
 * 읽은 바이트 수만큼 FILE의 위치를 전진시킴 */
off_t
file_write (struct file *file, const void *buffer, off_t size) {
	off_t bytes_written = inode_write_at (file->inode, buffer, size, file->pos);
	file->pos += bytes_written;
	return bytes_written;
}

/* BUFFER에서 SIZE 바이트를 FILE에 씀
 * 파일의 FILE_OFS 오프셋에서 시작
 * 실제로 쓴 바이트 수를 반환하며,
 * 파일 끝에 도달하면 SIZE보다 적을 수 있음
 * (일반적으로 이 경우 파일을 확장하지만, 파일 확장은
 * 아직 구현되지 않음)
 * 파일의 현재 위치는 영향받지 않음 */
off_t
file_write_at (struct file *file, const void *buffer, off_t size,
		off_t file_ofs) {
	return inode_write_at (file->inode, buffer, size, file_ofs);
}

/* file_allow_write()가 호출되거나 FILE이 닫힐 때까지
 * FILE의 기본 inode에 대한 쓰기 작업을 방지 */
void
file_deny_write (struct file *file) {
	ASSERT (file != NULL);
	if (!file->deny_write) {
		file->deny_write = true;
		inode_deny_write (file->inode);
	}
}

/* FILE의 기본 inode에 대한 쓰기 작업을 다시 활성화
 * (동일한 inode를 열고 있는 다른 파일에 의해
 * 쓰기가 여전히 거부될 수 있음) */
void
file_allow_write (struct file *file) {
	ASSERT (file != NULL);
	if (file->deny_write) {
		file->deny_write = false;
		inode_allow_write (file->inode);
	}
}

/* FILE의 크기를 바이트 단위로 반환 */
off_t
file_length (struct file *file) {
	ASSERT (file != NULL);
	return inode_length (file->inode);
}

/* FILE의 현재 위치를 파일 시작점에서
 * NEW_POS 바이트로 설정 */
void
file_seek (struct file *file, off_t new_pos) {
	ASSERT (file != NULL);
	ASSERT (new_pos >= 0);
	file->pos = new_pos;
}

/* FILE의 현재 위치를 파일 시작점에서의
 * 바이트 오프셋으로 반환 */
off_t
file_tell (struct file *file) {
	ASSERT (file != NULL);
	return file->pos;
}
