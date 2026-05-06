#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"

#include "devices/shutdown.h"  // shutdown_power_off`
#include "devices/input.h"     // input_getc
#include "userprog/process.h"  // process_execute, process_wait
#include "threads/malloc.h"    // malloc, free
#include <string.h>            // strlen, strlcpy

/**
 * Global lock for the file system */
struct lock file_lock;

// FIXME
// this causes race condition
struct child_process *pending_exec_child;

static void syscall_handler (struct intr_frame *);

void halt(void);
void exit(int status);
pid_t exec(const char *file);
int wait(pid_t pid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

void validate_pointer(const void* ptr);
void validate_buffer(const void* ptr, unsigned size);
void validate_string(const char *str);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  // NEW
  lock_init(&file_lock); // initialize the global file system lock
  // EndOfNew
}

/**
 * System Call	Prototype	What to Implement
halt	void halt(void)	Call shutdown_power_off()
exit	void exit(int status)	Terminate process, print message, free resources
exec	pid_t exec(const char *cmd_line)	Load and run new program, return PID or -1
wait	int wait(pid_t pid)	Wait for child process, return exit status
create	bool create(const char *file, unsigned size)	Create new file
remove	bool remove(const char *file)	Delete file
open	int open(const char *file)	Open file, return file descriptor
filesize	int filesize(int fd)	Return file size in bytes
read	int read(int fd, void *buffer, unsigned size)	Read from file or console (fd 0)
write	int write(int fd, const void *buffer, unsigned size)	Write to file or console (fd 1)
seek	void seek(int fd, unsigned position)	Move file position
tell	unsigned tell(int fd)	Return current file position
close	void close(int fd)	Close file descriptor.
each function's return value is put into the interrupt frame's eax register before returning to the user program.
 */
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  // REVIEW

  validate_buffer(f->esp, sizeof(int32_t)); // validate the user stack pointer before accessing it

  int32_t *user_stack = (int32_t *) f->esp;
  int SYSCALL_TYPE = user_stack[0];
  switch (SYSCALL_TYPE) {
    case SYS_HALT: {
      halt();
      break;
    }
    case SYS_EXIT: {
      validate_buffer(user_stack + 1,sizeof(int32_t));
      int status = user_stack[1];
      exit(status);
      break;
    }
    case SYS_EXEC: {
      validate_buffer(user_stack + 1,sizeof(int32_t));
      const char* filename = (const char*) user_stack[1];
      validate_string(filename); // validate the filename pointer before accessing it
      f->eax = exec(filename);
      break;
    }
    case SYS_WAIT:{
      validate_buffer(user_stack + 1,sizeof(int32_t));
      pid_t pid = (pid_t) user_stack[1];
      f->eax = wait(pid);
      break;
    }
    case SYS_CREATE: {
      validate_buffer(user_stack + 1,sizeof(int32_t));
      validate_buffer(user_stack + 2,sizeof(int32_t));
      const char* filename = (const char*) user_stack[1];
      validate_string(filename); // validate the filename pointer before accessing it
      unsigned initial_size = user_stack[2];
      f->eax = create(filename, initial_size);
      break;
    }
    case SYS_REMOVE: {
      validate_buffer(user_stack + 1,sizeof(int32_t));
      const char* filename = (const char*) user_stack[1];
      validate_string(filename); // validate the filename pointer before accessing it
      f->eax = remove(filename);
      break;
    }
    case SYS_OPEN: {
      validate_buffer(user_stack + 1,sizeof(int32_t));
      const char* filename = (const char*) user_stack[1];
      validate_string(filename); // validate the filename pointer before accessing it
      f->eax = open(filename);
      break;
    }
    case SYS_FILESIZE: {
      validate_buffer(user_stack + 1,sizeof(int32_t));
      int fd = user_stack[1];
      f->eax = filesize(fd);
      break;
    }
    case SYS_READ: {
      validate_buffer(user_stack + 1,sizeof(int32_t));
      validate_buffer(user_stack + 2,sizeof(int32_t));
      validate_buffer(user_stack + 3,sizeof(int32_t));
      int fd = user_stack[1];
      void* buffer = (void*) user_stack[2];
      
      unsigned length = user_stack[3];
      validate_buffer(buffer, length); // validate the buffer pointer before accessing it
      f->eax = read(fd, buffer, length);
      break;
    }
    case SYS_WRITE:
    {
      validate_buffer(user_stack + 1,sizeof(int32_t));
      validate_buffer(user_stack + 2,sizeof(int32_t));
      validate_buffer(user_stack + 3,sizeof(int32_t));
      int fd = user_stack[1];
      const void* buffer = (const void*) user_stack[2];
      unsigned length = user_stack[3];
      validate_buffer(buffer, length); // validate the buffer pointer before accessing it
      f->eax = write(fd, buffer, length);
      break;
    }
    case SYS_SEEK:
    {
      validate_buffer(user_stack + 1,sizeof(int32_t));
      validate_buffer(user_stack + 2,sizeof(int32_t));
      int fd = user_stack[1];
      unsigned position = user_stack[2];
      seek(fd, position);
      break;
    }
    case SYS_TELL:
    {
      validate_buffer(user_stack + 1,sizeof(int32_t));
      int fd = user_stack[1];
      f->eax = tell(fd);
      break;
    }
    case SYS_CLOSE:
    {
      validate_buffer(user_stack + 1,sizeof(int32_t));
      int fd = user_stack[1];
      close(fd);
      break;
    }
    default:
    {
      exit(-1); // if the syscall number is invalid, exit with status -1
      break;
    }
  }
  // thread_exit ();
}

