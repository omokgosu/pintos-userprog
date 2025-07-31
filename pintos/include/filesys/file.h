#ifndef FILESYS_FILE_H
#define FILESYS_FILE_H

#include "filesys/off_t.h"

#define FDT_CNT 512

struct inode;
enum fd_type {
    FD_STDIN,
    FD_STDOUT,
    FD_FILE,
    FD_DIR,
};

struct file_descriptor {
    int fd;                     /* 파일 디스크립터 번호 */
    enum fd_type type;          /* 파일 디스크립터 종류 */
    struct file *file;          /* FD_FILE 일때 사용 */
};

/* Opening and closing files. */
struct file *file_open (struct inode *);
struct file *file_reopen (struct file *);
struct file *file_duplicate (struct file *file);
void file_close (struct file *);
struct inode *file_get_inode (struct file *);

/* Reading and writing. */
off_t file_read (struct file *, void *, off_t);
off_t file_read_at (struct file *, void *, off_t size, off_t start);
off_t file_write (struct file *, const void *, off_t);
off_t file_write_at (struct file *, const void *, off_t size, off_t start);

/* Preventing writes. */
void file_deny_write (struct file *);
void file_allow_write (struct file *);

/* File position. */
void file_seek (struct file *, off_t);
off_t file_tell (struct file *);
off_t file_length (struct file *);

#endif /* filesys/file.h */
