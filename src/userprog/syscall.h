#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init(void);
void lock_filesys(void);
void unlock_filesys(void);

#endif /* userprog/syscall.h */
