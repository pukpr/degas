/*  This implementation uses the "ucontext" facility to simulate tasking at
**  the user-level. Timing can be controlled by a scheduler running in the
**  main thread that this module implements. If all tasks are sleeping
**  then the scheduler wakes up and dispatches according to a time-based
**  priority queue.
**
**  COMPLETE DETERMINISM: This library provides a simulated clock that is
**  completely deterministic. Unlike real-time software that depends on the
**  vagaries of the always-changing system clock, the simulated clock here
**  always gives the same response for the same sequence of operations. This
**  ensures reproducible execution traces, making debugging output understandable
**  and regression tests reliable. Time only advances when the scheduler
**  explicitly moves it forward - there is no dependency on wall-clock time.
**
**  Timers need special care because they could use an absolute or relative
**  clock.  An env. var. will need to be supplied to set absolute time.
**  For implementation, we use the main context for the scheduler.
**  This only wakes up when all timed cond-vars (and non-timed CV and
**  mutexes) are waiting. A priority scan is used to dispatch the timed
**  CVs in the order they should be released.  The "gettimeofday" gets
**  replaced with a simulated version that tracks a monotonic time.
**
**  Note: The SunOS and IRIX versions work, but defaults to Linux
**        Undef the macro desired, or use -D on comand line
**
**  gcc degas.c -shared -Xlinker -soname=libdegas.so -o libdegas.so
*/

//#define LINUX 1
//#define SUNOS 1
//#define IRIX 1

#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <values.h>
#include <errno.h>
#include <unistd.h>

#define MAX_THREAD_KEYS 10  /* used by tasking to hold self data */
#define DEFAULT_STACK_SIZE 1000000
#define MAX_THREADS 1000 /* max threads that can be active at once */
#define MAX_DEADLOCKS 1000  /* max deadlock attempts to fire */

char * SIM_CONTEXT_DEBUG = "SIM_CONTEXT_DEBUG";

struct timespec monotonic_time = {0, 0};
const struct timespec zerotime = {0, 0};
const struct timespec max_time = {MAXLONG, MAXLONG};

#define lessThan(l,r) (((l)->tv_sec < (r)->tv_sec) || \
                      (((l)->tv_sec == (r)->tv_sec) && \
                       ((l)->tv_nsec < (r)->tv_nsec)))

static pthread_attr_t PAT;

int debug = 0;
int initializedScheduler = 0;

/*  The Cntxt Structure -- Contains information about individual Cntxts. */
typedef struct {
  ucontext_t context;     /* Stores the current context */
  void (*func) (void *);  /* Functional behavior */
  void * arg;             /* Arguments for function */
  int waiter;             /* Is the thread waiting on timer? */
  struct timespec wait;   /* Timer time */
  int finished;           /* whether or not cntxt has ran and finished */
  int timed_out;          /* whether or not the last wait timed out */
} Cntxt;

Cntxt cntxtList[MAX_THREADS];       /* The Cntxt "queue" */
size_t currentCntxt = 0;               /* The index of currently executing Cntxt */
int numCntxts = 0;                  /* The number of Cntxts */
int numActiveCntxts = 0;            /* The number of active Cntxts */
/* The number of waiting Cntxts.  When this equals numActiveCntxts the scheduler
   will move time forward to the Cntxt with minimum wait time. */
int numWaitingCntxts = 0;
int livelockCounter = 0;            /* Counts consecutive yields without progress */
#define LIVELOCK_THRESHOLD 100      /* Max yields before checking for deadlock */
#define NSEC_PER_SEC 1000000000     /* Nanoseconds per second */

typedef struct {
  void * KA[MAX_THREADS];
  int set;
} KeyAddress;

KeyAddress addresses[MAX_THREAD_KEYS];

/* Forward declaration for debug printing */
void PrintDebug(char * msg, int v1, int v2);
#define printDebug(A,B,C)  PrintDebug(A,B,C)

/* Waiter queue for condition variables - supports multiple waiters per CV */
#define MAX_CVS 100
#define MAX_WAITERS_PER_CV 10

