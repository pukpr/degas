# Investigation: tl.adb Hanging Issue with LD_PRELOAD

## Problem Summary
The `tl.adb` program (and other multi-task Ada programs with entry calls) hangs when run with `LD_PRELOAD=./Linux/libdegas.so`. The program works correctly without the preload.

## Investigation Findings

### Test Results
1. ✅ Two tasks WITHOUT entry calls: Works fine (`tl_simple2.adb`)
2. ✅ One task WITH entry calls: Works fine (`tl_one.adb`, `simple_ada_test.adb` partially)
3. ❌ Two tasks WITH entry calls: Hangs during initialization (`tl.adb`)

### Root Cause Analysis

The hang is caused by a **race condition in the `pthread_cond_wait` implementation** combined with **scheduler behavior**:

1. **Atomicity Issue in pthread_cond_wait**:
   ```c
   // Current problematic sequence:
   pthread_mutex_unlock(__mutex);  // This yields internally!
   cv_add_waiter(__cond, currentCntxt);  // Added AFTER yielding
   holdContext(max_time, currentCntxt);
   ```
   
   Between unlocking the mutex and adding to the CV waiter queue, the context gets rescheduled (because `pthread_mutex_unlock` calls `sched_yield()`). This breaks the atomicity requirement of condition variable waits.

2. **Scheduler Doesn't Respect Waiter Status**:
   The `cntxtYield()` function uses round-robin scheduling that doesn't skip contexts marked as waiting. When a context is waiting on a CV, it still gets scheduled in round-robin, causing it to re-enter `pthread_cond_wait` multiple times.

3. **Context Re-adds Itself to Waiter Queue**:
   Due to issues #1 and #2, context 0 repeatedly:
   - Enters `pthread_cond_wait`
   - Gets rescheduled before properly waiting
   - Re-enters and adds itself to the queue again
   - Result: Queue fills with duplicate entries for the same context

### Debug Trace Analysis

Infinite loop pattern observed:
```
|c wait 0 0           <- pthread_cond_wait called
|m unlock 1 0          <- Mutex unlocked (yields)
|cv+ add_waiter 0 N    <- Context added to queue (N increases)
|! SCHEDULE 0 0        <- Scheduler picks same context again
(repeat)
```

Eventually the waiter queue saturates (MAX_WAITERS_PER_CV = 10), and the system deadlocks with:
- Context 0: Spinning in cond_wait loop
- Contexts 1 & 2: Blocked waiting for mutexes held by context 0
- No forward progress possible

## Attempted Fixes

###  1. Waiter Queue Implementation
- ✅ Replaced single SPINLOCK with queue system (MAX_WAITERS_PER_CV)
- ✅ Supports multiple waiters per condition variable
- ❌ Doesn't solve the atomicity/scheduler issue

### 2. Reorder Operations in pthread_cond_wait
- Tried: Add to queue BEFORE unlocking mutex
- Result: Partial improvement (no duplicate adds) but still hangs
- Issue: Scheduler still picks waiting contexts

### 3. Modify Scheduler to Skip Waiting Contexts
- Tried: Skip contexts with `waiter == 1` in round-robin
- Result: Segfaults or infinite loops
- Issue: Complex interaction between round-robin and all-sleeping modes

## Proper Solution (Not Yet Implemented)

The fix requires coordinated changes:

### A. Fix pthread_cond_wait Atomicity
```c
int pthread_cond_wait(pthread_cond_t *__cond, pthread_mutex_t *__mutex) {
  // 1. Add to waiter queue FIRST
  cv_add_waiter(__cond, currentCntxt);
  holdContext(max_time, currentCntxt);
  incrWaitingCntxt();
  
  // 2. Unlock mutex WITHOUT yielding back to this context
  //    (need a "unlock_and_dont_reschedule_me" variant)
  pthread_mutex_unlock_no_return(__mutex);
  
  // 3. This point should only be reached after being signaled
  while (!releaseContext()) {
    sched_yield();
  }
  
  decrWaitingCntxt();
  pthread_mutex_lock(__mutex);
  return 0;
}
```

### B. Modify Scheduler Round-Robin Logic
```c
void cntxtYield() {
  // Skip contexts that are waiting (waiter == 1) during round-robin
  // Only schedule waiting contexts when cntxtsAllSleeping() == true
  
  do {
    currentCntxt = (currentCntxt + 1) % (numCntxts + 1);
  } while (cntxtList[currentCntxt].finished || 
           (cntxtList[currentCntxt].waiter && !cntxtsAllSleeping()));
  
  // ... rest of scheduler logic
}
```

### C. Special Case for Mutex Unlock Within Cond Wait
Consider adding a flag to track "unlocking for cond wait" vs "normal unlock":
- Normal unlock: Yield to give others a chance
- Cond wait unlock: Yield but don't return to caller

## Testing Approach

1. Implement atomicity fix first
2. Test with `tl_one.adb` (should still work)
3. Implement scheduler fix
4. Test with `tl.adb` (should now work)
5. Test with all traffic_*.adb programs
6. Verify no regressions in `simple_ada_test`, `delays`, etc.

## Additional Notes

- The SPINLOCK-based implementation had similar issues but manifested differently
- The waiter queue is necessary but not sufficient to fix the problem
- The core issue is the interaction between pthread operations and the cooperative scheduler
- Real pthread implementations handle this with kernel-level blocking, which we simulate

## Files Modified

- `degas.c`: Added waiter queue system, modified pthread_cond_* functions
- `tl_one.adb`: Created for testing (one task with entry - works)
- `tl_simple2.adb`: Created for testing (two tasks without entry - works)
