#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "../filesys/file.h"
#include "../devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif


#define ONE_SECOND 100

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

#ifndef USERPROG
bool thread_prior_aging;
#endif

static int loadAvg;

#define READY_LIST_EMPTY -1

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
  initial_thread->recentCPU = 0;
  initial_thread->niceValue = 0;
  loadAvg = 0;


}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore start_idle;
  sema_init (&start_idle, 0);
  thread_create ("idle", PRI_MIN, idle, &start_idle);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&start_idle);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
/** timer.c / timer_interrupt() 에서 매번 불림 */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  if(thread_prior_aging || thread_mlfqs){
    enum intr_level previousInterruptLevel = intr_disable();

    thread_aging();

    intr_set_level(previousInterruptLevel);
  }

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();


}


void thread_aging(void){
  struct thread * currentThread = thread_current();

  if(currentThread != idle_thread)
    currentThread->recentCPU = sumOfFloatAndInt(currentThread->recentCPU,1);//1 더해주기
  

  // 4 tick 마다, 모든 쓰레드의 priority는 재계산된다.
  if(timer_ticks()% TIME_SLICE ==0){
    //update prioirity
    updatePriorityOfEachThread();

    // Priority update 후에 다시 스케줄링 검사
    int maxPriority = getMaxPriorityFromReadyQueue();
    if(currentThread->priority < maxPriority){
      intr_yield_on_return();
    }
  }

  // 1초(100 tick)마다, 모든 쓰레드의 recent CPU value & loadAvg update
  if(timer_ticks() % ONE_SECOND ==0){
    // update load avg and recent cpu
    updateLoadAvg();
    updateRecentCPU();
  }
  
}




/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

#ifdef USERPROG

  struct childStatus * newChildStatus = (struct childStatus *)malloc(sizeof(struct childStatus));
  newChildStatus->childTid = t->tid;
  newChildStatus->exitStatus = -1;
  newChildStatus->threadItself = t; // status 노드는 자신의 thread도 가리킨다
  list_push_back(&running_thread()->childStatusList,&(newChildStatus->self));
#endif

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);

  /* Add to run queue. */
  thread_unblock (t);

  int priorityOfRunningThread = thread_get_priority();
  int priorityOfNewCreatedThread = priority;

  /** 새로 생성한 쓰레드의 Priority가 더 큰 경우, 리스케줄링을한다.*/
  if(priorityOfRunningThread < priorityOfNewCreatedThread){
    thread_yield();
  }

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);

  list_insert_ordered (&ready_list, &t->elem, comparePriorityOf, NULL);
  
  t->status = THREAD_READY;

  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  // thread_current()->terminated = true;
  // printf("쓰레드 %s 종료!\n",thread_current()->name);
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
  
#endif

  
  // 현재 종료될 쓰레드의 동적할당 메모리를 모두 해제한다.
  struct thread * curThread = thread_current();
  cleanMemoryOfCurrentThread(curThread);

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/** @curThread 로 넘겨받은 쓰레드 객체의 동적 할당 메모리를 모두 해제한다. 
*/
void cleanMemoryOfCurrentThread(struct thread * curThread){
  struct thread * currentThread = curThread;

  int i;

  //Loop setting
  struct list_elem * tmpLink = list_begin(&currentThread->childStatusList);
  struct childStatus * tmpChildStatusNode;
  struct list_elem * nextTmpLink = tmpLink;

  // Delete Child Status List
  int childStatusListLength = list_size(&currentThread->childStatusList);
  for(i=0;i < childStatusListLength;i++){
    tmpLink = nextTmpLink;
    tmpChildStatusNode = list_entry(tmpLink,struct childStatus, self);
    
    nextTmpLink = list_next(nextTmpLink);

    list_remove(tmpLink);
    free(tmpChildStatusNode);
  }

  //Loop setting
  tmpLink = list_begin(&currentThread->fileDescriptorList);
  nextTmpLink = tmpLink;
  struct fileDescriptor * fileDescriptorNode;

  // Delete FileDescriptor List
  int fileDescriptorListLength = list_size(&currentThread->fileDescriptorList);
  for(i=0;i < fileDescriptorListLength;i++){

    tmpLink = nextTmpLink;
    fileDescriptorNode = list_entry(tmpLink,struct fileDescriptor, self);
#ifdef USERPROG
    file_close(fileDescriptorNode->file);
#endif

    nextTmpLink = list_next(nextTmpLink);

    list_remove(tmpLink);
    free(fileDescriptorNode);
  }

}


/**Returns TRUE if first 쓰레드의 priority > second 쓰레드의 Priority*/
bool comparePriorityOf(const struct list_elem* first, const struct list_elem* second, void* aux) {
 
  struct thread *firstThread = list_entry(first, struct thread, elem);
  struct thread *secondThread = list_entry(second, struct thread, elem);

  /** Useless code for delete warning */
  char *noWarning = aux;
  noWarning = noWarning;
  
  bool result = false;
  if(firstThread->priority > secondThread->priority){
    result = true;
  }
  
  return result;
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *currentThread = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();

  if (currentThread != idle_thread) 
    // Priority 값으로 sort해서 넣는다 => Ready Queue의 Priority가 내림차순이다
    list_insert_ordered (&ready_list, &currentThread->elem, comparePriorityOf, NULL);
  
  currentThread->status = THREAD_READY;

  schedule ();

  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}


