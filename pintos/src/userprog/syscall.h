#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "lib/user/syscall.h"
#include "threads/thread.h"

void syscall_init (void);
void halt (void);
void exit (int status);
pid_t exec (const char *cmd_line);
int open (const char *file);
int read (int fileDescriptor, void *buffer, unsigned size);
int write (int fileDescriptor, const void *outputStream, unsigned size);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int filesize (int fd);
void close (int fd);
void seek (int fd, unsigned position);
unsigned tell (int fd);

struct file * getFilePointerFrom(int fileDescriptorNumber);
struct fileDescriptor * getFileDescriptorFrom(int fileDescriptorNumber);

int fibonacci(int n);
int sum_of_four_int(int a, int b, int c, int d);
void checkValidUserVirtualAddress(const void* vaddr);
void notifyTerminationToParent(struct thread *childThread, int exitStatus);
int allocateNewFileDescriptor(struct file * file);

#endif /* userprog/syscall.h */