typedef struct {
  pthread_cond_t *cv_ptr;     /* Pointer to the condition variable */
  int waiters[MAX_WAITERS_PER_CV]; /* Array of waiting context IDs */
  int num_waiters;            /* Number of waiters in the queue */
} CVWaiterQueue;

CVWaiterQueue cv_queues[MAX_CVS];
int num_cv_queues = 0;

/* Find or create a waiter queue for a condition variable */
CVWaiterQueue* get_cv_queue(pthread_cond_t *cv) {
  int i;
  /* Search for existing queue */
  for (i = 0; i < num_cv_queues; i++) {
    if (cv_queues[i].cv_ptr == cv) {
      return &cv_queues[i];
    }
  }
  /* Create new queue */
  if (num_cv_queues < MAX_CVS) {
    cv_queues[num_cv_queues].cv_ptr = cv;
    cv_queues[num_cv_queues].num_waiters = 0;
    num_cv_queues++;
    return &cv_queues[num_cv_queues - 1];
  }
  return NULL; /* Too many CVs */
}

/* Add a waiter to the CV queue */
void cv_add_waiter(pthread_cond_t *cv, int context_id) {
  CVWaiterQueue *queue = get_cv_queue(cv);
  if (queue && queue->num_waiters < MAX_WAITERS_PER_CV) {
    queue->waiters[queue->num_waiters] = context_id;
    queue->num_waiters++;
    printDebug("cv+ add_waiter", context_id, queue->num_waiters);
  }
}

/* Remove and return the first waiter from the CV queue */
int cv_remove_waiter(pthread_cond_t *cv) {
  CVWaiterQueue *queue = get_cv_queue(cv);
  if (queue && queue->num_waiters > 0) {
    int waiter = queue->waiters[0];
    /* Shift remaining waiters */
    int i;
    for (i = 0; i < queue->num_waiters - 1; i++) {
      queue->waiters[i] = queue->waiters[i + 1];
    }
    queue->num_waiters--;
    printDebug("cv- remove_waiter", waiter, queue->num_waiters);
    return waiter;
  }
  return -1; /* No waiters */
}

/* Remove a specific waiter from the CV queue (for cleanup) */
void cv_remove_specific_waiter(pthread_cond_t *cv, int context_id) {
  CVWaiterQueue *queue = get_cv_queue(cv);
  if (queue) {
    int i, j;
    for (i = 0; i < queue->num_waiters; i++) {
      if (queue->waiters[i] == context_id) {
        /* Shift remaining waiters */
        for (j = i; j < queue->num_waiters - 1; j++) {
          queue->waiters[j] = queue->waiters[j + 1];
        }
        queue->num_waiters--;
        printDebug("cv* remove_specific", context_id, queue->num_waiters);
        break;
      }
    }
  }
}

/* Check if there are any waiters on the CV */
int cv_has_waiters(pthread_cond_t *cv) {
  CVWaiterQueue *queue = get_cv_queue(cv);
  return (queue && queue->num_waiters > 0);
}

void Scheduler_init(void) {
  if (!initializedScheduler) {
    initializedScheduler = 1;
    debug = getenv(SIM_CONTEXT_DEBUG) != 0;
    if (debug) printf("%i : %s\n", debug, SIM_CONTEXT_DEBUG);

    /* Initialize monotonic time to 1 second to mimic the effect of delay 1.0
     * This provides a deterministic starting point - the same initial value
     * every time, ensuring reproducible behavior across all runs. */
    monotonic_time.tv_sec = 1;
    monotonic_time.tv_nsec = 0;

    /* the internal scheduler has an extra active context */
    if (numActiveCntxts == 0) numActiveCntxts = 1;  /* The main program is active */

    /* initialize arrays to 0 */
    int i;
    for (i = 0; i < MAX_THREADS; i++) {
      cntxtList[i].finished = 0;
      cntxtList[i].waiter = 0;
    }
    for (i = 0; i < MAX_THREAD_KEYS; i++) {
      addresses[i].set = 0;
    }
    pthread_attr_init(&PAT);
    pthread_attr_setstacksize(&PAT, DEFAULT_STACK_SIZE);
    
    /* Initialize context 0 (main thread) to properly save/restore its state */
    getcontext(&cntxtList[0].context);
  }
  initializedScheduler = 1;
}

