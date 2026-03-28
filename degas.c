/*  This implementation uses the "ucontext" facility to simulate tasking at
**  the user-level. Timing can be controlled by a scheduler running in the
**  main thread that this module implements. If all tasks are sleeping
**  then the scheduler wakes up and dispatches according to a time-based
**  priority queue.
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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <limits.h>
#include <unistd.h>
#include <dlfcn.h>

#define MAX_THREAD_KEYS 10  /* used by tasking to hold self data */
#define DEFAULT_STACK_SIZE 1000000
#define MAX_THREADS 1000 /* max threads that can be active at once */
#define MAX_DEADLOCKS 1000  /* max deadlock attempts to fire */

char * SIM_CONTEXT_DEBUG = "SIM_CONTEXT_DEBUG";

struct timespec monotonic_time = {0, 0};
const struct timespec zerotime = {0, 0};
const struct timespec max_time = {LONG_MAX, LONG_MAX};

int debug = 0;
int initializedScheduler = 0;

/*  The Cntxt Structure -- Contains information about individual Cntxts. */
typedef struct {
  ucontext_t context;     /* Stores the current context */
  void (*func) (void *);  /* Functional behavior */
  void * arg;             /* Arguments for function */
  int waiter;             /* 0: Ready, 1: Waiting for Timer, 2: Waiting for Sync */
  struct timespec wait;   /* Timer time */
  int finished;           /* whether or not cntxt has ran and finished */
  int next_waiter;        /* For queuing on mutex/cond (index + 1) */
} Cntxt;

/* Shadow structures for pthread types to avoid glibc internal changes */
typedef struct {
    size_t stacksize;
} DegasAttr;

typedef struct {
    int owner;         /* 0 if free, index+1 otherwise */
    int waiters_head;  /* index+1 of first waiting context */
    int waiters_tail;  /* index+1 of last waiting context */
} DegasMutex;

typedef struct {
    int waiters_head;  /* index+1 of first waiting context */
    int waiters_tail;  /* index+1 of last waiting context */
} DegasCond;

#if defined(LINUX) || defined(__linux__)
# define STACKSIZE (((DegasAttr*)__attr)->stacksize)
# define MCOUNT (((DegasMutex*)__mutex)->owner)
# define M_HEAD (((DegasMutex*)__mutex)->waiters_head)
# define M_TAIL (((DegasMutex*)__mutex)->waiters_tail)
# define C_HEAD (((DegasCond*)__cond)->waiters_head)
# define C_TAIL (((DegasCond*)__cond)->waiters_tail)
#elif IRIX==1
# define STACKSIZE __attr->__D[0]
# define MCOUNT __mutex->__D[0]
# define SPINLOCK __cond->__D[0]
#elif SUNOS==1
# define STACKSIZE (int)__attr->__pthread_attrp
# define MCOUNT __mutex->__pthread_mutex_data
# define SPINLOCK __cond->__pthread_cond_data
#endif

Cntxt cntxtList[MAX_THREADS];       /* The Cntxt "queue" */
int currentCntxt = 0;               /* The index of currently executing Cntxt */
int numCntxts = 0;                  /* The number of Cntxts */
int numActiveCntxts = 0;            /* The number of active Cntxts */

/* Per-context ATCB storage — each simulated context keeps its own GNAT Task_Id.
   This bypasses the real __thread TLS which is shared across all OS threads
   (and therefore shared across all our simulated contexts).
   Saved/restored in cntxtYield via the GNAT-internal selfXnn/setXnn functions. */
static void *per_context_atcb[MAX_THREADS];

/* Function pointers for GNAT's TLS self/set — resolved lazily from libgnarl. */
static void *(*real_selfXnn)(void) = NULL;
static void  (*real_setXnn)(void *) = NULL;

static void init_tls_fns(void) {
    if (!real_selfXnn) {
        real_selfXnn = (void *(*)(void))dlsym(RTLD_DEFAULT,
            "system__task_primitives__operations__specific__selfXnn");
        real_setXnn  = (void (*)(void *))dlsym(RTLD_DEFAULT,
            "system__task_primitives__operations__specific__setXnn");
    }
}
/* The number of waiting Cntxts.  When this equals numActiveCntxts the scheduler
   will move time forward to the Cntxt with minimum wait time. */
