#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"
#include "process.h"
#include <stdbool.h>

void syscall_init (void);
typedef int pid_t;

// NEW: global lock for file system operations
extern struct lock file_lock; // global lock for file system operations

// struct for child process information
struct child_process {
    tid_t pid; // Process ID of the child
    int exit_status; // Exit status of the child process
    bool has_exited; // Flag to indicate if the child has exited
    bool parent_waiting; // Flag to indicate if the parent is waiting on this child
    bool was_waited_on; // Flag to indicate if wait() has already been called on this child
    bool parent_is_dead; // Flag to indicate if the parent has already exited

    struct semaphore sema;  // Semaphore for synchronizing with the parent process
    struct thread *child;
    struct thread *parent; // Pointer to parent thread
    struct list_elem elem; // List element for child process list

};

/* syscalls */
// void halt (void);
// void exit (int status);
// pid_t exec (const char *cmd_line);
// int wait (pid_t pid);
// bool create (const char *file, unsigned initial_size);
// bool remove (const char *file);
// int open (const char *file);
// int filesize (int fd);
// int read (int fd, void *buffer, unsigned size);
// int write (int fd, const void *buffer, unsigned size);
// void seek (int fd, unsigned position);
// unsigned tell (int fd);
// void close (int fd);


#endif /* userprog/syscall.h */
