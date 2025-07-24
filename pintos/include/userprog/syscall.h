#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>

void syscall_init (void);
void halt();
void exit(int status);
bool create (const char *file, unsigned initial_size);
int open (const char *file);
int write (int fd, const void *buffer, unsigned length);
void close(int fd);

#endif /* userprog/syscall.h */
