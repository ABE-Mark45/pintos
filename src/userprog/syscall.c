#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

static void syscall_handler(struct intr_frame*);

void syscall_init(void) {
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Reads a byte at user virtual address UADDR.
UADDR must be below PHYS_BASE.
Returns the byte value if successful, -1 if a segfault
occurred. */
static int get_user(const uint8_t* uaddr) {
  // printf("%p\n", uaddr);
  if ((void*)uaddr >= PHYS_BASE) {
    return -1;
  }
  int result;
  asm("movl $1f, %0; movzbl %1, %0; 1:" : "=&a"(result) : "m"(*uaddr));
  return result;
}
/* Writes BYTE to user address UDST.
UDST must be below PHYS_BASE.
Returns true if successful, false if a segfault occurred. */
static bool put_user(uint8_t* udst, uint8_t byte) {
  if ((void*)udst < PHYS_BASE) {
    return -1;
  }
  int error_code;
  asm("movl $1f, %0; movb %b2, %1; 1:"
      : "=&a"(error_code), "=m"(*udst)
      : "q"(byte));
  return error_code != -1;
}

static bool test_pointer_4_bytes(void* byte_ptr) {
  if (byte_ptr + 4 >= PHYS_BASE) {
    return false;
  }
  return get_user(byte_ptr++) != -1 && get_user(byte_ptr++) != -1 &&
         get_user(byte_ptr++) != -1 && get_user(byte_ptr++) != -1;
}

static bool test_string(const char* buffer) {
  int x = 0;
  while (true) {
    if (get_user((uint8_t*)buffer) == -1) {
      return false;
    }
    if (*buffer == '\0') {
      break;
    }
    x++;
    buffer++;
  }
  return true;
}

static bool file_with_fd(const struct list_elem* elem, void* aux) {
  int fd_num = ((int)aux);
  struct fd_elem* fd = list_entry(elem, struct fd_elem, elem);
  return fd->fd == fd_num;
}

static void syscall_close(int fd_num) {
  struct thread* cur = thread_current();

  struct list_elem* elem =
      list_search(&cur->files, &file_with_fd, (void*)fd_num);
  if (elem == NULL) {
    return;
  }
  struct fd_elem* fd = list_entry(elem, struct fd_elem, elem);

  lock_filesys();
  file_close(fd->file);
  unlock_filesys();

  list_remove(elem);
  free(fd);
}

static void syscall_open(struct intr_frame* f, char* buffer) {
  if (buffer == NULL || !test_string(buffer)) {
    thread_exit(-1);
  }
  struct thread* cur = thread_current();
  int cur_fd = cur->fd_counter++;
  lock_filesys();
  struct file* opened_file = filesys_open(buffer);
  unlock_filesys();

  if (opened_file == NULL) {
    f->eax = -1;
    return;
  }

  struct fd_elem* fd = (struct fd_elem*)malloc(sizeof(struct fd_elem));
  fd->fd = cur_fd;
  fd->file = opened_file;
  list_push_front(&cur->files, &fd->elem);
  f->eax = cur_fd;
}

static void syscall_write(struct intr_frame* f, int fd_num, char* buffer,
                          unsigned size) {
  if (buffer == NULL || !test_string(buffer) || fd_num < 0 ||
      fd_num == STDIN_FILENO) {
    f->eax = -1;
    thread_exit(-1);
    return;
  }

  if (fd_num == STDOUT_FILENO) {
    putbuf(buffer, size);
    f->eax = size;
  } else {
    struct thread* cur = thread_current();
    struct list_elem* elem =
        list_search(&cur->files, &file_with_fd, (void*)fd_num);
    if (elem == NULL) {
      f->eax = 0;
      return;
    }
    struct fd_elem* fd = list_entry(elem, struct fd_elem, elem);
    struct file* opened_file = fd->file;
    lock_filesys();
    f->eax = file_write(opened_file, buffer, size);
    unlock_filesys();
  }
}

static void syscall_read(struct intr_frame* f, int fd_num, char* buffer,
                         unsigned size) {
  if (buffer == NULL || !test_string(buffer) || fd_num < 0 ||
      fd_num == STDIN_FILENO) {
    f->eax = -1;
    thread_exit(-1);
    return;
  }

  if (fd_num == STDIN_FILENO) {
    for (int i = 0; i < size; i++) {
      *buffer++ = input_getc();
    }
    f->eax = size;
  } else {
    struct thread* cur = thread_current();
    struct list_elem* elem =
        list_search(&cur->files, &file_with_fd, (void*)fd_num);
    if (elem == NULL) {
      f->eax = 0;
      return;
    }
    struct fd_elem* fd = list_entry(elem, struct fd_elem, elem);
    struct file* opened_file = fd->file;
    lock_filesys();
    f->eax = file_read(opened_file, buffer, size);
    unlock_filesys();
  }
}

static void syscall_seek(int fd_num, unsigned position) {
  struct thread* cur = thread_current();
  struct list_elem* elem =
      list_search(&cur->files, &file_with_fd, (void*)fd_num);
  if (elem == NULL) {
    return;
  }
  struct fd_elem* fd = list_entry(elem, struct fd_elem, elem);
  struct file* opened_file = fd->file;
  lock_filesys();
  file_seek(opened_file, position);
  unlock_filesys();
}

static void syscall_tell(struct intr_frame* f, int fd_num) {
  struct thread* cur = thread_current();
  struct list_elem* elem =
      list_search(&cur->files, &file_with_fd, (void*)fd_num);
  if (elem == NULL) {
    f->eax = -1;
    return;
  }
  struct fd_elem* fd = list_entry(elem, struct fd_elem, elem);
  struct file* opened_file = fd->file;
  lock_filesys();
  f->eax = file_tell(opened_file);
  unlock_filesys();
}

static void syscall_filesize(struct intr_frame* f, int fd_num) {
  struct thread* cur = thread_current();
  struct list_elem* elem =
      list_search(&cur->files, &file_with_fd, (void*)fd_num);
  if (elem == NULL) {
    f->eax = -1;
    return;
  }
  struct fd_elem* fd = list_entry(elem, struct fd_elem, elem);
  struct file* opened_file = fd->file;
  lock_filesys();
  f->eax = file_length(opened_file);
  unlock_filesys();
}

static void syscall_create(struct intr_frame* f, const char* file_name,
                           int32_t inital_size) {
  if (file_name == NULL || !test_string(file_name) || inital_size < 0) {
    thread_exit(-1);
  }
  lock_filesys();
  bool status = filesys_create(file_name, inital_size);
  unlock_filesys();
  f->eax = status;
}

static void syscall_remove(struct intr_frame* f, const char* file_name) {
  if (file_name == NULL || !test_string(file_name)) {
    thread_exit(-1);
  }
  lock_filesys();
  bool status = filesys_remove(file_name);
  unlock_filesys();
  f->eax = status;
}

static void syscall_exit(const int* status_code) {
  thread_exit(!test_pointer_4_bytes(status_code) ? -1 : *status_code);
}

static void syscall_exec(struct intr_frame* f, const char* cmd_line) {
  if (cmd_line == NULL || !test_string(cmd_line)) {
    f->eax = -1;
    return;
  }
  f->eax = process_execute(cmd_line);
}

static void check_and_parse_args(uint32_t** argv, void* ptr, int count) {
  for (int i = 0; i < count; i++) {
    if (!test_pointer_4_bytes(ptr + (i << 2))) {
      thread_exit(-1);
    }
    argv[i] = (uint32_t*)(ptr + (i << 2));
  }
}

static void syscall_handler(struct intr_frame* f) {
  if (!test_pointer_4_bytes(f->esp)) {
    thread_exit(-1);
  }
  int sys_code = *((uint32_t*)f->esp);
  uint32_t* argv[3];

  switch (sys_code) {
    case SYS_HALT:
      shutdown_power_off();
      break;
    case SYS_WRITE:
      check_and_parse_args(argv, f->esp + 4, 3);
      syscall_write(f, (int)*argv[0], (char*)*argv[1], (unsigned)*argv[2]);
      break;
    case SYS_READ:
      check_and_parse_args(argv, f->esp + 4, 3);
      syscall_read(f, (int)*argv[0], (char*)*argv[1], (unsigned)*argv[2]);
      break;

    case SYS_CREATE:
      check_and_parse_args(argv, f->esp + 4, 2);
      syscall_create(f, (char*)*argv[0], *argv[1]);
      break;
    case SYS_REMOVE:
      check_and_parse_args(argv, f->esp + 4, 1);
      syscall_remove(f, (char*)*argv[0]);
      break;
    case SYS_EXIT:
      check_and_parse_args(argv, f->esp + 4, 1);
      syscall_exit((int*)argv[0]);
      break;
    case SYS_OPEN:
      check_and_parse_args(argv, f->esp + 4, 1);
      syscall_open(f, (char*)*argv[0]);
      break;
    case SYS_CLOSE:
      check_and_parse_args(argv, f->esp + 4, 1);
      syscall_close((int)*argv[0]);
      break;
    case SYS_FILESIZE:
      check_and_parse_args(argv, f->esp + 4, 1);
      syscall_filesize(f, (int)*argv[0]);
      break;
    case SYS_SEEK:
      check_and_parse_args(argv, f->esp + 4, 2);
      syscall_seek((int)*argv[0], (unsigned)*argv[1]);
      break;
    case SYS_TELL:
      check_and_parse_args(argv, f->esp + 4, 1);
      syscall_tell(f, (int)*argv[0]);
      break;
    case SYS_EXEC:
      check_and_parse_args(argv, f->esp + 4, 1);
      syscall_exec(f, (char*)*argv[0]);
      break;
    case SYS_WAIT:
      check_and_parse_args(argv, f->esp + 4, 1);
      f->eax = process_wait((tid_t)*argv[0]);
      break;
    default:
      break;
  }

  // printf("system call!\n");
  // thread_exit();
}
