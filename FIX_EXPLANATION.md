# Fix for Task Scheduling Issue with Non-blocking Tasks

## Problem
The `tl.adb` program hung when run with the degas library (`LD_PRELOAD=./Linux/libdegas.so`). The program has two tasks:
- **NorthSouth**: A task with an entry definition but no accept statement (runs to completion without blocking)
- **EastWest**: A task that waits for an entry call (blocks on condition variable)

The issue occurred because the scheduler didn't properly handle the case where one task runs without blocking while another task is waiting.

## Root Cause
Two related issues in the scheduler:

### 1. Round-robin Scheduler Didn't Skip Waiting Contexts
The `cntxtYield()` function's round-robin loop only skipped finished contexts:
```c
do {
  currentCntxt = (currentCntxt + 1) % (numCntxts + 1);
} while (cntxtList[currentCntxt].finished);
```

This meant that contexts marked as waiting (`waiter == 1`) could still be selected during round-robin scheduling. When a context is waiting on a condition variable, it should not be scheduled until signaled.

### 2. Non-atomic pthread_cond_wait
The `pthread_cond_wait()` function had a race condition:
```c
pthread_mutex_unlock(__mutex);  // This yields internally!
cv_add_waiter(__cond, currentCntxt);  // Added AFTER yielding
holdContext(max_time, currentCntxt);
```

Between unlocking the mutex and adding to the CV waiter queue, the context could get rescheduled (because `pthread_mutex_unlock` calls `sched_yield()`). This broke atomicity and allowed the context to be rescheduled before it was properly marked as waiting.

### Combined Effect
These two issues combined to create an infinite loop:
1. Context enters `pthread_cond_wait`
2. Unlocks mutex (which yields)
3. Gets rescheduled by round-robin before being added to waiter queue
4. Re-enters `pthread_cond_wait` and adds itself to queue again
5. Queue eventually fills with duplicate entries (MAX_WAITERS_PER_CV = 10)
6. System deadlocks

## Solution

### Fix 1: Skip Waiting Contexts in Round-Robin
Modified `cntxtYield()` to skip contexts that are waiting, unless all contexts are sleeping:
```c
void cntxtYield() {
  size_t lastCntxt = currentCntxt;
  int allSleeping = cntxtsAllSleeping();

  /* Skip finished contexts during round-robin.
   * Also skip waiting contexts UNLESS all contexts are sleeping. */
  do {
    currentCntxt = (currentCntxt + 1) % (numCntxts + 1);
  } while (cntxtList[currentCntxt].finished || 
           (cntxtList[currentCntxt].waiter && !allSleeping));

  if (allSleeping) {
    // ... handle all-sleeping case
  }
}
```

This ensures that:
- Active (non-waiting, non-finished) contexts are scheduled in round-robin
- Waiting contexts are only scheduled when all contexts are sleeping (by the time-based scheduler)
- The scheduler makes forward progress even when some tasks don't block

### Fix 2: Ensure pthread_cond_wait Atomicity
Reordered operations in `pthread_cond_wait()` to mark the context as waiting BEFORE unlocking the mutex:
```c
int pthread_cond_wait(pthread_cond_t *__cond, pthread_mutex_t *__mutex) {
  /* Add this context to the waiter queue BEFORE unlocking the mutex.
   * This ensures atomicity - the context is registered as waiting before
   * it can be rescheduled by the yield in mutex_unlock. */
  cv_add_waiter(__cond, currentCntxt);
  holdContext(max_time, currentCntxt);
  incrWaitingCntxt();
  
  /* Now unlock the mutex - this may yield but we're already marked as waiting */
  pthread_mutex_unlock(__mutex);
  
  /* Wait until signaled */
  while (!releaseContext()) {
    sched_yield();
  }
  
  decrWaitingCntxt();
  pthread_mutex_lock(__mutex);
  return 0;
}
```

This ensures that:
- The context is added to the CV waiter queue before any potential reschedule
- The context is marked as waiting before mutex unlock can trigger a yield
- No duplicate additions to the waiter queue can occur

## Testing
Created C test programs that mirror the Ada scenario:
- `test_scheduler.c`: Basic test with one waiting and one non-waiting task
- `test_tl_scenario.c`: More accurate simulation of the tl.adb scenario

Both tests pass successfully with the fixes applied.

## Files Modified
- `degas.c`: Applied both fixes to the scheduler
- `.gitignore`: Added test binaries
