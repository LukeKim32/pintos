#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "lib/stdio.h"
#include "threads/malloc.h"

#define NOT_PROPER_TERMINATION -1
#define PROPER_TERMINATION 1
#define MAIN_THREAD 0

#define MAX_ARGUMENT_NUMBER 256
#define COMMAND 0

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;
  char *ptr;
  
  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

	/* Check if argument "file_name" is valid*/
  char fileNameBuffer[200];
  strlcpy(fileNameBuffer, file_name, strlen(file_name) + 1);
  char * parsedName = strtok_r(fileNameBuffer," ",&ptr);

  struct file * fileValidity = filesys_open(parsedName);
  if(fileValidity == NULL)
   return NOT_PROPER_TERMINATION;

  /* Create a new thread to execute FILE_NAME. */

  // printf("프로세스 엑세큐트 : 실행시킨 파일 이름 : %s\n",file_name);
  
  tid = thread_create (parsedName, PRI_DEFAULT, start_process, fn_copy);

  // 자식보다 부모 쓰레드가 먼저 끝나는 것을 방지하기 위해
  // 자식의 Load 완료 까지 기다림
  sema_down(&(thread_current()->lockForChildLoad));


  /**자식 쓰레드의 정상 생성/실패 확인해야함. 
  *실패 시 => 1) 자식쓰레드는 exit을 호출하며 '자신'의 동적할당 해제(thread_exit()) 
  *         2) 부모의 리스트에서 자신도 지워야 한다. (부모의 process_wait())
  */

  struct list_elem * eachChildStatusLink = list_begin(&thread_current()->childStatusList);
  int childProcessListLength = list_size(&thread_current()->childStatusList);

  int i=0;
  struct thread * targetChildThread = NULL;
  struct childStatus * eachChildStatusNode;

  for(;i < childProcessListLength;i++){
    
    eachChildStatusNode = list_entry(eachChildStatusLink,struct childStatus, self);

    /** 현재 쓰레드의 자식 쓰레드들에서 일치하는 tid를 찾은 경우
    * 자식이 오류로 이미 process_exit()에서 다시 잠들었어도, 부모의 리스트에 쓰레드 생성 시 (thread_create()) 추가했기 때문에 무조건 있다.
    */
    if(eachChildStatusNode->childTid == tid){
      targetChildThread = eachChildStatusNode->threadItself;
      break;
    }

    eachChildStatusLink = list_next(eachChildStatusLink);
  }

  /** 찾은 자식이 start_process()에서 Load fail => 종료되어야 해서 이미 process_exit()에서 기다리고 있는 경우 (zombie)
   * 부모는 wait를 호출 => 1)자식의 exit Status 값 받음, 2) 자식을 종료 & 리스트에서 제거 & 해제
   * loadFailure 초기값 : true.
   * start_process()에서 Load 성공해야 false로 변경
   */ 
  // if(targetChildThread->loadFailure){
  if(targetChildThread->loadFailure){
    return process_wait(tid); //자식을 종료 & 리스트에서 제거 & 해제
  }


  if (tid == TID_ERROR)
    palloc_free_page (fn_copy); 
  
  return tid;
}


/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
  char *arguments[MAX_ARGUMENT_NUMBER]; 
  char *argument;
  char *ptr;
  int i;
  
  // file_name에는 run 뒤의 명령어가 전부 저장됨 ex)echo x
  // 따라서 file_name을 공백 기준으로 parse 하여 arguments 배열에 저장
  // ex) arguments = ["echo", "x"]

  // parseInput(file_name, arguments); 

  /** parseInput()내 동적할당 메모리 해제 타이밍 모호 => 로컬 변수로 설정*/
  char fileNameBuffer[strlen(file_name)+1];
  strlcpy(fileNameBuffer,file_name,strlen(file_name)+1);

  arguments[COMMAND] = strtok_r(fileNameBuffer, " ", &ptr);// command; ex) echo

  // get rest of arguments
  for(i = 1; ; i++){
    argument = strtok_r(NULL, " ", &ptr);
    if (argument == NULL) {
      break;
    }
    arguments[i] = argument;
  }

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  
  success = load (arguments[COMMAND], &if_.eip, &if_.esp);

  if(success){
      BuildStackWith(arguments, &if_.esp);
      thread_current()->loadFailure = false;
  }

  /* If load failed, quit. */
  palloc_free_page (file_name);

  /** 자식보다 부모 쓰레드가 먼저 끝나는 것을 방지하기 위해.
  * 자신을 기다리고 있는 부모 쓰레드를 재개해준다.
  */
  struct thread * parentThread = thread_current()->parent;
  sema_up(&(parentThread->lockForChildLoad));

  if (!success) {
    thread_exit (); //이 안에서 우리가 생각 못했던 종료한 경우 -> 말끔히 지워야 한다. 리스트 할당한거 파일디스크립터 할당한거.
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}