int numWaitingCntxts = 0;

typedef struct {
  void * KA[MAX_THREADS];
  int set;
} KeyAddress;

KeyAddress addresses[MAX_THREAD_KEYS];

void FinalReport(void) {
    int i;
    printf("\n--- FINAL CONTEXT STATUS ---\n");
    printf("Monotonic Time: %li.%09li\n", monotonic_time.tv_sec, monotonic_time.tv_nsec);
    printf("Active: %i, Waiting: %i\n", numActiveCntxts, numWaitingCntxts);
    for (i = 0; i < numCntxts + 1; i++) {
        printf("Context %i: finished=%i waiter=%i wait=%li.%09li\n", 
               i, cntxtList[i].finished, cntxtList[i].waiter,
               cntxtList[i].wait.tv_sec, cntxtList[i].wait.tv_nsec);
    }
}

void Scheduler_init(void) {
  if (!initializedScheduler) {
    atexit(FinalReport);
    // getcontext(&cntxtList[0].context);  /* main thread */
    debug = getenv(SIM_CONTEXT_DEBUG) != 0;
    printf("%i : %s\n", debug, SIM_CONTEXT_DEBUG);

    /* the internal scheduler has an extra active context */
    numActiveCntxts = 1;  /* The main program is active */

    /* initialize arrays to 0 */
    int i;
    for (i = 0; i < MAX_THREADS; i++) {
      cntxtList[i].finished = 0;
    }
    for (i = 0; i < MAX_THREAD_KEYS; i++) {
      addresses[i].set = 0;
    }
  }
  initializedScheduler = 1;
}

/* optionally prints out debug info */
void PrintDebug(char * msg, int v1, int v2);
#define printDebug(A,B,C)  PrintDebug(A,B,C)

/* Creates a new Cntxt, running the function that is passed as argument. */
int spawnCntxt(void (*func) (void *),
               void * stack,
               int size,
               void * arg);

/*  Yield control to another execution context */
void cntxtYield();

int getCurrentCntxt();
int cntxtsAllSleeping();
void holdContext(struct timespec time, int Context);
int releaseContext();
void incrWaitingCntxt();
void decrWaitingCntxt();


/* Records when Cntxt started and when it is done so that we know when
   to free its resources. Called in the Cntxt's context of execution. */

void CntxtStart(void (*func) (void *)) {
  /* numActiveCntxts was incremented in spawnCntxt */
  if (debug) printf(" |@ START %i\n", currentCntxt);
  func(cntxtList[currentCntxt].arg);
  numActiveCntxts -= 1;
  if (cntxtList[currentCntxt].waiter != 0) decrWaitingCntxt();
  cntxtList[currentCntxt].finished = 1;
  printDebug ("@ FINISHED", currentCntxt, 0);
  cntxtYield();
}

/* Creates new Cntxt, running the function that is passed as an argument */
int spawnCntxt(void (*func) (void *),
               void * stack,
               int size,
               void * arg) {
  if (numCntxts >= MAX_THREADS) {
    printf("EXIT, too many tasks : %i\n", MAX_THREADS);
    exit(1);
  }
  /* the 0th cntxt is the main scheduler cntxt */
  numCntxts += 1;
  numActiveCntxts += 1;
  /* Add the new function to the end of the Cntxt list */
  getcontext(&cntxtList[numCntxts].context);

  /* Set the context to a newly allocated stack */
  cntxtList[numCntxts].context.uc_link = 0;
#if defined(LINUX) || defined(__linux__)
  cntxtList[numCntxts].context.uc_stack.ss_sp = stack;
#else
# if SUNOS==1 || IRIX==1
  cntxtList[numCntxts].context.uc_stack.ss_sp = stack + size - 8;  /* Bug? */
# else
  cntxtList[numCntxts].context.uc_stack.ss_sp = stack;
# endif
#endif
  cntxtList[numCntxts].context.uc_stack.ss_size = size;
  cntxtList[numCntxts].context.uc_stack.ss_flags = 0;
  cntxtList[numCntxts].arg = arg;
  cntxtList[numCntxts].func = func;
  cntxtList[numCntxts].waiter = 0;
  cntxtList[numCntxts].wait.tv_sec = 0;
  cntxtList[numCntxts].wait.tv_nsec = 0;
  cntxtList[numCntxts].next_waiter = 0;

  /* Create the context. The context calls CntxtStart( func ). */
  makecontext(&cntxtList[numCntxts].context,
              (void *) CntxtStart,
              1,
              cntxtList[numCntxts].func);
  return numCntxts;
}

