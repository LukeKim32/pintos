#include "userprog/syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include <string.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "lib/user/syscall.h"
#include "threads/synch.h"


#define STDIN 0
#define STDOUT 1
#define NOT_PROPER_ACCESS -1
#define POSITIVE 0
#define NEGATIVE 1
#define NOT_A_NUMBER -2147483647
#define MAXIMUM_STRING_LENGTH 256
#define FILE_NAME_LENGTH_LIMIT 14
#define FIRST_FILE_DESCRIPTOR 3

static void syscall_handler(struct intr_frame *);

struct lock fileSystemUseLock;

/* An open file. */
struct file 
  {
    struct inode *inode;        /* File's inode. */
    off_t pos;                  /* Current position. */
    bool deny_write;            /* Has file_deny_write() been called? */
  };


void syscall_init(void)
{
  lock_init(&fileSystemUseLock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}


static void
syscall_handler(struct intr_frame *f UNUSED)
{
  int status;
  char * fileName;
  pid_t childPid;
  int fileDescriptor;
  uint32_t inputStream;
  uint32_t outputStream;
  unsigned size;
  int targetIndex;
  unsigned initialSize;
  unsigned position;
  int inputs[4];
  int i;

  checkValidUserVirtualAddress(f->esp+4); 
  
  switch (*(uint32_t *)(f->esp)){

    case SYS_HALT:
      halt();
      break;

    case SYS_EXIT:
      status= (int)(*(uint32_t *)(f->esp + 4));
      exit(status);
      f->eax = (int)(*(uint32_t *)(f->esp + 4));
      break;

    case SYS_EXEC:
      fileName = (char *)*(uint32_t *)(f->esp + 4);
      f->eax = exec(fileName);
      break;

    case SYS_WAIT:
      childPid = (pid_t)*(uint32_t *)(f->esp + 4);
      f->eax = wait(childPid);
      break;

    case SYS_CREATE:                 
      checkValidUserVirtualAddress(f->esp + 8);
      fileName = (char *)*(uint32_t *)(f->esp + 4);
      initialSize = (unsigned)*(uint32_t *)(f->esp + 8);
      f->eax = create(fileName, initialSize);
      break;

    case SYS_REMOVE:              
      fileName = (char *)*(uint32_t *)(f->esp + 4);
      f->eax = remove(fileName);
      break;

    case SYS_OPEN:                  
      fileName = (char *)*(uint32_t *)(f->esp + 4);
      f->eax = open(fileName);
      break;

    case SYS_FILESIZE:
      fileDescriptor = (int)*(uint32_t *)(f->esp + 4);
      f->eax = filesize(fileDescriptor);
      break;

    case SYS_SEEK:
      checkValidUserVirtualAddress(f->esp + 8);
      fileDescriptor = (int)*(uint32_t *)(f->esp + 4);
      position = (unsigned)*(uint32_t *)(f->esp + 8);
      seek(fileDescriptor, position);
      break;

     case SYS_TELL:
      checkValidUserVirtualAddress(f->esp + 4);
      fileDescriptor = (int)*(uint32_t *)(f->esp + 4);
      f->eax = tell(fileDescriptor);
      break;

    case SYS_CLOSE:
      fileDescriptor = (int)*(uint32_t *)(f->esp + 4);
      close(fileDescriptor);
      break;

    case SYS_READ:
      //유저 스택 arguments에서 추출
      checkValidUserVirtualAddress(f->esp+8);
      checkValidUserVirtualAddress(f->esp+12); 

      fileDescriptor = (int)*(uint32_t *)(f->esp + 4);
      inputStream = *(uint32_t *)(f->esp + 8);
      size = (unsigned)*((uint32_t *)(f->esp + 12));

      f->eax = read(fileDescriptor,(void *)inputStream,size);
      break;

    case SYS_WRITE:
      //유저 스택 arguments에서 추출
      checkValidUserVirtualAddress(f->esp+8); 
      checkValidUserVirtualAddress(f->esp+12); 

      fileDescriptor = (int)*(uint32_t *)(f->esp + 4);
      outputStream = *(uint32_t *)(f->esp + 8);
      size = (unsigned)*((uint32_t *)(f->esp + 12));

      f->eax = write(fileDescriptor,(void *)outputStream,size);
      break;

    case FIBONACCI:
      targetIndex = (int)*(uint32_t *)(f->esp + 4);     
      f->eax = fibonacci(targetIndex);
      break;

    case SUM_OF_FOUR_INT:
      for(i=0;i<4;i++){
        inputs[i] = (int)*(uint32_t *)(f->esp + 4*(i+1));
      }

      f->eax = sum_of_four_int(inputs[0],inputs[1],
                                inputs[2],inputs[3]);
      break;
  }

}


/** Check the stack pointer is properly pointing 
 * user stack.
 * If not, exit(-1)
*/
void checkValidUserVirtualAddress(const void* vaddr){
  if(!is_user_vaddr (vaddr)){
    exit(NOT_PROPER_ACCESS);
  }
} // 커널모드는 모든 메모리에 접근가능한데(현재 커널모드), 우리가 필요한 데이터는 유저메모리에 있으므로
// 현재 접근중인 메모리가 유저메모리인지 확인해야 한다. 애초에 커널메모리는 참조할 이유가 없다.


/** Exit thread showing status*/
void exit (int status){
  char *threadName;
  char stringBuffer[MAXIMUM_STRING_LENGTH];
  char *ptr;

  strlcpy(stringBuffer,thread_name(),strlen(thread_name())+1);
  stringBuffer[strlen(thread_name())] = '\0';
  threadName = strtok_r(stringBuffer," ",&ptr);

  printf("%s: exit(%d)\n", threadName, status); // thread_name() 파싱을 해주어야한다!
  
	notifyTerminationToParent(thread_current(),status);

  thread_exit(); //여기서 이 쓰레드가 할당 해제될 것이다 (schedule())
}

/** @parameter
 * @childThread 자식 쓰레드
 * @exitStatus 자식 쓰레드 종료 상태
 * 자식 쓰레드의 부모 쓰레드에게 자식의 종료 상태를 알려주는 메소드
*/
void notifyTerminationToParent(struct thread *childThread, int exitStatus){

   struct thread * parentThread = childThread->parent;
   tid_t childTid = childThread->tid;

   struct list_elem * eachChildStatusLink = list_begin(&parentThread->childStatusList);
   int childProcessListLength = list_size(&parentThread->childStatusList);
   struct childStatus * eachChildStatusNode;

   int i;
  
   for(i=0;i < childProcessListLength;i++){
    
    eachChildStatusNode = list_entry(eachChildStatusLink,struct childStatus, self);

    if(eachChildStatusNode->childTid == childTid){
       eachChildStatusNode->exitStatus = exitStatus;
       break;
    }

    eachChildStatusLink = list_next(eachChildStatusLink);
   }

}

void halt(void){
  shutdown_power_off();
}


pid_t exec (const char *cmd_line){
  return process_execute(cmd_line);
}


pid_t wait(pid_t childPid){
    return process_wait(childPid);
}


bool create (const char *file, unsigned initial_size) {
  if(file == NULL){
    exit(NOT_PROPER_ACCESS);
  }

  bool createSuccess = false;

  lock_acquire(&fileSystemUseLock);
  createSuccess = filesys_create(file, initial_size);
  lock_release(&fileSystemUseLock);

  return createSuccess;
}


bool remove (const char *file) {
  if(file == NULL){
    exit(NOT_PROPER_ACCESS);
  }

  bool removeSuccess = false;

  lock_acquire(&fileSystemUseLock);
  removeSuccess = filesys_remove(file);
  lock_release(&fileSystemUseLock);

  return removeSuccess;
}


void close (int fd) {

  lock_acquire(&fileSystemUseLock);

  struct fileDescriptor * targetFileDescriptor = getFileDescriptorFrom(fd);

  if(targetFileDescriptor == NULL){
    lock_release(&fileSystemUseLock);
    exit(NOT_PROPER_ACCESS);
  }

  struct file * targetFile = targetFileDescriptor->file;

  if(targetFile == NULL){
    lock_release(&fileSystemUseLock);
    exit(NOT_PROPER_ACCESS);
  }

  // 현재 쓰레드의 File Descriptor List에서 제거 및 해제
  list_remove(&(targetFileDescriptor->self));
  free(targetFileDescriptor);

  file_close(targetFile);

  lock_release(&fileSystemUseLock);

}


int filesize (int fd) {
  // fd를 통해 fileName을 알아야함
  struct file * targetFile = getFilePointerFrom(fd);

  if(targetFile == NULL){
    exit(NOT_PROPER_ACCESS);
  }

  return file_length(targetFile);
}

void seek (int fd, unsigned position) {

  struct file * targetFile = getFilePointerFrom(fd);

  if(targetFile == NULL){
    exit(NOT_PROPER_ACCESS);
  }

  file_seek(targetFile, position);
}


/** @FileDescriptor Number 값을 이용해 
 * 현재 쓰레드의 Open 되어 있는 File 포인터를 찾아 반환한다
*/
struct file * getFilePointerFrom(int fileDescriptorNumber){
  struct fileDescriptor * targetFileDescriptor = getFileDescriptorFrom(fileDescriptorNumber);
  
  struct file * targetFile = NULL;
  if(targetFileDescriptor != NULL){
    targetFile = targetFileDescriptor->file;
  }

  return targetFile;
}

/** @FileDescriptor Number 값을 이용해 
 * 현재 쓰레드의 Open 되어 있는 File에 대한 정보가 담겨있는
 * FileDescriptor 구조체를 찾아 반환한다
*/
struct fileDescriptor * getFileDescriptorFrom(int fileDescriptorNumber){
  struct thread * currentThread = thread_current();//list_entry(thread_current()->parent,struct thread,self);
  struct list * fileDescriptorList = &(currentThread->fileDescriptorList);

  struct list_elem * eachFileDescriptorLink = list_begin(fileDescriptorList);
  int fileDescriptorListLength = list_size(fileDescriptorList);
   
  int i;
  struct fileDescriptor * eachFileDescriptorNode = NULL;
  
   for(i=0;i < fileDescriptorListLength;i++){
    
    eachFileDescriptorNode = list_entry(eachFileDescriptorLink,struct fileDescriptor, self);

    if(eachFileDescriptorNode->number == fileDescriptorNumber){
      return eachFileDescriptorNode;
    }

    eachFileDescriptorLink = list_next(eachFileDescriptorLink);
   }

  return eachFileDescriptorNode;

}



int open (const char *file){
  
  if(file == NULL){
    exit(NOT_PROPER_ACCESS);
  }

  // File Name 길이가 14 초과
  if(strlen(file)>FILE_NAME_LENGTH_LIMIT){ 
    exit(NOT_PROPER_ACCESS);
  }

  checkValidUserVirtualAddress(file);

  lock_acquire(&fileSystemUseLock); // 아래서부터 Critical section - shared memory : fileSystem & 현재쓰레드의 fileDescriptorList

  struct file * targetFile = filesys_open(file);
  
  if(targetFile == NULL){
    lock_release(&fileSystemUseLock);
    return -1;
  }

  if (strcmp(thread_name(), file) == 0) {
    file_deny_write(targetFile);
  } 

  // fileDescriptor 값은 각 open할 file에 대해 순차적으로 할당되어야 하므로 CS 내부. (중간에 context switch 방지)
  int newFileDescriptorNumber = allocateNewFileDescriptor(targetFile);

  lock_release(&fileSystemUseLock);

  return newFileDescriptorNumber;
}


/** 현재 쓰레드의 open된 File들의 리스트에 대한 정보 = FileDescriptorList
 * 에서 가장 마지막 File Descriptor 값 + 1 을
 * 새 File Descriptor로 할당하며 리스트에 추가한다.
*/
int allocateNewFileDescriptor(struct file * file){
  
  struct thread * currentThread = thread_current();
  struct list * fileDescriptorList = &currentThread->fileDescriptorList;

  int newFileDescriptorNumber;

  if(list_empty(fileDescriptorList)){
    newFileDescriptorNumber = FIRST_FILE_DESCRIPTOR;

  }else { // list not empty, 마지막 fd 다음 값 확인

    struct list_elem * lastFileDescriptorNode = list_rbegin(&currentThread->fileDescriptorList);
    struct fileDescriptor * lastFileDescriptor = list_entry(lastFileDescriptorNode,struct fileDescriptor,self);

    // 새로운 FD 값 = 마지막 file Descriptor 값 + 1 로 할당
    newFileDescriptorNumber = lastFileDescriptor->number + 1;
  }

  struct fileDescriptor * newFileDescriptor = (struct fileDescriptor *)malloc(sizeof(struct fileDescriptor));
  newFileDescriptor->number = newFileDescriptorNumber;
  newFileDescriptor->file = file;

  list_push_back(&currentThread->fileDescriptorList,&(newFileDescriptor->self));

  return newFileDescriptorNumber;
}


/** InputStream : uint32_t
 * input_getc()로 부터 STDIN 입력 => inputStream(=input Buffer)에 저장
 */
int read (int fileDescriptor, void *inputStream, unsigned size){
  // printf("Read 들어옴!\n");
 unsigned eachByte;
 int finalBytesRead=0;
 struct file * targetFile;

 checkValidUserVirtualAddress(inputStream);
 
 if(fileDescriptor < 0 || inputStream == NULL){
    return NOT_PROPER_ACCESS;
  }

  lock_acquire(&fileSystemUseLock);

  switch(fileDescriptor){
    case STDIN:
      eachByte=0;
      for(; eachByte<size ; eachByte++){
        ((uint8_t *)inputStream)[eachByte]=input_getc();
      }
      
      finalBytesRead = (int)eachByte;
      break;

    case STDOUT :
      finalBytesRead = NOT_PROPER_ACCESS;
      break;

    default :
      targetFile = getFilePointerFrom(fileDescriptor);
      if(targetFile==NULL){
        exit(NOT_PROPER_ACCESS);
      }

      finalBytesRead = file_read(targetFile,inputStream,size);
      break;
  }

  lock_release(&fileSystemUseLock);
  return finalBytesRead;
}


/** OutputStream : uint32_t
 * outputStream(=output Buffer)에서 값을 읽어와 fileDescriptor에 출력
 */
int write (int fileDescriptor, const void *outputStream, unsigned size){

  struct file * targetFile;
  int finalBytesWritten = 0;

  checkValidUserVirtualAddress(outputStream);

  if(fileDescriptor < 0 || outputStream == NULL){
    return NOT_PROPER_ACCESS;
  }

  lock_acquire(&fileSystemUseLock);

  switch(fileDescriptor){
    case STDOUT:
      putbuf(outputStream, size);
      finalBytesWritten = (int)size;
      break;

    case STDIN :
      finalBytesWritten = NOT_PROPER_ACCESS;
      break;

    default :
      targetFile = getFilePointerFrom(fileDescriptor);
      if(targetFile==NULL){
        exit(NOT_PROPER_ACCESS);
      }

      finalBytesWritten= file_write(targetFile,outputStream,size);
      break;
  }

  lock_release(&fileSystemUseLock);
  return finalBytesWritten;
}

unsigned tell (int fd) {
  struct file * targetFile = getFilePointerFrom(fd);

  if(targetFile == NULL){
    exit(NOT_PROPER_ACCESS);
  }

  return file_tell(targetFile);
}


/** User-defined System call
 * Fibonacci dp
*/
int fibonacci(int n){
  
  if(n<0){ // Check Validity of N
    return NOT_PROPER_ACCESS;
  }

  int fibonacciSequence[n+1];
  int i; //loop variable

  fibonacciSequence[0]=0;

  if(n>0){
    fibonacciSequence[1]=1;
    for(i=2;i<=n;i++){
      fibonacciSequence[i] = fibonacciSequence[i-1] + fibonacciSequence[i-2];
    }
  }

  return fibonacciSequence[n];
}


/** User-defined system call2
*/
int sum_of_four_int(int a, int b, int c, int d){
  return a + b+ c+ d;
}