/* optionally prints out debug info */
void PrintDebug(char * msg, int v1, int v2);
#define printDebug(A,B,C)  PrintDebug(A,B,C)

/* Creates a new Cntxt, running the function that is passed as argument. */
size_t spawnCntxt(void (*func) (void *),
                  void * stack,
                  int size,
                  void * arg);

/*  Yield control to another execution context */
void cntxtYield();

size_t getCurrentCntxt();
int cntxtsAllSleeping();
void holdContext(struct timespec time, int Context);
int releaseContext();
void incrWaitingCntxt();
void decrWaitingCntxt();

int pthread_attr_init (pthread_attr_t *__attr);
int pthread_attr_setstacksize (pthread_attr_t *__attr, size_t __stacksize);


/* Records when Cntxt started and when it is done so that we know when
   to free its resources. Called in the Cntxt's context of execution. */

void CntxtStart(void (*func) (void *)) {
  func(cntxtList[currentCntxt].arg);
  numActiveCntxts -= 1;
  cntxtList[currentCntxt].finished = 1;
  printDebug ("@ FINISHED", (int)currentCntxt, 0);

  // Wake up anyone waiting for a join or mutex
  int i;
  for (i=0; i<numCntxts+1; i++) {
     if (cntxtList[i].waiter && !lessThan(&cntxtList[i].wait, &max_time)) {
         holdContext(zerotime, i);
     }
  }

  cntxtYield();
}

/* Creates new Cntxt, running the function that is passed as an argument */
size_t spawnCntxt(void (*func) (void *),
                  void * stack,
                  int size,
                  void * arg) {
  Scheduler_init();
  if (numCntxts >= MAX_THREADS) {
    printf("EXIT, too many tasks : %i\n", MAX_THREADS);
    exit(1);
  }
  /* the 0th cntxt is the main scheduler cntxt */
  numCntxts += 1;
  /* Add the new function to the end of the Cntxt list */
  getcontext(&cntxtList[numCntxts].context);

  /* Set the context to a newly allocated stack */
  cntxtList[numCntxts].context.uc_link = 0;
#if SUNOS==1 || IRIX==1
  cntxtList[numCntxts].context.uc_stack.ss_sp = stack + size - 8;  /* Bug? */
#else
  cntxtList[numCntxts].context.uc_stack.ss_sp = stack;
#endif
  cntxtList[numCntxts].context.uc_stack.ss_size = size;
  cntxtList[numCntxts].context.uc_stack.ss_flags = 0;
  cntxtList[numCntxts].arg = arg;
  cntxtList[numCntxts].func = func;
  cntxtList[numCntxts].waiter = 0;
  cntxtList[numCntxts].wait.tv_sec = 0;
  cntxtList[numCntxts].wait.tv_nsec = 0;

  /* Create the context. The context calls CntxtStart( func ). */
  makecontext(&cntxtList[numCntxts].context,
              (void *) CntxtStart,
              1,
              cntxtList[numCntxts].func);
  numActiveCntxts += 1;
  return numCntxts;
}

int Deadlock = 0;

int findMinWaitingCntxt() {
  struct timespec minTime = max_time;
  int minCntxt = -1;
  int i;
  for (i=0; i < numCntxts+1; i++) {
    if (cntxtList[i].waiter && !cntxtList[i].finished) {
       if (minCntxt == -1 || lessThan(&cntxtList[i].wait, &minTime)) {
          minCntxt = i;
          minTime = cntxtList[i].wait;
       }
    }
  }
  return minCntxt;
}