#define lessThan(l,r) (((l)->tv_sec < (r)->tv_sec) || \
                      (((l)->tv_sec == (r)->tv_sec) && \
                       ((l)->tv_nsec < (r)->tv_nsec)))

int Deadlock = 0;

int findMinWaitingCntxt() {
  struct timespec minTime = max_time;
  int minCntxt = -1;
  int i;
  Deadlock += 1;
  for (i=0; i < numCntxts+1; i++) {
    if (cntxtList[i].waiter == 1) { /* Waiting for Timer */
      if (!cntxtList[i].finished) {
        if (lessThan(&cntxtList[i].wait, &minTime)) {
          Deadlock = 0;
          minCntxt = i;
          minTime = cntxtList[i].wait;
        }
      }
    }
  }
  if (Deadlock > MAX_DEADLOCKS) {
    printf("EXIT, global deadlock detected\n");
    exit(1);
  }
  return minCntxt;
}

int cntxtReady();

void cntxtYield() {
  int lastCntxt = currentCntxt;
  int i;

  int next = findMinWaitingCntxt();
  if (currentCntxt == 0 && next == 0) {
      int i;
      for (i = 1; i < numCntxts + 1; i++) {
          if (cntxtList[i].waiter == 1 && !cntxtList[i].finished) {
              next = i;
              break;
          }
      }
  }

  if (next != -1 && cntxtsAllSleeping()) {
      currentCntxt = next;
      printDebug("! SCHEDULE", currentCntxt, Deadlock);
      cntxtList[currentCntxt].waiter = 0;
      if (lessThan(&monotonic_time, &cntxtList[currentCntxt].wait)) {
          monotonic_time = cntxtList[currentCntxt].wait;
      }
  } else if (cntxtsAllSleeping()) {
      printf("EXIT, everyone suspended - deadlock\n");
      exit(1);
  }
  
  /*  Round-robin loop for READY threads */
  int start = currentCntxt;
  for (i = 0; i < numCntxts + 1; i++) {
    int idx = (start + 1 + i) % (numCntxts + 1);
    if (!cntxtList[idx].finished && cntxtList[idx].waiter == 0) {
      currentCntxt = idx;
      break;
    }
  }

  if (lastCntxt != currentCntxt) {
    printDebug("#sw", lastCntxt, currentCntxt);

    /* Save this context's GNAT ATCB before switching away. */
    init_tls_fns();
    if (real_selfXnn) per_context_atcb[lastCntxt] = real_selfXnn();

    swapcontext(&cntxtList[lastCntxt].context,
                &cntxtList[currentCntxt].context);

    /* Restore this context's GNAT ATCB after returning here. */
    if (real_setXnn && per_context_atcb[lastCntxt])
      real_setXnn(per_context_atcb[lastCntxt]);
  }
}

int getCurrentCntxt() {
  return currentCntxt;
}

int cntxtReady() {
  int i;
  for (i = 0; i < numCntxts + 1; i++) {
    if (!cntxtList[i].finished && cntxtList[i].waiter == 0) {
      return 1;
    }
  }
  return 0;
}

int cntxtsAllSleeping() {
  int ready = cntxtReady();
  int res = !ready && (numWaitingCntxts == numActiveCntxts);
  if (debug && res) {
      printf(" |ALL SLEEPING active=%i wait=%i\n", numActiveCntxts, numWaitingCntxts);
  }
  return res;
}

void PrintDebug(char * msg, int v1, int v2) {
  if (debug) {
    printf(" |%s %i %i \n", msg, v1, v2);
    fflush(stdout);
  }
}

