#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

/* 디렉토리 구조체 */
struct dir {
	struct inode *inode;                /* 백업 저장소 */
	off_t pos;                          /* 현재 위치 */
};

/* 단일 디렉토리 엔트리 */
struct dir_entry {
	disk_sector_t inode_sector;         /* 헤더의 섹터 번호 */
	char name[NAME_MAX + 1];            /* null로 종료되는 파일 이름 */
	bool in_use;                        /* 사용 중인지 비어있는지 여부 */
};

/* 주어진 SECTOR에 ENTRY_CNT 개의 엔트리를 위한 공간을 가진 디렉토리를 생성
 * 성공하면 true, 실패하면 false를 반환 */
bool
dir_create (disk_sector_t sector, size_t entry_cnt) {
	return inode_create (sector, entry_cnt * sizeof (struct dir_entry));
}

/* 주어진 INODE에 대한 디렉토리를 열고 반환하며, 해당 inode의 소유권을 가져감
 * 실패 시 null 포인터를 반환 */
struct dir *
dir_open (struct inode *inode) {
	struct dir *dir = calloc (1, sizeof *dir);
	if (inode != NULL && dir != NULL) {
		dir->inode = inode;
		dir->pos = 0;
		return dir;
	} else {
		inode_close (inode);
		free (dir);
		return NULL;
	}
}

/* 루트 디렉토리를 열고 해당 디렉토리를 반환
 * 성공하면 true, 실패하면 false를 반환 */
struct dir *
dir_open_root (void) {
	return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* DIR과 동일한 inode에 대한 새로운 디렉토리를 열고 반환
 * 실패 시 null 포인터를 반환 */
struct dir *
dir_reopen (struct dir *dir) {
	return dir_open (inode_reopen (dir->inode));
}

/* DIR을 소멸시키고 관련 리소스를 해제 */
void
dir_close (struct dir *dir) {
	if (dir != NULL) {
		inode_close (dir->inode);
		free (dir);
	}
}

/* DIR에 캡슐화된 inode를 반환 */
struct inode *
dir_get_inode (struct dir *dir) {
	return dir->inode;
}

/* DIR에서 주어진 NAME을 가진 파일을 검색
 * 성공 시 true를 반환하고, EP가 non-null이면 *EP에 디렉토리 엔트리를 설정하며,
 * OFSP가 non-null이면 *OFSP에 디렉토리 엔트리의 바이트 오프셋을 설정
 * 그렇지 않으면 false를 반환하고 EP와 OFSP를 무시 */
static bool
lookup (const struct dir *dir, const char *name,
		struct dir_entry *ep, off_t *ofsp) {
	struct dir_entry e;
	size_t ofs;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
			ofs += sizeof e)
		if (e.in_use && !strcmp (name, e.name)) {
			if (ep != NULL)
				*ep = e;
			if (ofsp != NULL)
				*ofsp = ofs;
			return true;
		}
	return false;
}

/* DIR에서 주어진 NAME을 가진 파일을 검색하고
 * 존재하면 true, 그렇지 않으면 false를 반환
 * 성공 시 *INODE에 파일의 inode를 설정하고, 그렇지 않으면
 * null 포인터로 설정. 호출자는 *INODE를 닫아야 함 */
bool
dir_lookup (const struct dir *dir, const char *name,
		struct inode **inode) {
	struct dir_entry e;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	if (lookup (dir, name, &e, NULL))
		*inode = inode_open (e.inode_sector);
	else
		*inode = NULL;

	return *inode != NULL;
}

/* DIR에 NAME이라는 이름의 파일을 추가하며, 이미 해당 이름의 파일이 있으면 안됨
 * 파일의 inode는 INODE_SECTOR 섹터에 있음
 * 성공하면 true, 실패하면 false를 반환
 * NAME이 유효하지 않거나(즉, 너무 길거나) 디스크 또는 메모리 오류가 발생하면 실패 */
bool
dir_add (struct dir *dir, const char *name, disk_sector_t inode_sector) {
	struct dir_entry e;
	off_t ofs;
	bool success = false;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	/* NAME의 유효성을 검사 */
	if (*name == '\0' || strlen (name) > NAME_MAX)
		return false;

	/* NAME이 이미 사용 중인지 확인 */
	if (lookup (dir, name, NULL, NULL))
		goto done;

	/* OFS를 빈 슬롯의 오프셋으로 설정
	 * 빈 슬롯이 없으면 현재 파일 끝으로 설정됨

	 * inode_read_at()은 파일 끝에서만 짧은 읽기를 반환
	 * 그렇지 않으면 메모리 부족과 같은 일시적인 문제로 인한
	 * 짧은 읽기가 아닌지 확인해야 함 */
	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
			ofs += sizeof e)
		if (!e.in_use)
			break;

	/* 슬롯에 쓰기 */
	e.in_use = true;
	strlcpy (e.name, name, sizeof e.name);
	e.inode_sector = inode_sector;
	success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

done:
	return success;
}

/* DIR에서 NAME에 대한 모든 엔트리를 제거
 * 성공하면 true, 실패하면 false를 반환
 * 주어진 NAME을 가진 파일이 없을 때만 실패 */
bool
dir_remove (struct dir *dir, const char *name) {
	struct dir_entry e;
	struct inode *inode = NULL;
	bool success = false;
	off_t ofs;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	/* 디렉토리 엔트리 찾기 */
	if (!lookup (dir, name, &e, &ofs))
		goto done;

	/* inode 열기 */
	inode = inode_open (e.inode_sector);
	if (inode == NULL)
		goto done;

	/* 디렉토리 엔트리 지우기 */
	e.in_use = false;
	if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
		goto done;

	/* inode 제거 */
	inode_remove (inode);
	success = true;

done:
	inode_close (inode);
	return success;
}

/* DIR에서 다음 디렉토리 엔트리를 읽고 이름을 NAME에 저장
 * 성공하면 true, 디렉토리에 더 이상 엔트리가 없으면 false를 반환 */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1]) {
	struct dir_entry e;

	while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) {
		dir->pos += sizeof e;
		if (e.in_use) {
			strlcpy (name, e.name, NAME_MAX + 1);
			return true;
		}
	}
	return false;
}
