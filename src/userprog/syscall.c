#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static struct lock filesys_ops_lock;

static void syscall_handler(struct intr_frame*);

void syscall_init(void) {
  lock_init(&filesys_ops_lock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Reads a byte at user virtual address UADDR.
UADDR must be below PHYS_BASE.
Returns the byte value if successful, -1 if a segfault
occurred. */
static int get_user(const uint8_t* uaddr) {
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

static bool test_pointer_4_bytes(const uint32_t* buffer) {
  const uint8_t* byte_ptr = (uint8_t*)buffer;
  if (get_user(byte_ptr++) == -1) {
    return false;
  }
  if (get_user(byte_ptr++) == -1) {
    return false;
  }
  if (get_user(byte_ptr++) == -1) {
    return false;
  }
  if (get_user(byte_ptr++) == -1) {
    return false;
  }
  return true;
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
  int fd_num = *((int*)aux);
  struct fd_elem* fd = list_entry(elem, struct fd_elem, elem);
  if (fd->fd == fd_num) {
    return true;
  }
  return false;
}

static void syscall_close(struct intr_frame* f, int fd_num) {
  struct thread* cur = thread_current();

  struct list_elem* elem =
      list_search(&cur->files, &file_with_fd, (void*)&fd_num);
  if (elem == NULL) {
    return;
  }
  struct fd_elem* fd = list_entry(elem, struct fd_elem, elem);

  lock_acquire(&filesys_ops_lock);
  file_close(fd->file);
  lock_release(&filesys_ops_lock);

  list_remove(elem);
  free(fd);
}

static void syscall_open(struct intr_frame* f, char* buffer) {
  if (buffer == NULL || !test_string(buffer)) {
    thread_exit(-1);
  }
  struct thread* cur = thread_current();
  int cur_fd = cur->fd_counter++;
  lock_acquire(&filesys_ops_lock);
  struct file* opened_file = filesys_open(buffer);
  lock_release(&filesys_ops_lock);

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
        list_search(&cur->files, &file_with_fd, (void*)&fd_num);
    if (elem == NULL) {
      f->eax = 0;
      return;
    }
    struct fd_elem* fd = list_entry(elem, struct fd_elem, elem);
    struct file* opened_file = fd->file;
    lock_acquire(&filesys_ops_lock);
    f->eax = file_write(opened_file, buffer, size);
    lock_release(&filesys_ops_lock);
  }
}

static void syscall_create(struct intr_frame* f, const char* file_name,
                           int32_t inital_size) {
  if (file_name == NULL || !test_string(file_name) || inital_size < 0) {
    thread_exit(-1);
  }
  lock_acquire(&filesys_ops_lock);
  bool status = filesys_create(file_name, inital_size);
  lock_release(&filesys_ops_lock);
  f->eax = status;
}

static void syscall_exit(const int* status_code) {
  thread_exit(!test_pointer_4_bytes(status_code) ? -1 : *status_code);
}

static void syscall_handler(struct intr_frame* f) {
  if (!test_pointer_4_bytes(f->esp)) {
    thread_exit(-1);
  }
  int sys_code = *((uint32_t*)f->esp);
  uint32_t* argv[3] = {(uint32_t*)(f->esp + 4), (uint32_t*)(f->esp + 8),
                       (uint32_t*)(f->esp + 12)};
  switch (sys_code) {
    case SYS_WRITE:
      syscall_write(f, (int)*argv[0], (char*)*argv[1], (unsigned)*argv[2]);
      break;
    case SYS_CREATE:
      syscall_create(f, (char*)*argv[0], *argv[1]);
      break;
    case SYS_EXIT:
      syscall_exit((int*)argv[0]);
      break;
    case SYS_OPEN:
      syscall_open(f, (char*)*argv[0]);
      break;
    case SYS_CLOSE:
      syscall_close(f, (int)*argv[0]);
      break;
    default:
      break;
  }

  // printf("system call!\n");
  // thread_exit();
}
