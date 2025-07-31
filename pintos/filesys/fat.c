#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

/* DISK_SECTOR_SIZE보다 작아야 함 */
struct fat_boot {
	unsigned int magic;
	unsigned int sectors_per_cluster; /* 1로 고정 */
	unsigned int total_sectors;
	unsigned int fat_start;
	unsigned int fat_sectors; /* FAT의 섹터 단위 크기 */
	unsigned int root_dir_cluster;
};

/* FAT 파일시스템 */
struct fat_fs {
	struct fat_boot bs;
	unsigned int *fat;
	unsigned int fat_length;
	disk_sector_t data_start;
	cluster_t last_clst;
	struct lock write_lock;
};

static struct fat_fs *fat_fs;

void fat_boot_create (void);
void fat_fs_init (void);

void
fat_init (void) {
	fat_fs = calloc (1, sizeof (struct fat_fs));
	if (fat_fs == NULL)
		PANIC ("FAT init failed");

	// 디스크에서 부트 섹터 읽기
	unsigned int *bounce = malloc (DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT init failed");
	disk_read (filesys_disk, FAT_BOOT_SECTOR, bounce);
	memcpy (&fat_fs->bs, bounce, sizeof (fat_fs->bs));
	free (bounce);

	// FAT 정보 추출
	if (fat_fs->bs.magic != FAT_MAGIC)
		fat_boot_create ();
	fat_fs_init ();
}

void
fat_open (void) {
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT load failed");

	// 디스크에서 직접 FAT 로드
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_read = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_read;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_read (filesys_disk, fat_fs->bs.fat_start + i,
			           buffer + bytes_read);
			bytes_read += DISK_SECTOR_SIZE;
		} else {
			uint8_t *bounce = malloc (DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT load failed");
			disk_read (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			memcpy (buffer + bytes_read, bounce, bytes_left);
			bytes_read += bytes_left;
			free (bounce);
		}
	}
}

void
fat_close (void) {
	// FAT 부트 섹터 쓰기
	uint8_t *bounce = calloc (1, DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT close failed");
	memcpy (bounce, &fat_fs->bs, sizeof (fat_fs->bs));
	disk_write (filesys_disk, FAT_BOOT_SECTOR, bounce);
	free (bounce);

	// FAT를 디스크에 직접 쓰기
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_wrote = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_wrote;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_write (filesys_disk, fat_fs->bs.fat_start + i,
			            buffer + bytes_wrote);
			bytes_wrote += DISK_SECTOR_SIZE;
		} else {
			bounce = calloc (1, DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT close failed");
			memcpy (bounce, buffer + bytes_wrote, bytes_left);
			disk_write (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			bytes_wrote += bytes_left;
			free (bounce);
		}
	}
}

void
fat_create (void) {
	// FAT 부트 생성
	fat_boot_create ();
	fat_fs_init ();

	// FAT 테이블 생성
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT creation failed");

	// ROOT_DIR_CLST 설정
	fat_put (ROOT_DIR_CLUSTER, EOChain);

	// ROOT_DIR_CLUSTER 영역을 0으로 채우기
	uint8_t *buf = calloc (1, DISK_SECTOR_SIZE);
	if (buf == NULL)
		PANIC ("FAT create failed due to OOM");
	disk_write (filesys_disk, cluster_to_sector (ROOT_DIR_CLUSTER), buf);
	free (buf);
}

void
fat_boot_create (void) {
	unsigned int fat_sectors =
	    (disk_size (filesys_disk) - 1)
	    / (DISK_SECTOR_SIZE / sizeof (cluster_t) * SECTORS_PER_CLUSTER + 1) + 1;
	fat_fs->bs = (struct fat_boot){
	    .magic = FAT_MAGIC,
	    .sectors_per_cluster = SECTORS_PER_CLUSTER,
	    .total_sectors = disk_size (filesys_disk),
	    .fat_start = 1,
	    .fat_sectors = fat_sectors,
	    .root_dir_cluster = ROOT_DIR_CLUSTER,
	};
}

void
fat_fs_init (void) {
	/* TODO: 여기에 코드를 작성하세요. */
}

/*----------------------------------------------------------------------------*/
/* FAT 처리                                                                   */
/*----------------------------------------------------------------------------*/

/* 체인에 클러스터를 추가
 * CLST가 0이면 새로운 체인을 시작
 * 새로운 클러스터 할당에 실패하면 0을 반환 */
cluster_t
fat_create_chain (cluster_t clst) {
	/* TODO: 여기에 코드를 작성하세요. */
}

/* CLST에서 시작하는 클러스터 체인을 제거
 * PCLST가 0이면 CLST를 체인의 시작으로 간주 */
void
fat_remove_chain (cluster_t clst, cluster_t pclst) {
	/* TODO: 여기에 코드를 작성하세요. */
}

/* FAT 테이블의 값을 업데이트 */
void
fat_put (cluster_t clst, cluster_t val) {
	/* TODO: 여기에 코드를 작성하세요. */
}

/* FAT 테이블의 값을 가져오기 */
cluster_t
fat_get (cluster_t clst) {
	/* TODO: 여기에 코드를 작성하세요. */
}

/* 클러스터 번호를 섹터 번호로 변환 */
disk_sector_t
cluster_to_sector (cluster_t clst) {
	/* TODO: 여기에 코드를 작성하세요. */
}