/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{

  struct list_elem *eachChildStatusLink;
  struct childStatus * eachChildStatusNode;
  struct childStatus * targetChildStatusNode;
  struct thread * targetChildThread = NULL;
  
  int exitStatus = -1;

  eachChildStatusLink = list_begin(&thread_current()->childStatusList);
  int childStatusListLength = list_size(&thread_current()->childStatusList);
  int i;
  for(i=0;i < childStatusListLength;i++){
    
    eachChildStatusNode = list_entry(eachChildStatusLink,struct childStatus, self);

    // 무조건 자식 쓰레드의 메타데이터인 childStatusNode를 찾을 것이다. (자식의 정상 수행/종료 여부와 상관없이)
    if(eachChildStatusNode->childTid == child_tid){
      targetChildThread = eachChildStatusNode->threadItself;
      targetChildStatusNode = eachChildStatusNode; // for Readability
      break;
    }

     eachChildStatusLink = list_next(eachChildStatusLink);
  }

  // Exception Handling : 없는 자식쓰레드에 대해 wait()을 호출한 경우
  if(targetChildThread == NULL){
    return NOT_PROPER_TERMINATION;
  }


  /** 현 시점 상황: 자신(부모쓰레드)이 wait할 자식 쓰레드를 가리키는 포인터를 찾았다*/

  // 자식의 실행을 기다리기 위해 sleep
  // 자식이 이미 기능 수행 완료(실제로는 process_exit()에서 sleep 중) => lockForChildExecute + 1 이 되어있으므로 통과
  sema_down(&(thread_current()->lockForChildExecute));

  /** 현 시점 상황: 자식은 다 수행하고, syscall/exit()을 호출 => 현재쓰레드(부모)를 깨우고, 자신은 sleep*/

  exitStatus = targetChildStatusNode->exitStatus;

  // 자식 Status 노드(메타데이터)를 자신(부모)의 ChildStatus Linked List에서 제외&해제한다.
  list_remove(&(targetChildStatusNode->self));
  free(targetChildStatusNode);

  // 자식쓰레드가 exit()이 마저 실행되어 사라지도록 sleep에서 깨워준다
  sema_up(&(targetChildThread->alarmByParent));

  return exitStatus;
}


/* Free the current process's resources. */
void
process_exit (void)
{
  
  struct thread *cur = thread_current ();
  uint32_t *pd;

  // 자신(자식쓰레드)의 수행이 완료되었으므로, 부모쓰레드의 sleep을 깨운다.
  struct thread * parentThread = thread_current()->parent;
  sema_up(&(parentThread->lockForChildExecute));


  // 자신(자식쓰레드)는 완전히 종료되기 전 부모쓰레드가 허용할 때 까지 잠시 sleep.
  sema_down(&(cur->alarmByParent));

  /** 현 시점 상황 : 부모가 자신(자식쓰레드)의 파괴를 허용함. 자신의 동적할당 메모리를 해제할 것임(thread_exit()에서 진행)*/

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}


/** Kernel에 입력받은 명령어를 파싱
 * Ex. 'echo x' => 'echo', 'x'
 */
void parseInput(char *file_name, char *arguments[]){
  char *fileNameBuffer;
  char *argument;
  char *ptr;
  int i;
  
  fileNameBuffer = (char *)malloc(sizeof(char)*(strlen(file_name)+1));
  strlcpy(fileNameBuffer,file_name,strlen(file_name)+1);
  
  arguments[COMMAND] = strtok_r(fileNameBuffer, " ", &ptr);// command; ex) echo

  // get rest of arguments
  for(i = 1; ; i++){
    argument = strtok_r(NULL, " ", &ptr);
    if (argument == NULL) {
      break;
    }
    arguments[i] = argument;
  }
}

