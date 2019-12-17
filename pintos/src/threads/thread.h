#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "synch.h"
#include "threads/synch.h"

#ifndef USERPROG
extern bool thread_prior_aging;
#endif

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* Alarm time */
#define ALARM_OFF 0

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */

typedef int64_t fixedFloat;

struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */
   
    struct lock fileAccessLock;

    int alarmTime;
    int recentCPU;
    int niceValue;

//#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */

    struct list childStatusList; // 자식 List를 2개로 운용(childProcessList가 가리키는 자식 thread가 free되어도 값을 참조하기위해)
    struct thread * parent; // 자신의 parent를 가리키는 용도, 초기화 : init_thread() 

    struct list fileDescriptorList;

    struct semaphore lockForChildLoad;
    struct semaphore lockForChildExecute;
    struct semaphore alarmByParent;
    struct lock tempLock;

    bool loadFailure;

    
    
//#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/** Parent Thread의 Child Status Linked List에서 유지할 
 * Child Thread의 tid & exit status 정보
*/
struct childStatus {
   struct list_elem self;
   tid_t childTid;
   int exitStatus;
   struct thread * threadItself;
};

/** fileDescriptorList에 Linked List의 노드 구조체 
 * fileName을 이용한 탐색, number을 이용한 file Descriptor 값을 할당한다.
*/
struct fileDescriptor{
   struct list_elem self;
   int number;
   struct file * file;
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

bool isThreadDead(struct thread *threadForCheck);
void cleanMemoryOfCurrentThread(struct thread * curThread);

/** 스케줄러용 User defined Functions */

/** Thread Aging*/
void thread_aging(void);
/** @first, @second 쓰레드의 Priority 비교 */
bool comparePriorityOf(const struct list_elem* first, const struct list_elem* second, void* aux);


/** 새로운 Priority 계산*/
int calculateNewPriorityWith(int recentCPU, int niceValue);
/** Ready List에서 Maximum priority 값을 얻음*/
int getMaxPriorityFromReadyQueue(void);
/** Ready List의 쓰레드들의 Priority 업데이트*/
void updatePriorityOfEachThread(void);
/** Ready List의 쓰레드들의 LoadAvg, recentCPU 업데이트*/
void updateLoadAvg(void);
void updateRecentCPU(void);
/** recent_cpu = (2 * load_avg) / (2 * load_avg + 1 ) * recent_cpu + nice */
// int calculateNewRecentCPUWith(int recentCPU, int nice);
int calculateNewRecentCPUWith(int loadAverage, int recentCPU, int nice);


/** Fixed Point Float 계산 Functions */

int convertTofixedFloat(int integer);
int convertToInteger(int fixedFloat);

/** 실수 + 정수*/
int sumOfFloatAndInt(int fixedFloat, int integer);
/** 정수 - 실수*/
int subractFloatFromInt(int fixedFloat, int integer);
/** 실수 / 정수*/
int divideFloatByInt(int fixedFloat, int integer);
/** 실수 * 정수*/
int multiplyFloatAndInt(int fixedFloat, int integer);

/** 실수 * 실수*/
int multiplyTwoFloats(int fixedFloatOne, int fixedFloatTwo);
/** 실수 + 실수*/
int sumOfTwoFloats(int fixedFloatOne, int fixedFloatTwo);
/** 실수 - 실수*/
int subtractTwoFloats(int fixedFloatOne, int fixedFloatTwo);
/** 실수 / 실수*/
int divideTwoFloats(int fixedFloatOne, int fixedFloatTwo);


#endif /* threads/thread.h */
