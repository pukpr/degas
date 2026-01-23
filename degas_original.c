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

#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <values.h>

#define MAX_THREAD_KEYS 10  /* used by tasking to hold self data */
#define DEFAULT_STACK_SIZE 1000000
#define MAX_THREADS 1000 /* max threads that can be active at once */
#define MAX_DEADLOCKS 1000  /* max deadlock attempts to fire */

char * SIM_CONTEXT_DEBUG = "SIM_CONTEXT_DEBUG";

struct timespec monotonic_time = {0, 0};
const struct timespec zerotime = {0, 0};
const struct timespec max_time = {MAXLONG, MAXLONG};

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
} Cntxt;

Cntxt cntxtList[MAX_THREADS];       /* The Cntxt "queue" */
int currentCntxt = 0;               /* The index of currently executing Cntxt */
int numCntxts = 0;                  /* The number of Cntxts */
int numActiveCntxts = 0;            /* The number of active Cntxts */
/* The number of waiting Cntxts.  When this equals numActiveCntxts the scheduler
   will move time forward to the Cntxt with minimum wait time. */
int numWaitingCntxts = 0;

typedef struct {
  void * KA[MAX_THREADS];
  int set;
} KeyAddress;

KeyAddress addresses[MAX_THREAD_KEYS];

void Scheduler_init(void) {
  if (!initializedScheduler) {
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
  numActiveCntxts += 1;
  func(cntxtList[currentCntxt].arg);
  numActiveCntxts -= 1;
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
  cntxtList[numCntxts].wait.tv_sec = 0.0;
  cntxtList[numCntxts].wait.tv_nsec = 0.0;

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
  struct timespec minTime = max_time; // {MAXLONG, MAXLONG};
  int minCntxt = currentCntxt;
  int i;
  Deadlock += 1;
  for (i=0; i < numCntxts+1; i++) {
    if (cntxtList[i].waiter) {
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

void cntxtYield() {
  int lastCntxt = currentCntxt;

  do { /*  Round-robin loop */
    currentCntxt = (currentCntxt + 1) % (numCntxts + 1);
  } while (cntxtList[currentCntxt].finished);

  if (cntxtsAllSleeping()) {
    currentCntxt = findMinWaitingCntxt();
    printDebug("! SCHEDULE", currentCntxt, Deadlock);
    cntxtList[currentCntxt].waiter = 0;
  }
  printDebug("#sw", lastCntxt, currentCntxt);

  swapcontext(&cntxtList[lastCntxt].context,
              &cntxtList[currentCntxt].context);
}

int getCurrentCntxt() {
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
# define STACKSIZE __attr->__stacksize
#endif

int pthread_create (pthread_t *__restrict __threadp,
                    __const pthread_attr_t *__restrict __attr,
                    void *(*__start_routine) (void *),
                    void *__restrict __arg) {
  int res;
  int val;
  size_t size = DEFAULT_STACK_SIZE;
  if (STACKSIZE > 0) {
    size = STACKSIZE;
  } else {
    val = pthread_attr_setstacksize(__attr, size);
  }

  char *stack = malloc(size);

  res = spawnCntxt((void *)__start_routine,
                   stack,
                   (int)STACKSIZE,
                   __arg);
  __threadp = (pthread_t*)res;
  printDebug("p CREATE", (int)__threadp, (int)STACKSIZE);
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
  addresses[(int)__key].KA[getCurrentCntxt()] = __pointer;
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
  printDebug("p attrstacksize", (int)__stacksize, 0);
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
# define MCOUNT __mutex->__D[0]
#elif SUNOS==1
# define MCOUNT __mutex->__pthread_mutex_data
#else
# define MCOUNT __mutex->__m_count
#endif

int pthread_mutex_init (pthread_mutex_t *__restrict __mutex,
                        __const pthread_mutexattr_t *__restrict __mutex_attr) {
 Scheduler_init();  /* in case not init'ed */
 MCOUNT = 0;
 printDebug("m init", MCOUNT, 0);
 return 0;
}

int pthread_mutex_destroy (pthread_mutex_t *__mutex) {
  printDebug("m destroy", MCOUNT, 0);
  return 0;
}

int pthread_mutex_lock (pthread_mutex_t *__mutex) {
  printDebug("m lock", MCOUNT, currentCntxt);
  incrWaitingCntxt();
  while (MCOUNT != 0) {
    sched_yield();
  }
  MCOUNT = currentCntxt + 1; /* Add one for Context=0 */
  decrWaitingCntxt();
  //  sched_yield();
  return 0;
}

int pthread_mutex_unlock (pthread_mutex_t *__mutex) {
  /* no debugging, as print causes seg fault on task exit */
  /* printDebug("m unlock", MCOUNT, 0); */
  if (MCOUNT != 0) {
     holdContext(zerotime, MCOUNT-1); /* schedules locked context to wakeup */
     MCOUNT = 0;
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
# define SPINLOCK __cond->__c_lock.__spinlock
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
  printDebug("c signal", SPINLOCK-1, 0);
  if (SPINLOCK != 0) {
     holdContext(zerotime, SPINLOCK-1);  /* schedules the waiting context */
     SPINLOCK = 0;
  }
  /* should there be a sched_yield() here? */
  return 0;
}

int pthread_cond_wait (pthread_cond_t *__restrict __cond,
                       pthread_mutex_t *__restrict __mutex) {
  printDebug("c wait", SPINLOCK, currentCntxt);
  pthread_mutex_unlock(__mutex);
  SPINLOCK = currentCntxt + 1;  /* Add one for Context=0 */
  incrWaitingCntxt();
  while (SPINLOCK != 0) {
    sched_yield();
  }
  decrWaitingCntxt();
  pthread_mutex_lock(__mutex);
  /* should there be a sched_yield() here? */
  return 0;
}

int pthread_cond_timedwait (pthread_cond_t *__restrict __cond,
                            pthread_mutex_t *__restrict __mutex,
                            __const struct timespec *__restrict __abstime) {
  printDebug("c timewait", (int)__abstime->tv_sec, (int)__abstime->tv_nsec);
  pthread_mutex_unlock(__mutex);
  holdContext(*__abstime, getCurrentCntxt());
  incrWaitingCntxt();
  while (!releaseContext()) {
    sched_yield();
  }
  decrWaitingCntxt();
  monotonic_time = *__abstime;
  pthread_mutex_lock(__mutex);
  /* should there be a sched_yield() here? */
  return 0;
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
                  __timezone_ptr_t __tz)
#endif
{
  /* convert from timespec to timeval.  always round up nsec. */
  (__tv)->tv_sec = monotonic_time.tv_sec;
  (__tv)->tv_usec = (monotonic_time.tv_nsec+999) / 1000;
  return 0;
}

/* More accurate version, but not used on Linux */
int clock_gettime(clockid_t ct, struct timespec *__tv)
{
  (__tv)->tv_sec = monotonic_time.tv_sec;
  (__tv)->tv_nsec = monotonic_time.tv_nsec;
  return 0;
}