// DONE: All these functions need to be implemented with mutual exclusion
//       need to put semaphores around critical sections of code that access shared resources (e.g. file system, process table, etc.)

// DONE
/**
 * only calls shutdown_power_off()
 */
void
halt (void) {
  shutdown_power_off();
}

// TODO
/*
  Terminates the current user program, returning status to the kernel.
  If the process’s parent waits for it (see below), 
  this is the status that will be returned. 
  Conventionally, a status of 0 indicates success and nonzero values indicate errors.
  The kernel should print a message like thread_name(): exit(status), 
  where status is the value passed in by the user program. 
  The kernel should free any resources used by the process.
*/
void exit (int status) {
  struct thread *cur = thread_current();
  struct child_process *info = cur->my_info;
  
  // NEW: Store the exit status in the shared child_process struct.
  if (info != NULL) {
    info->exit_status = status;
    info->has_exited = true;
  }


  /* Print the required termination */
  /* Format: "process_name: exit(status)" */
  printf("%s: exit(%d)\n", cur->name, status);

  /* Perform actual thread termination */
  /* This will trigger process_exit() via the thread scheduler */
  thread_exit();
}


pid_t
exec (const char *file) {

  // NEW: create a child process struct to share information between the parent and the child
  struct child_process* child = malloc(sizeof(struct child_process));
  if (child == NULL)
    return -1;

  
  pid_t pid = process_execute(file);

  if (pid == TID_ERROR)
  {     
    return -1;
  }

  // list_push_back(&thread_current()->children, &child->elem); // when you come back run the test again 
  // probably this line the same thread has two different structs so the parent actually waits on one valid then on one dead 
  // and he end up waiting forever
  // sema_down(&child -> sema);   // parent waits until child finishes loading
  // this line was causing issues 
  // because two distict structures was being created 
  // and the parent waits on two different semaphores, which causes the parent to wait indefinitely

  // parent and child run concurrently, so we need to check if the child loaded successfully or not

  // child loaded unsuccessful
  if (pid == -1) {
    return -1;
  }

  return pid;
}

// TODO
// THIS IS THE HARDEST ONE
/**
* Waits for a child process pid and retrieves the child’s exit status.
* 1. If pid is still alive, waits until it terminates. 
*   Then, returns the status that pid passed to exit. 
* 2. If pid did not call exit(), 
*   but was terminated by the kernel (e.g. killed due to an exception), wait(pid) must return -1.
* 3. If pid is invalid or if it was not a child of the calling process,
*   or if process_wait() has already been successfully called for the given pid, wait(pid) must return -1 immediately, without waiting.

* NOTE: It is perfectly legal for a parent process to wait for child processes 
* that have already terminated by the time the parent calls wait, 
* but the kernel must still allow the parent to retrieve its child’s exit status, or
* learn that the child was terminated by the kernel.
*/
int
wait (pid_t pid) {
  return process_wait(pid);
}

// DONE
/*
  Creates a new file called file initially initial size bytes in size. 
  Returns true if successful, false otherwise. 
  Creating a new file does not open it: 
  opening the new file is
  a separate operation which would require a open system call.
*/
bool
create (const char *file, unsigned initial_size) {
  lock_acquire(&file_lock); // acquire the global file system lock
  bool success = filesys_create(file, initial_size);
  lock_release(&file_lock); // release the global file system lock
  return success;
}

// DONE
bool
remove (const char *file) {
  lock_acquire(&file_lock); // acquire the global file system lock
  bool success = filesys_remove(file);
  lock_release(&file_lock); // release the global file system lock
  return success;
}

// DONE: 1. synchronization
//      2. need to maintain a file descriptor table for each process, which maps file descriptors to open files.
int
open (const char *file) {

  lock_acquire(&file_lock); // acquire the global file system lock
  struct file* f = filesys_open(file);
  lock_release(&file_lock);  // release the global file system lock

  if (f == NULL) {
    return -1;
  }

  // actually add the file to the file descriptor table of the current process and return the file descriptor
  int fd = thread_add_file(f);
  
  if (fd == -1) {
    lock_acquire(&file_lock); // acquire the global file system lock
    file_close(f);
    lock_release(&file_lock); // release the global file system lock
    return -1;
  }
  // lock_release(&file_lock); // release the global file system lock
  return fd;
  
}