void cntxtYield() {
  size_t lastCntxt = currentCntxt;

  do { /*  Round-robin loop */
    currentCntxt = (currentCntxt + 1) % (numCntxts + 1);
  } while (cntxtList[currentCntxt].finished);

  if (cntxtsAllSleeping()) {
    currentCntxt = findMinWaitingCntxt();
    printDebug("! SCHEDULE", currentCntxt, 0);
    if (cntxtList[currentCntxt].waiter) {
      /* Deterministic time advancement: Jump directly to next wakeup time.
       * This is the key to DEGAS's determinism - time never flows on its own,
       * it only advances when explicitly set here. Same program = same times. */
      monotonic_time = cntxtList[currentCntxt].wait;
      cntxtList[currentCntxt].timed_out = 1;
    }
    /* Only clear waiter flag if this is a timed wait (not max_time).
     * Contexts waiting on condition variables with max_time should remain
     * in waiting state until explicitly signaled. Clearing waiter for max_time
     * waits causes pthread_cond_wait to return prematurely, leading to the
     * context re-entering the wait and adding itself to the queue multiple times. */
    if (!lessThan(&cntxtList[currentCntxt].wait, &max_time)) {
      /* This context is waiting on max_time (indefinite CV wait).
       * Don't clear waiter flag - it should only be cleared by signal/broadcast. */
    } else {
      /* This is a timed wait that has expired. Clear waiter flag. */
      cntxtList[currentCntxt].waiter = 0;
    }
  }
  printDebug("#sw", (int)lastCntxt, (int)currentCntxt);

  if (lastCntxt != currentCntxt) {
     swapcontext(&cntxtList[lastCntxt].context,
                 &cntxtList[currentCntxt].context);
  }
}

size_t getCurrentCntxt() {
  return currentCntxt;
}

int cntxtsAllSleeping() {
  return (numWaitingCntxts == numActiveCntxts);
}

void PrintDebug(char * msg, int v1, int v2) {
  if (debug) {
    printf(" |%s %i %i \n", msg, v1, v2);
  }
}

void holdContext(struct timespec time, int Context) {
  cntxtList[Context].waiter = 1;
  cntxtList[Context].wait = time;
}

int releaseContext() {
  return !cntxtList[currentCntxt].waiter;
}

void incrWaitingCntxt() {
  numWaitingCntxts++;
}

void decrWaitingCntxt() {
  numWaitingCntxts--;
}

/*************************************************************
*
* Pthread creation, attributes, keys
*
**************************************************************/

#if IRIX==1
# define STACKSIZE __attr->__D[0]
#elif SUNOS==1
# define STACKSIZE (int)__attr->__pthread_attrp
#else
/* Note: On Linux, pthread_attr_t is opaque, but degas needs to store stack size.
 * We use the __align field to store the stack size value that we intercept.
 * The STACKSIZE0 macro (__stacksize) would be correct for real pthread structures,
 * but since we're intercepting pthread calls, we control our own storage.
 * Important: pthread_attr_init() must initialize STACKSIZE to 0 to avoid
 * displaying garbage values (e.g., 2129920) from uninitialized memory.
 */
# define STACKSIZE0 __attr->__stacksize
# define STACKSIZE __attr->__align
#endif

int pthread_create (pthread_t *__restrict __threadp,
                    __const pthread_attr_t *__restrict __attr,
                    void *(*__start_routine) (void *),
                    void *__restrict __arg) {
  size_t res = 0 ;
  int val;
  size_t size = DEFAULT_STACK_SIZE;
  void *fake_stack;

  if (__attr != NULL) {
    val = pthread_attr_getstack(__attr, &fake_stack, &size);
    if (size == 0) {
      size = DEFAULT_STACK_SIZE;
    }
  }

  char *stack = malloc(size);

  res = spawnCntxt((void *)__start_routine,
                   stack,
                   (int)size,
                   __arg);
  *__threadp = (pthread_t)res;
  printDebug("p CREATE", (size_t)*__threadp, (int)size);
  return 0;
}

int pthread_join (pthread_t __threadid, void **__value64) {
  size_t res = (size_t)__threadid;
  printDebug("p join", (int)res, 0);
  incrWaitingCntxt();
  while (!cntxtList[res].finished) {
    holdContext(max_time, currentCntxt);
    sched_yield();
  }
  decrWaitingCntxt();
  return 0;
}

int pthread_kill (pthread_t __threadid, int __signo) {
  printDebug("p kill", 0, 0);
  return 0;
}

pthread_key_t cKey = 1;
int pthread_key_create (pthread_key_t *__key,
                        void (*__destr_function) (void *)) {
  (*__key) = cKey;
  cKey = cKey + 1;
  printDebug("p key-create", (int)cKey, 0);
  return 0;
}