/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  if(thread_mlfqs){
    return;
  }

  // 현재 돌고 있는 쓰레드(기존 최고 Priority) Update
  thread_current()->priority = new_priority;

  //새로 update한 priority는 더이상 최고 Priority가 아닐 수도 있다
  //레디큐의 최대값과 비교해본다
  int maxPriorityAmongReadyQueue = getMaxPriorityFromReadyQueue();

  // 더이상 최고 Priority가 아닌 경우, 리스케줄링을 한다
  if(maxPriorityAmongReadyQueue > new_priority){
    thread_yield();
  }
  
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}


/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/** 쓰레드가 invalid => return true */
bool isThreadDead(struct thread *threadForCheck){
  return !(threadForCheck != NULL && threadForCheck->magic == THREAD_MAGIC);
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);


  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  list_push_back (&all_list, &t->allelem);

  t->alarmTime = ALARM_OFF;
  t->recentCPU = running_thread()->recentCPU;
  t->niceValue = running_thread()->niceValue;

//#ifdef USERPROG

  list_init(&(t->childStatusList));
  list_init(&(t->fileDescriptorList));

  //자신의 Parent Thread를 가리킨다.
  t->parent = running_thread(); 
  
  sema_init(&(t->lockForChildLoad),0);
  sema_init(&(t->lockForChildExecute),0);
  sema_init(&(t->alarmByParent),0);
  lock_init(&(t->tempLock));

  t->loadFailure = true;
//#endif
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

/** Functions for fixed point float */

//Fraction : 소수부
#define FRACTION_OF_FIXED_POINT 16384 //(1<<14)

/** Fixed Floating Point : 일반 실수 * 2^14 */

/** 실수 + 정수 */
int sumOfFloatAndInt(int fixedFloat, int integer){
  //정수는 소수부가 없으므로 소수부만큼 shift left를 해준다
  int floatConvertedFromInteger = integer * FRACTION_OF_FIXED_POINT;

  return fixedFloat + floatConvertedFromInteger;
}

/** 정수를 Fixed Point Float으로 변환 */
int convertTofixedFloat(int integer){
  return integer * FRACTION_OF_FIXED_POINT;
}

/** Fixed Point Float를 정수로 변환 */
int convertToInteger(int fixedFloat){
  return fixedFloat / FRACTION_OF_FIXED_POINT;
}

/** 정수 - 실수 */
int subractFloatFromInt(int fixedFloat, int integer){
  //정수는 소수부가 없으므로 소수부만큼 shift left를 해준다
  int floatConvertedFromInteger = integer * FRACTION_OF_FIXED_POINT;

  return floatConvertedFromInteger - fixedFloat;
}

/** 실수 * 실수*/
int multiplyTwoFloats(int fixedFloatOne, int fixedFloatTwo){
  int64_t result = fixedFloatOne;
  result = result * fixedFloatTwo / FRACTION_OF_FIXED_POINT;
  return (int)result;
}

/** 실수 + 실수*/
int sumOfTwoFloats(int fixedFloatOne, int fixedFloatTwo){
  return fixedFloatOne + fixedFloatTwo;
}

/** 실수 - 실수*/
int subtractTwoFloats(int fixedFloatOne, int fixedFloatTwo){
  return fixedFloatOne - fixedFloatTwo;
}

/** 실수 / 정수*/
int divideFloatByInt(int fixedFloat, int integer){
  return fixedFloat / integer;
}

/** 실수 * 정수*/
int multiplyFloatAndInt(int fixedFloat, int integer) {
  return fixedFloat * integer;
}

/** 실수 / 실수*/
int divideTwoFloats(int fixedFloatOne, int fixedFloatTwo){
  int64_t result = fixedFloatOne;
  result = fixedFloatOne * FRACTION_OF_FIXED_POINT / fixedFloatTwo;
  
  return (int)result;
}


/* Returns the current thread's nice value. */
int thread_get_nice (void){
  return thread_current()->niceValue;
}


/* Sets the current thread's nice value to NICE. */
void thread_set_nice (int new_nice){
  struct thread * currentThread = thread_current();

  currentThread->niceValue = new_nice;

  currentThread->priority = calculateNewPriorityWith(currentThread->recentCPU, new_nice);

  // updatePriorityOfEachThread();
  // updateRecentCPU();

  int maxPrioirtyAmongReadyQueue = getMaxPriorityFromReadyQueue();

  /** Current 쓰레드의 priority가 최고값이 아닌 경우, 리스케줄링*/
  if(currentThread->priority < maxPrioirtyAmongReadyQueue){
    thread_yield();
  }

}