void holdContext(struct timespec time, int Context) {
  cntxtList[Context].waiter = 1;
  cntxtList[Context].wait = time;
}

void suspendContext(int Context) {
  cntxtList[Context].waiter = 2;
}

void readyContext(int Context) {
  cntxtList[Context].waiter = 0;
  cntxtList[Context].wait.tv_sec = 0;
  cntxtList[Context].wait.tv_nsec = 0;
}

int releaseContext() {
  return (cntxtList[currentCntxt].waiter == 0);
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

int pthread_create (pthread_t *__restrict __threadp,
                    const pthread_attr_t *__restrict __attr,
                    void *(*__start_routine) (void *),
                    void *__restrict __arg) {
  int res;
  size_t size = DEFAULT_STACK_SIZE;
  if (__attr && STACKSIZE > 0) {
    size = STACKSIZE;
  }

  char *stack = malloc(size);

  res = spawnCntxt((void *)__start_routine,
                   stack,
                   (int)size,
                   __arg);
  *__threadp = (pthread_t)res;
  printDebug("p CREATE", (int)res, (int)size);
  return 0;
}

int pthread_kill (pthread_t __threadid, int __signo) {
  printDebug("p kill", (int)__threadid, __signo);
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
                     const sigset_t *__restrict __newmask,
                     sigset_t *__restrict __oldmask) {
  /*  no debugging allowed here */
  return 0;
}

int pthread_setspecific (pthread_key_t __key,
                         const void *__pointer) {
  /* don't call printDebug here.. may cause seg fault on task exit! */
  addresses[(int)__key].KA[getCurrentCntxt()] = (void *)__pointer;
  addresses[(int)__key].set = 1;
  return 0;
}

void *pthread_getspecific (pthread_key_t __key) {
  /* Cannot have any textIO here, o.w. secondary stack blows up */
  if (addresses[(int)__key].set) {
    if (addresses[(int)__key].KA[getCurrentCntxt()] == 0) {
      /* not set, so use the key for the main thread */
      return addresses[(int)__key].KA[0];
    } else {
      /*  use the key previously set */
      return addresses[(int)__key].KA[getCurrentCntxt()];
    }
  } else {
    return 0;
  }
}

int pthread_attr_setstacksize (pthread_attr_t *__attr,
                               size_t __stacksize) {
  STACKSIZE = __stacksize;
  printDebug("p attrstacksize", (int)__stacksize, 0);
  return 0;
}

int pthread_setschedparam (pthread_t __target_thread, int __policy,
                           const struct sched_param *__param) {
  printDebug("p setschedparam", (int)__target_thread, 0);
  return 0;
}

int pthread_attr_setschedpolicy (pthread_attr_t *__attr, int __policy) {
  printDebug("p attrsetschedpolicy", __policy, 0);
  return 0;
}

int pthread_attr_init (pthread_attr_t *__attr) {
  printDebug("p attrinit", 0, 0);
  STACKSIZE = 0;
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
  exit(0);
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

int pthread_mutex_init (pthread_mutex_t *__restrict __mutex,
                        const pthread_mutexattr_t *__restrict __mutex_attr) {
 Scheduler_init();
 MCOUNT = 0;
 M_HEAD = 0;
 M_TAIL = 0;
 printDebug("m init", 0, 0);
 return 0;
}

int pthread_mutex_destroy (pthread_mutex_t *__mutex) {
  printDebug("m destroy", MCOUNT, 0);
  return 0;
}

int pthread_mutex_lock (pthread_mutex_t *__mutex) {
  int me = currentCntxt + 1;
  if (debug) printf(" |m lock owner=%i me=%i\n", MCOUNT, me);
  if (MCOUNT == 0) {
    MCOUNT = me;
  } else if (MCOUNT != me) {
    /* If we don't already own it, we must wait */
    cntxtList[currentCntxt].next_waiter = 0;
    if (M_TAIL == 0) {
      M_HEAD = M_TAIL = me;
    } else {
      cntxtList[M_TAIL - 1].next_waiter = me;
      M_TAIL = me;
    }
    suspendContext(currentCntxt);
    incrWaitingCntxt();
    while (MCOUNT != me) {
      if (debug) printf(" |m wait owner=%i me=%i\n", MCOUNT, me);
      cntxtYield();
    }
    decrWaitingCntxt();
    readyContext(currentCntxt);
  }
  return 0;
}

int pthread_mutex_unlock (pthread_mutex_t *__mutex) {
  int me = currentCntxt + 1;
  if (MCOUNT == me) {
    if (M_HEAD != 0) {
      int next = M_HEAD;
      M_HEAD = cntxtList[next - 1].next_waiter;
      if (M_HEAD == 0) M_TAIL = 0;
      MCOUNT = next;
      if (debug) printf(" |m unlock wake=%i\n", next-1);
      readyContext(next - 1);
      cntxtYield();
    } else {
      MCOUNT = 0;
      if (debug) printf(" |m unlock free\n");
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
  Scheduler_init();
  printDebug("c attrinit", 0, 0);
  return 0;
}

int pthread_condattr_destroy (pthread_condattr_t *__attr) {
  printDebug("c attrdestroy", 0, 0);
  return 0;
}

int pthread_cond_init (pthread_cond_t *__restrict __cond,
                       const pthread_condattr_t *__restrict __cond_attr) {
  Scheduler_init();
  C_HEAD = 0;
  C_TAIL = 0;
  if (debug) printf(" |c init@ %p ctx=%i\n", (void*)__cond, currentCntxt);
  return 0;
}

int pthread_cond_destroy (pthread_cond_t *__cond) {
  printDebug("c destroy", 0, 0);
  return 0;
}

int pthread_cond_signal (pthread_cond_t *__cond) {
  if (debug) printf(" |c signal@ %p head=%i tail=%i\n", (void*)__cond, C_HEAD, C_TAIL);
  if (C_HEAD != 0) {
    int next = C_HEAD;
    C_HEAD = cntxtList[next - 1].next_waiter;
    if (C_HEAD == 0) C_TAIL = 0;
    printDebug("c signal", next-1, 0);
    cntxtList[next - 1].waiter = 0; /* Clear waiter status */
    readyContext(next - 1);
    cntxtYield(); /* Let the woken context run before signaler continues */
  }
  return 0;
}

int pthread_cond_broadcast (pthread_cond_t *__cond) {
  if (debug) printf(" |c broadcast@ %p head=%i tail=%i\n", (void*)__cond, C_HEAD, C_TAIL);
  while (C_HEAD != 0) {
    pthread_cond_signal(__cond);
  }
  return 0;
}

int pthread_cond_wait (pthread_cond_t *__restrict __cond,
                       pthread_mutex_t *__restrict __mutex) {
  int me = currentCntxt + 1;
  if (debug) printf(" |c wait@ %p ctx=%i head=%i tail=%i\n", (void*)__cond, me-1, C_HEAD, C_TAIL);
  printDebug("c wait", (int)me-1, 0);
  pthread_mutex_unlock(__mutex);

  cntxtList[currentCntxt].next_waiter = 0;
  if (C_TAIL == 0) {
    C_HEAD = C_TAIL = me;
  } else {
    cntxtList[C_TAIL - 1].next_waiter = me;
    C_TAIL = me;
  }

  suspendContext(currentCntxt);
  incrWaitingCntxt();
  while (cntxtList[currentCntxt].waiter != 0) {
    cntxtYield();
  }
  decrWaitingCntxt();

  /* Remove from CV queue if we are still there (e.g. timeout or spurious wake) */
  if (C_HEAD == me) {
      C_HEAD = cntxtList[me - 1].next_waiter;
      if (C_HEAD == 0) C_TAIL = 0;
  } else {
      /* Need to search if we are in the middle of the queue */
      int curr = C_HEAD;
      while (curr != 0) {
          int next = cntxtList[curr - 1].next_waiter;
          if (next == me) {
              cntxtList[curr - 1].next_waiter = cntxtList[me - 1].next_waiter;
              if (C_TAIL == me) C_TAIL = curr;
              break;
          }
          curr = next;
      }
  }

  pthread_mutex_lock(__mutex);
  return 0;
}

int pthread_cond_timedwait (pthread_cond_t *__restrict __cond,
                            pthread_mutex_t *__restrict __mutex,
                            const struct timespec *__restrict __abstime) {
  int me = currentCntxt + 1;
  if (debug) printf(" |c timewait@ %p ctx=%i current=%li.%09li target=%li.%09li\n",
                    (void*)__cond, me-1, monotonic_time.tv_sec, monotonic_time.tv_nsec,
                    __abstime->tv_sec, __abstime->tv_nsec);

  if (!lessThan(&monotonic_time, __abstime)) {  /* monotonic_time >= abstime: already expired */
      if (debug) printf(" |TIMEOUT-PRE %i\n", me-1);
      return 110; /* ETIMEDOUT */
  }

  pthread_mutex_unlock(__mutex);

  cntxtList[currentCntxt].next_waiter = 0;
  if (C_TAIL == 0) {
    C_HEAD = C_TAIL = me;
  } else {
    cntxtList[C_TAIL - 1].next_waiter = me;
    C_TAIL = me;
  }

  holdContext(*__abstime, currentCntxt);
  incrWaitingCntxt();
  while (cntxtList[currentCntxt].waiter != 0) {
    if (!lessThan(&monotonic_time, __abstime)) {  /* monotonic_time >= abstime: expired */
        if (debug) printf(" |TIMEOUT %i\n", currentCntxt);
        cntxtList[currentCntxt].waiter = 0;
        break;
    }
    cntxtYield();
  }
  decrWaitingCntxt();

  /* Remove from CV queue if we are still there */
  if (C_HEAD == me) {
      C_HEAD = cntxtList[me - 1].next_waiter;
      if (C_HEAD == 0) C_TAIL = 0;
  } else {
      int curr = C_HEAD;
      while (curr != 0) {
          int next = cntxtList[curr - 1].next_waiter;
          if (next == me) {
              cntxtList[curr - 1].next_waiter = cntxtList[me - 1].next_waiter;
              if (C_TAIL == me) C_TAIL = curr;
              break;
          }
          curr = next;
      }
  }

  pthread_mutex_lock(__mutex);
  int res = 0;
  if (!lessThan(&monotonic_time, __abstime)) {  /* monotonic_time >= abstime: timed out */
      res = 110; /* ETIMEDOUT */
  }
  /* Clear our wait time so we don't trigger the scheduler jump to the past */
  cntxtList[currentCntxt].wait.tv_sec = 0;
  cntxtList[currentCntxt].wait.tv_nsec = 0;
  return res;
}

/*************************************************************
*
* GNAT Self (per-context ATCB)
*
* GNAT uses pragma Thread_Local_Storage (ATCB) in s-tpopsp.adb.
* All simulated contexts share one OS thread and thus one TLS slot.
* system__task_primitives__operations__self is called via PLT so we
* can intercept it.  On first call from each context we read the real
* TLS value (via RTLD_NEXT) and cache it; thereafter we return the
* cached value regardless of what TLS now holds.
*
**************************************************************/

void *system__task_primitives__operations__self(void) {
  if (per_context_atcb[currentCntxt] == NULL) {
    typedef void *(*fn_t)(void);
    static fn_t real_self = NULL;
    if (!real_self)
      real_self = (fn_t)dlsym(RTLD_NEXT,
                               "system__task_primitives__operations__self");
    if (real_self)
      per_context_atcb[currentCntxt] = real_self();
    if (debug) fprintf(stderr, " |SELF ctx=%i atcb=%p\n", currentCntxt, per_context_atcb[currentCntxt]);
  }
  return per_context_atcb[currentCntxt];
}

/*************************************************************
*
* Time
*
**************************************************************/

int gettimeofday (struct timeval *__restrict __tv, void * __tz)
{
  (__tv)->tv_sec = monotonic_time.tv_sec;
  (__tv)->tv_usec = (monotonic_time.tv_nsec+999) / 1000;
  return 0;
}

int clock_gettime(clockid_t ct, struct timespec *__tv)
{
  (__tv)->tv_sec = monotonic_time.tv_sec;
  (__tv)->tv_nsec = monotonic_time.tv_nsec;
  return 0;
}
