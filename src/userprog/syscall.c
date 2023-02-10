#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
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
      PANIC("WRONG: %d", x);
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

static void syscall_write(struct intr_frame* f) {
  int fd = *((int*)f->esp + 1);
  char* buffer = (char*)(*((int*)f->esp + 2));
  unsigned size = *((unsigned*)f->esp + 3);

  // hex_dump((uintptr_t)f->esp, f->esp, 512, true);
  // PANIC("HERE: %d, %p, %d", fd, buffer, size);

  if (!test_string(buffer)) {
    thread_exit(-1);
    f->eax = -1;
    return;
  }

  if (fd == STDOUT_FILENO) {
    putbuf(buffer, size);
    f->eax = size;
  } else {
    f->eax = -1;
  }
}

static void syscall_create(struct intr_frame* f, const char* file_name,
                           uint32_t inital_size) {

  bool status = filesys_create(file_name, inital_size);
  f->eax = status;
}

static void syscall_exit(const uint32_t* status_code) {
  thread_exit(!test_pointer_4_bytes(status_code) ? -1 : *status_code);
}

static void syscall_handler(struct intr_frame* f) {
  if (!test_pointer_4_bytes(f->esp)) {
    thread_exit(-1);
  }
  int sys_code = *((uint32_t*)f->esp);
  void* argv[3] = {(void*)f->esp + 1, (void*)f->esp + 2, (void*)f->esp + 3};
  switch (sys_code) {
    case SYS_WRITE:
      syscall_write(f);
      break;
    case SYS_CREATE:
      syscall_create(f, (char*)argv[0], *((uint32_t*)argv[1]));
      break;
    case SYS_EXIT:
      syscall_exit(((int*)(f->esp + 4)));
      break;
    default:
      break;
  }

  // printf("system call!\n");
  // thread_exit();
}