int pthread_sigmask (int __how,
#if SUNOS==1 || IRIX==1
                     sigset_t *__newmask,
                     sigset_t *__oldmask) {
#else
                     __const __sigset_t *__restrict __newmask,
                     __sigset_t *__restrict __oldmask) {
#endif
  /*  no debugging allowed here */
  return 0;
}

int pthread_setspecific (pthread_key_t __key,
                         __const void *__pointer) {
  /* don't call printDebug here.. may cause seg fault on task exit! */
  addresses[(int)__key].KA[getCurrentCntxt()] = (void *)__pointer;
  addresses[(int)__key].set = 1;
  return 0;
}

void *pthread_getspecific (pthread_key_t __key) {
  /* Cannot have any textIO here, o.w. secondary stack blows up */
  if (addresses[__key].set) {
    if (addresses[__key].KA[getCurrentCntxt()] == 0) {
      /* not set, so use the key for the main thread */
      return addresses[__key].KA[0];
    } else {
      /*  use the key previously set */
      return addresses[__key].KA[getCurrentCntxt()];
    }
  } else {
    return 0;
  }
}

int pthread_attr_setstacksize (pthread_attr_t *__attr,
                               size_t __stacksize) {
  STACKSIZE = __stacksize;
  //printDebug("p attrsetstacksize", (int)__stacksize, 0);
  printDebug("p attrsetstacksize", STACKSIZE, 0);
  return 0;
}

int pthread_setschedparam (pthread_t __target_thread, int __policy,
                           __const struct sched_param *__param) {
  printDebug("p setschedparam", (int)__target_thread, 0);
  return 0;
}

int pthread_attr_setschedpolicy (pthread_attr_t *__attr, int __policy) {
  printDebug("p attrsetschedpolicy", __policy, 0);
  return 0;
}

int pthread_attr_init (pthread_attr_t *__attr) {
  printDebug("p attrinit", 0, 0);
  /* Initialize STACKSIZE to 0 to avoid showing garbage values in debug output.
   * The actual stack size will be set later via pthread_attr_setstacksize().
   * Without this initialization, __align field contains arbitrary memory values,
   * causing non-uniform stack sizes in debug output (e.g., 2129920 instead of expected value).
   */
  STACKSIZE = 0;
  return 0;
}

// This appears to be used by the GNAT stack checker in 6.1
// Will need some work if stack checking invoked
int pthread_getattr_np (pthread_t __th, pthread_attr_t *__attr) {
  printDebug("p getattr_np", 0, 0);
  if (__attr) *__attr = PAT;
  return 0;
}

static int BASE = 0;

int pthread_attr_getstack (__const pthread_attr_t *__restrict __attr,
                           void **__restrict __stackaddr,
                           size_t *__restrict __stacksize) {
  *__stacksize = STACKSIZE;
  *__stackaddr = &BASE;
  printDebug("p attrgetstack", STACKSIZE, 0);
  return 0;
}                           

int pthread_attr_destroy (pthread_attr_t *__attr) {
  printDebug("p attrdestroy", 0, 0);
  return 0;
}

int pthread_attr_setdetachstate (pthread_attr_t *__attr,
                                 int __detachstate) {
  printDebug("p attrsetdetachstate", __detachstate, 0);
  return 0;
}

/*************************************************************
*
* Yield, Self, Exit
*
**************************************************************/

int sched_yield (void) {
  cntxtYield();
  return 0;
}

pthread_t pthread_self (void) {
  printDebug("p self", getCurrentCntxt(), 0);
  return (pthread_t)getCurrentCntxt();
}

void pthread_exit (void *__retval) {
  printDebug("p exit", 0, 0);
  numActiveCntxts -= 1;
  cntxtList[currentCntxt].finished = 1;
  cntxtYield();
  while(1);
}

/*************************************************************
*
* Mutex
*
**************************************************************/

int pthread_mutexattr_init (pthread_mutexattr_t *__attr) {
  Scheduler_init();  /* First call from Ada tasking */
  printDebug("m attrinit", 0, 0);
  return 0;
}

