#include "filesys/fsutil.h"
#include <debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/disk.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"

/* 루트 디렉토리의 파일들을 나열 */
void
fsutil_ls (char **argv UNUSED) {
	struct dir *dir;
	char name[NAME_MAX + 1];

	printf ("Files in the root directory:\n");
	dir = dir_open_root ();
	if (dir == NULL)
		PANIC ("root dir open failed");
	while (dir_readdir (dir, name))
		printf ("%s\n", name);
	printf ("End of listing.\n");
}

/* 파일 ARGV[1]의 내용을 시스템 콘솔에
 * 16진수와 ASCII로 출력 */
void
fsutil_cat (char **argv) {
	const char *file_name = argv[1];

	struct file *file;
	char *buffer;

	printf ("Printing '%s' to the console...\n", file_name);
	file = filesys_open (file_name);
	if (file == NULL)
		PANIC ("%s: open failed", file_name);
	buffer = palloc_get_page (PAL_ASSERT);
	for (;;) {
		off_t pos = file_tell (file);
		off_t n = file_read (file, buffer, PGSIZE);
		if (n == 0)
			break;

		hex_dump (pos, buffer, n, true); 
	}
	palloc_free_page (buffer);
	file_close (file);
}

/* 파일 ARGV[1]을 삭제 */
void
fsutil_rm (char **argv) {
	const char *file_name = argv[1];

	printf ("Deleting '%s'...\n", file_name);
	if (!filesys_remove (file_name))
		PANIC ("%s: delete failed\n", file_name);
}

/* "scratch" 디스크(hdc 또는 hd1:0)에서 파일시스템의
 * 파일 ARGV[1]로 복사
 *
 * scratch 디스크의 현재 섹터는 "PUT\0" 문자열로 시작하고
 * 그 다음에 파일 크기를 바이트 단위로 나타내는 32비트 리틀 엔디안 정수가 옴
 * 이후 섹터들은 파일 내용을 담고 있음
 *
 * 이 함수의 첫 번째 호출은 scratch 디스크의 시작부터 읽기 시작
 * 이후 호출들은 디스크를 가로질러 진행됨
 * 이 디스크 위치는 fsutil_get()에서 사용하는 것과 독립적이므로
 * 모든 'put'은 모든 'get'보다 먼저 와야 함 */
void
fsutil_put (char **argv) {
	static disk_sector_t sector = 0;

	const char *file_name = argv[1];
	struct disk *src;
	struct file *dst;
	off_t size;
	void *buffer;

	printf ("Putting '%s' into the file system...\n", file_name);

	/* 버퍼 할당 */
	buffer = malloc (DISK_SECTOR_SIZE);
	if (buffer == NULL)
		PANIC ("couldn't allocate buffer");

	/* 소스 디스크를 열고 파일 크기를 읽음 */
	src = disk_get (1, 0);
	if (src == NULL)
		PANIC ("couldn't open source disk (hdc or hd1:0)");

	/* 파일 크기 읽기 */
	disk_read (src, sector++, buffer);
	if (memcmp (buffer, "PUT", 4))
		PANIC ("%s: missing PUT signature on scratch disk", file_name);
	size = ((int32_t *) buffer)[1];
	if (size < 0)
		PANIC ("%s: invalid file size %d", file_name, size);

	/* 대상 파일 생성 */
	if (!filesys_create (file_name, size))
		PANIC ("%s: create failed", file_name);
	dst = filesys_open (file_name);
	if (dst == NULL)
		PANIC ("%s: open failed", file_name);

	/* 복사 수행 */
	while (size > 0) {
		int chunk_size = size > DISK_SECTOR_SIZE ? DISK_SECTOR_SIZE : size;
		disk_read (src, sector++, buffer);
		if (file_write (dst, buffer, chunk_size) != chunk_size)
			PANIC ("%s: write failed with %"PROTd" bytes unwritten",
					file_name, size);
		size -= chunk_size;
	}

	/* 마무리 */
	file_close (dst);
	free (buffer);
}

/* 파일시스템에서 파일 FILE_NAME을 scratch 디스크로 복사
 *
 * scratch 디스크의 현재 섹터는 "GET\0"을 받고
 * 그 다음에 파일 크기를 32비트 리틀 엔디안 정수로 바이트 단위로 받음
 * 이후 섹터들은 파일의 데이터를 받음
 *
 * 이 함수의 첫 번째 호출은 scratch 디스크의 시작부터 쓰기 시작
 * 이후 호출들은 디스크를 가로질러 진행됨
 * 이 디스크 위치는 fsutil_put()에서 사용하는 것과 독립적이므로
 * 모든 'put'은 모든 'get'보다 먼저 와야 함 */
void
fsutil_get (char **argv) {
	static disk_sector_t sector = 0;

	const char *file_name = argv[1];
	void *buffer;
	struct file *src;
	struct disk *dst;
	off_t size;

	printf ("Getting '%s' from the file system...\n", file_name);

	/* 버퍼 할당 */
	buffer = malloc (DISK_SECTOR_SIZE);
	if (buffer == NULL)
		PANIC ("couldn't allocate buffer");

	/* 소스 파일 열기 */
	src = filesys_open (file_name);
	if (src == NULL)
		PANIC ("%s: open failed", file_name);
	size = file_length (src);

	/* 대상 디스크 열기 */
	dst = disk_get (1, 0);
	if (dst == NULL)
		PANIC ("couldn't open target disk (hdc or hd1:0)");

	/* 섹터 0에 크기 쓰기 */
	memset (buffer, 0, DISK_SECTOR_SIZE);
	memcpy (buffer, "GET", 4);
	((int32_t *) buffer)[1] = size;
	disk_write (dst, sector++, buffer);

	/* 복사 수행 */
	while (size > 0) {
		int chunk_size = size > DISK_SECTOR_SIZE ? DISK_SECTOR_SIZE : size;
		if (sector >= disk_size (dst))
			PANIC ("%s: out of space on scratch disk", file_name);
		if (file_read (src, buffer, chunk_size) != chunk_size)
			PANIC ("%s: read failed with %"PROTd" bytes unread", file_name, size);
		memset (buffer + chunk_size, 0, DISK_SECTOR_SIZE - chunk_size);
		disk_write (dst, sector++, buffer);
		size -= chunk_size;
	}

	/* 마무리 */
	file_close (src);
	free (buffer);
}