/** @arguments 와 관련 data를 Stack에 할당 
 */
void BuildStackWith(char *arguments[], void **esp) {
  int lenOfArgument = 0; // 각 argument의 길이
  int lenOfTotalArguments = 0; // 모든 argument의 길이 합 ; word align 수 계산에 사용
  int numArguments = 0; // argument들의 개수
  int numAlign; // word align 해야 할 개수
  int i;
  
  // count number of arguments
  for (i = 0; ; i++){
    if(arguments[i] == NULL){
      break;
    }
    numArguments += 1;
  }


  // arguments[n] ~ arguments[0] 저장
  // Data : 'argument\0'
  uint32_t *addressOfArgumentsInStack = (uint32_t *)malloc(sizeof(uint32_t)*numArguments);

  for(i = numArguments - 1; i >= 0; i--){
    lenOfArgument = strlen(arguments[i]);
    *esp -= (lenOfArgument + 1); // 끝에 NULL 값이 있으므로 +1
    strlcpy(*esp, arguments[i], lenOfArgument + 1);
    addressOfArgumentsInStack[i] = (uint32_t)*esp;
    lenOfTotalArguments += (lenOfArgument + 1);
  }

  // word align 저장
  // Data : 0 (uint8_t 타입, 1바이트)
  numAlign = 4 - (lenOfTotalArguments % 4);
  while (numAlign != 4 && numAlign > 0) {
    *esp -= 1;
    **(uint8_t **)esp = 0;
    numAlign--;
  }

  // NULL 저장
  // Data : 0 (uint32_t 타입, 4바이트의 NULL 값)
  *esp -= 4;
  **(uint32_t **)esp = 0;

  // arguments[n] ~ arguments[0]의 스택의 주소 저장
  for(i = numArguments - 1; i >= 0; i--){
    *esp -= 4;
    **(uint32_t **)esp = addressOfArgumentsInStack[i];
  }

  // arguments의 주소 저장
  *esp -= 4;
  **(uint32_t **)esp = (uint32_t)(*esp + 4);

  // arguments의 개수 저장
  *esp -= 4;
  **(uint32_t **)esp = numArguments;

  // return 주소 저장
  // Data : 0
  *esp -= 4;
  **(uint32_t **)esp = 0;

  free(addressOfArgumentsInStack);

}


/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofset;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofset = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofset < 0 || file_ofset > file_length (file))
        goto done;
      file_seek (file, file_ofset);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofset += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *knpage = palloc_get_page (PAL_USER);
      if (knpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, knpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (knpage);
          return false; 
        }
      memset (knpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, knpage, writable)) 
        {
          palloc_free_page (knpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *th = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (th->pagedir, upage) == NULL
          && pagedir_set_page (th->pagedir, upage, kpage, writable));
}


/** User-defined function area */

int getExitStatusOfChildBy(tid_t child_tid, struct thread * thread){
  
  int exitStatus=-1;

  struct list_elem * statusListElem = list_begin(&thread->childStatusList);
  int childStatusListLength = list_size(&thread->childStatusList);
  struct childStatus * eachChildStatusNode;

  int eachChildIndex;  
  for(eachChildIndex=0;eachChildIndex < childStatusListLength;eachChildIndex++){
    
    eachChildStatusNode = list_entry(statusListElem,struct childStatus, self);

    if(eachChildStatusNode->childTid == child_tid){
      // printf("겟엑싯스태터스 - child_tid : %d\n",child_tid);
      exitStatus = eachChildStatusNode->exitStatus;
      list_remove(statusListElem);
      
      free(eachChildStatusNode);
      break;
    }

    statusListElem = list_next(statusListElem);
  }
  return exitStatus;
}

void findChildAndListenFinish(struct thread *eachThread, void *aux){
  tid_t childTid = ((tid_t *)aux)[1];
  if(eachThread->tid == childTid){
    printf("쓰레드 %s finish 리스닝\n",eachThread->name);
    while(1){
         if(isThreadDead(eachThread)){
          printf("쓰레드 끝남!\n");
          ((tid_t *)aux)[0] = PROPER_TERMINATION;
          break;
        }
      }
  }
}

bool checkThreadFinishedByStatus(int status){
  // printf("THread status check : %d\n",status);
  switch(status){
    case THREAD_DYING:
      return true;
    
    default:
      return false;
  }
}