int pthread_mutexattr_destroy (pthread_mutexattr_t *__attr) {
  printDebug("m attrdestroy", 0, 0);
  return 0;
}

#if IRIX==1
# define MOWNER0 __mutex->__D[1]
# define MCOUNT0 __mutex->__D[0]
#elif SUNOS==1
# define MOWNER0 __mutex->__pthread_mutex_data
# define MCOUNT0 __mutex->__pthread_mutex_data
#else
# define MOWNER __mutex->__data.__owner
# define MCOUNT __mutex->__data.__count
#endif

int pthread_mutex_init (pthread_mutex_t *__restrict __mutex,
                        __const pthread_mutexattr_t *__restrict __mutex_attr) {
 Scheduler_init();  /* in case not init'ed */
 MOWNER = 0;
 MCOUNT = 0;
 printDebug("m init", MCOUNT, 0);
 return 0;
}

int pthread_mutex_destroy (pthread_mutex_t *__mutex) {
  printDebug("m destroy", MCOUNT, 0);
  return 0;
}

int pthread_mutex_lock (pthread_mutex_t *__mutex) {
  printDebug("m lock", MOWNER, currentCntxt);
  if (MOWNER == (int)currentCntxt + 1) {
    MCOUNT++;
    return 0;
  }
  /* Only increment waiting count if mutex is actually contended.
   * This prevents the scheduler from incorrectly thinking all contexts
   * are sleeping when the mutex is immediately available. */
  if (MOWNER != 0) {
    incrWaitingCntxt();
    while (MOWNER != 0) {
      holdContext(max_time, currentCntxt);
      sched_yield();
    }
    decrWaitingCntxt();
  }
  MOWNER = (int)currentCntxt + 1; /* Add one for Context=0 */
  MCOUNT = 1;
  return 0;
}

int pthread_mutex_unlock (pthread_mutex_t *__mutex) {
  if (MOWNER == (int)currentCntxt + 1) {
    MCOUNT--;
    if (MCOUNT == 0) {
      MOWNER = 0;
      /* forced yield to give other threads a chance */
      sched_yield();
    }
  }
  return 0;
}

/*************************************************************
*
* Condition Variable
*
**************************************************************/

int pthread_condattr_init (pthread_condattr_t *__attr) {
  Scheduler_init(); /* Second call from Ada tasking */
  printDebug("c attrinit", 0, 0);
  return 0;
}

int pthread_condattr_destroy (pthread_condattr_t *__attr) {
  printDebug("c attrdestroy", 0, 0);
  return 0;
}

#if IRIX==1
# define SPINLOCK __cond->__D[0]
#elif SUNOS==1
# define SPINLOCK __cond->__pthread_cond_data
#else
# define SPINLOCK0 __cond->__c_lock.__spinlock
# define SPINLOCK (*(unsigned long long *)&(__cond->__data.__wseq))
#endif

int pthread_cond_init (pthread_cond_t *__restrict __cond,
                       __const pthread_condattr_t *__restrict
                       __cond_attr) {
  Scheduler_init();  /* in case not init'ed */
  SPINLOCK = 0;  /* Holds the spinlock */
  printDebug("c init", currentCntxt, 0);
  return 0;
}

int pthread_cond_destroy (pthread_cond_t *__cond) {
  printDebug("c destroy", 0, 0);
  return 0;
}


int pthread_cond_signal (pthread_cond_t *__cond) {
  /* Wake up one waiter from the queue */
  int waiter = cv_remove_waiter(__cond);
  printDebug("c signal", waiter, 0);
  if (waiter >= 0) {
     holdContext(zerotime, waiter);  /* schedules the waiting context */
     sched_yield();  /* Give the waiter a chance to run */
  }
  return 0;
}

int pthread_cond_broadcast (pthread_cond_t *__cond) {
    /* Wake up all waiters from the queue */
    printDebug("c broadcast", 0, 0);
    while (cv_has_waiters(__cond)) {
        int waiter = cv_remove_waiter(__cond);
        if (waiter >= 0) {
            holdContext(zerotime, waiter);
        }
    }
    sched_yield();
    return 0;
}