/** 새로운 Priority 계산*/
int calculateNewPriorityWith(int recentCPU, int niceValue){
  
  /** newPriority = PRI_MAX - (convertToInteger(recentCPU) / 4) - (niceValue * 2) */
  int floatPRI_MAX = convertTofixedFloat(PRI_MAX);
  int floatNiceValue = convertTofixedFloat(niceValue);
  int temp1 = subtractTwoFloats(floatPRI_MAX,divideFloatByInt(recentCPU,4));
  int floatMediumResults = subtractTwoFloats(temp1, multiplyFloatAndInt(floatNiceValue,2));

  int newPriority = convertToInteger(floatMediumResults); //Priority는 정수!

  if(newPriority > PRI_MAX){
    newPriority = PRI_MAX;
  } else if(newPriority < PRI_MIN){
    newPriority = PRI_MIN;
  }

  return newPriority;
}


/** Ready List에서 Maximum priority 값을 얻음*/
int getMaxPriorityFromReadyQueue(void){
  
  int readyListLength = list_size(&ready_list);

  if(readyListLength>0){
    struct list_elem * eachReadyNode = list_begin(&ready_list);
    struct thread * maxPriorityThread = list_entry(eachReadyNode,struct thread, elem);

    return maxPriorityThread->priority;

  }else{
    return READY_LIST_EMPTY;
  }
}


/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu (void){
  // 실수를 정수형으로 이용할 것이기 때문에, FRACTION으로 나눔으로써 정수형 형변환 한 것임
  struct thread * currentThread = thread_current();
  return convertToInteger(multiplyFloatAndInt(currentThread->recentCPU,100));
}


/* Returns 100 times the system load average. */
int thread_get_load_avg (void){
  //mlfqs-load-1의 testcase에서 정수형으로 이용할 것이기 때문에, FRACTION으로 나눔으로써 정수형 형변환 한 것임
  return convertToInteger(multiplyFloatAndInt(loadAvg,100));
}


/** Ready List의 쓰레드들의 Priority 업데이트
 * 4 ticks 마다 불림 (timer_interrupt())
*/
void updatePriorityOfEachThread(void){

  struct list_elem * eachThreadNode = list_begin(&all_list);
  struct thread * targetReadyThread;

  while(eachThreadNode!=list_end(&all_list)){

    targetReadyThread = list_entry(eachThreadNode,struct thread, allelem);

    targetReadyThread->priority = calculateNewPriorityWith(targetReadyThread->recentCPU,
                                                      targetReadyThread->niceValue);

    eachThreadNode = list_next(eachThreadNode);
  }
}



/** 시스템에 존재하는 모든 쓰레드(러닝쓰레드 & 레디쓰레드)의 LoadAvg, recentCPU 업데이트
 * 1초(100 ticks)마다 불림 (timer_interrupt())
*/
void updateLoadAvg(void){

  /** Load Avg 업데이트 : 레디 큐 길이 이용*/
  int readyListLength = list_size(&ready_list);
  struct thread * currentThread = thread_current();

  //Running중인 현재 쓰레드도 포함
  if(currentThread != idle_thread){
    readyListLength++;
  }

  // load_avg = (59/60) * load_avg + (1/60) * ready_threads
  int temp1 = multiplyFloatAndInt(loadAvg,59);
  temp1 = sumOfFloatAndInt(temp1,readyListLength);
  loadAvg = divideFloatByInt(temp1,60);
}


/** Recent CPU 업데이트 */
void updateRecentCPU(void){

  struct list_elem * eachReadyNode = list_begin(&all_list);
  struct thread * targetReadyThread;

  while(eachReadyNode!=list_end(&all_list)){

    targetReadyThread = list_entry(eachReadyNode,struct thread, allelem);
    
    if(targetReadyThread != idle_thread){
      // printf("updateRecentCPU : 계산 전 : 쓰레드 이름 : %s, recent CPU : %d, load Avg : %d\n",targetReadyThread->name,targetReadyThread->recentCPU,loadAvg);                                                
      targetReadyThread->recentCPU = calculateNewRecentCPUWith(loadAvg,targetReadyThread->recentCPU,
                                                        targetReadyThread->niceValue);
      // printf("updateRecentCPU : 계산 후 : 쓰레드 이름 : %s, recent CPU : %d\n",targetReadyThread->name,targetReadyThread->recentCPU);                                                
    }
    
    eachReadyNode = list_next(eachReadyNode);
  }
}


/** recent_cpu = (2 * load_avg) / (2 * load_avg + 1 ) * recent_cpu + nice */
int calculateNewRecentCPUWith(int loadAverage, int recentCPU, int nice){

  int temp1 = multiplyFloatAndInt(loadAverage,2); // (2 * load_avg)
  int temp2 = sumOfFloatAndInt(temp1,1); // (2 * load_avg + 1 )

  temp1 = divideTwoFloats(temp1,temp2); // (2 * load_avg) / (2 * load_avg + 1 )

  // (2 * load_avg) / (2 * load_avg + 1 ) * recent_cpu
  temp1 = multiplyTwoFloats(temp1,recentCPU);

  // (2 * load_avg) / (2 * load_avg + 1 ) * recent_cpu + nice
  temp1 = sumOfFloatAndInt(temp1, nice);
  
  return temp1;
}
