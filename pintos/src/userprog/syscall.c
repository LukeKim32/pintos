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


#define STDIN 0
#define STDOUT 1
#define NOT_PROPER_ACCESS -1
#define POSITIVE 0
#define NEGATIVE 1
#define NOT_A_NUMBER -2147483647
#define MAXIMUM_STRING_LENGTH 256

static void syscall_handler(struct intr_frame *);

void syscall_init(void)
{
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
  int inputs[4];
  int i;
  // printf("시스템 콜 넘버 : %d\n",*(uint32_t *)(f->esp));
  // hex_dump(f->esp, f->esp, 100, 1); 
  checkValidUserVirtualAddress(f->esp+4); // 항상 처리 써주기
  
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

   struct thread * parentThread = list_entry(childThread->parent,struct thread,self);
   tid_t childTid = childThread->tid;

   struct list_elem * eachChild = list_begin(&parentThread->childStatusList);
   int childProcessListLength = list_size(&parentThread->childStatusList);
   struct childStatus * eachChildStatusNode;

   int eachChildIndex;
  
   for(eachChildIndex=0;eachChildIndex < childProcessListLength;eachChildIndex++){
    
    eachChildStatusNode = list_entry(eachChild,struct childStatus, self);
    // printf("for loop tid : %d, child_tid : %d\n",eachThread->tid,child_tid);
    // printf("Loop 쓰레드 이름 : %s\n",eachThread->name);
    if(eachChildStatusNode->childTid == childTid){
       eachChildStatusNode->exitStatus = exitStatus;

       //printf("\n%d 쓰레드 종료 - 상태 %d 저장\n",childTid,exitStatus);
    }

    eachChild = list_next(eachChild);
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


/** InputStream : uint32_t
 * input_getc()로 부터 STDIN 입력 => inputStream(=input Buffer)에 저장
 */
int read (int fileDescriptor, void *inputStream, unsigned size){

 unsigned eachByte;
  switch(fileDescriptor){
    case STDIN:
      // Loop 돌면서 input에 엔터가 들어왔을 때 탈출을 해야하는건지..?

      eachByte=0;
      for(; eachByte<size ; eachByte++){
        ((uint8_t *)inputStream)[eachByte]=input_getc();
      }

      return (int)eachByte;

    default :
      return NOT_PROPER_ACCESS;
  }
}

/** OutputStream : uint32_t
 * outputStream(=output Buffer)에서 값을 읽어와 fileDescriptor에 출력
 */
int write (int fileDescriptor, const void *outputStream, unsigned size){
  // unsigned eachByte;
  // uint8_t singleOutputByte;

  switch(fileDescriptor){
    case STDOUT:
      //스택에 쌓아놓은 argument ex. echo 'x'

      //===========input_putc error!!====
      // eachByte=0;
      //  enum intr_level interruptLevel = intr_disable();
      // for(; eachByte<size ; eachByte++){
      //   singleOutputByte = ((uint8_t*)outputStream)[eachByte];
      //   input_putc(singleOutputByte);
      //   printf("출력 : %c\n",singleOutputByte);
      // }
      // intr_set_level(interruptLevel);
      // return (int)eachByte;

      putbuf(outputStream, size);
      return (int)size;

    default :
      return NOT_PROPER_ACCESS;

  }
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