int pthread_cond_wait (pthread_cond_t *__restrict __cond,
                       pthread_mutex_t *__restrict __mutex) {
  printDebug("c wait", currentCntxt, 0);
  
  /* Add to waiter queue and mark as waiting BEFORE unlocking the mutex.
   * This ensures atomicity - the context is registered as waiting before
   * it can be rescheduled by the yield in mutex_unlock.
   * However, don't increment numWaitingCntxts yet, as that would make
   * the scheduler think all contexts are sleeping while this context
   * is still actively executing the mutex_unlock. */
  cv_add_waiter(__cond, currentCntxt);
  holdContext(max_time, currentCntxt);
  
  pthread_mutex_unlock(__mutex);
  
  /* Now increment the waiting count after the unlock is complete */
  incrWaitingCntxt();
  
  /* Wait until signaled (context released) */
  while (!releaseContext()) {
    sched_yield();
  }
  
  decrWaitingCntxt();
  pthread_mutex_lock(__mutex);
  return 0;
}

int pthread_cond_timedwait (pthread_cond_t *__restrict __cond,
                            pthread_mutex_t *__restrict __mutex,
                            __const struct timespec *__restrict __abstime) {
  /* Note: The timewait debug values show absolute time (accumulated from time=0),
   * not relative delays. This is correct behavior as per POSIX specification:
   * pthread_cond_timedwait() takes an absolute time value in __abstime.
   * The monotonic_time starts at 1 second (see Scheduler_init), so timewait
   * values accumulate from that point (e.g., 2s, 3s, 4s, etc.).
   */
  printDebug("c timewait", (int)__abstime->tv_sec, (int)__abstime->tv_nsec);
  cntxtList[getCurrentCntxt()].timed_out = 0;
  holdContext(*__abstime, getCurrentCntxt());
  incrWaitingCntxt();
  pthread_mutex_unlock(__mutex);
  while (!releaseContext()) {
    sched_yield();
  }
  decrWaitingCntxt();
  pthread_mutex_lock(__mutex);
  /* should there be a sched_yield() here? */
  return cntxtList[getCurrentCntxt()].timed_out ? ETIMEDOUT : 0;
}

/*************************************************************
*
* Time
*
**************************************************************/

int gettimeofday (struct timeval *__restrict __tv,
#if IRIX==1
                  ...)
#elif SUNOS==1
                  void * __tz)
#else
                  void * __tz)
#endif
{
  /* convert from timespec to timeval.  always round up nsec. */
  (__tv)->tv_sec = monotonic_time.tv_sec;
  (__tv)->tv_usec = (monotonic_time.tv_nsec+999) / 1000;
  return 0;
}

/* More accurate version, but not used on Linux 
 * Returns the simulated monotonic_time regardless of which clock type is requested.
 * This ensures complete determinism - the same program always gets the same time values.
 * Note: Does not distinguish between CLOCK_REALTIME and CLOCK_MONOTONIC, which may
 * cause issues if Ada runtime expects actual wall-clock time (seconds since epoch). */
int clock_gettime(clockid_t ct, struct timespec *__tv)
{
  (__tv)->tv_sec = monotonic_time.tv_sec;
  (__tv)->tv_nsec = monotonic_time.tv_nsec;
  return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    struct timespec abstime;
    abstime.tv_sec = monotonic_time.tv_sec + req->tv_sec;
    abstime.tv_nsec = monotonic_time.tv_nsec + req->tv_nsec;
    if (abstime.tv_nsec >= 1000000000) {
        abstime.tv_sec++;
        abstime.tv_nsec -= 1000000000;
    }
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t c = PTHREAD_COND_INITIALIZER;
    pthread_mutex_lock(&m);
    pthread_cond_timedwait(&c, &m, &abstime);
    pthread_mutex_unlock(&m);
    return 0;
}

unsigned int sleep(unsigned int seconds) {
    struct timespec req = {seconds, 0};
    nanosleep(&req, NULL);
    return 0;
}

int usleep(unsigned int usec) {
    struct timespec req = {usec / 1000000, (usec % 1000000) * 1000};
    nanosleep(&req, NULL);
    return 0;
}