// DONE
/**
 * Returns the file size in bytes */
int
filesize (int fd) {
  // convert file descriptor to file pointer
  lock_acquire(&file_lock); // acquire the global file system lock
  struct file* file = thread_get_file(fd);
  lock_release(&file_lock); // release the global file system lock

  if (file == NULL) {
    return -1;
  }
  return file_length(file);
}

/**
 * Reads size bytes from file into buffer */
int
read (int fd, void *buffer, unsigned size) {
  // read from console
  if (fd == 0) { // STDIN
    uint8_t* buf = buffer;
    for (unsigned i = 0; i < size; i++) {
      buf[i] = input_getc();
    }
    return size;
  }

  lock_acquire(&file_lock); // acquire the global file system lock
  struct file* file = thread_get_file(fd);
  if (file == NULL) {
    lock_release(&file_lock); // release the global file system lock
    return -1;
  }
  int bytes = file_read(file, buffer, size);
  lock_release(&file_lock); // release the global file system lock
  return bytes;
}

// DONE: synchronization
/**
 * Writes size bytes from buffer into file */
int
write (int fd, const void *buffer, unsigned size) {

  if (fd == 0) return 0; // cannot write to STDIN
  // write to console
  if (fd == 1) { // STDOUT
    putbuf(buffer, size);
    return size;
  }
  lock_acquire(&file_lock); // acquire the global file system lock
  struct file* file = thread_get_file(fd);
  lock_release(&file_lock); // release the global file system lock

  if (file == NULL) {
    return 0; // no data written
  }
  lock_acquire(&file_lock); // acquire the global file system lock
  off_t bytes_written = file_write(file, buffer, size);
  lock_release(&file_lock); // release the global file system lock
  return bytes_written;
}

// DONE
/**
 * Sets the file position to position */
void
seek (int fd, unsigned position) {

  lock_acquire(&file_lock); // acquire the global file system lock
  struct file* file = thread_get_file(fd);
  lock_release(&file_lock); // release the global file system lock

  if (file == NULL) {
    return;
  }
  lock_acquire(&file_lock); // acquire the global file system lock
  file_seek(file, position);
  lock_release(&file_lock); // release the global file system lock
}

// DONE
/**
 * Returns the current file position from the start of the file */
unsigned
tell (int fd) {

  lock_acquire(&file_lock); // acquire the global file system lock
  struct file* file = thread_get_file(fd);
  lock_release(&file_lock); // release the global file system lock

  if (file == NULL) {
    return -1;
  }
  lock_acquire(&file_lock); // acquire the global file system lock
  unsigned position = file_tell(file);
  lock_release(&file_lock); // release the global file system lock
  return position;
}

// DONE
/**
 * Closes file descriptor */
void
close (int fd) {

  lock_acquire(&file_lock); // acquire the global file system lock
  struct file* file = thread_get_file(fd);
  lock_release(&file_lock); // release the global file system lock

  if (file == NULL) {
    return;
  }
  // DONE : need to remove the file from the file descriptor table of the current process
  lock_acquire(&file_lock); // acquire the global file system lock
  
  thread_close_file(fd); // remove the file from the file descriptor table of the current process
  lock_release(&file_lock); // release the global file system lock
}

/* --- pointers validation --- */

void validate_pointer(const void* ptr) {
  /* Check if the pointer is in user space and mapped */
  if (ptr == NULL || !is_user_vaddr(ptr) || pagedir_get_page(thread_current()->pagedir, ptr) == NULL) {
    exit(-1);
  }
}

void validate_buffer(const void* ptr, unsigned size) {
  if (size == 0) return;
  
  const char *start = (const char *)ptr;
  const char *end = start + size - 1;

  /* Validate the start and end of the buffer */
  validate_pointer(start);
  validate_pointer(end);

  /* If the buffer spans multiple pages, check each page boundary */
  for (const char *p = pg_round_up(start); p < end; p += PGSIZE) {
    validate_pointer(p);
  }
}
/* use me 🥺 i am really sad for you 
void validate_buffer(const void *addr, unsigned size) {
    if (addr == NULL || !is_user_vaddr(addr) || !is_user_vaddr(addr + size - 1)) exit(-1);
    for (uint8_t *p = (uint8_t *)pg_round_down(addr); p <= (uint8_t *)addr; p += PGSIZE)
        if (pagedir_get_page(thread_current()->pagedir, p) == NULL) exit(-1);
}
*/

void validate_string(const char *str) {
  validate_pointer(str); // Check the start
  while (true) {
    // We check the byte before reading it to avoid the fault
    if (pagedir_get_page(thread_current()->pagedir, str) == NULL) {
        exit(-1);
    }
    if (*str == '\0') break;
    str++;
    if (!is_user_vaddr(str)) exit(-1);
  }
}