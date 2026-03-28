# Degas Project Status

## Project Context
The `degas` project aims to simulate Ada tasking by intercepting `pthread` calls using `LD_PRELOAD`. It uses `ucontext` for user-level threading and implements a simulated monotonic clock. The current implementation was outdated and incompatible with modern glibc behavior.

## Current Progress

### Findings
1.  **Deadlock on Startup:** The main thread (context 0) was entering a wait state before new tasks (contexts 1 and 2) had a chance to increment the active context count, leading to premature "global deadlock" detection.
2.  **Scheduler Loop/Hang:** Contexts were becoming stuck in tight loops (specifically context 2 in the `r` test). This was caused by `cntxtsAllSleeping()` returning true while ready tasks were still available, causing the scheduler to incorrectly advance time instead of yielding to ready tasks.
3.  **Synchronization Bugs:**
    *   `pthread_mutex_lock` did not correctly transition contexts back to a "ready" state after they were suspended while waiting for a lock.
    *   `pthread_cond_wait` and `pthread_cond_timedwait` were missing queueing logic or failed to reset the `waiter` status upon waking, causing contexts to stay suspended or in timer-wait mode indefinitely.
    *   `readyContext` did not clear the `wait` timespec, potentially allowing the scheduler to wake a task twice (once for signal, once for timeout).
4.  **Context 0 Busy-Wait:** Context 0 (main thread) frequently calls `pthread_cond_timedwait` with very short timeouts (10ms). While the scheduler correctly advances time to these points, context 0 appears to be busy-waiting for other tasks to progress, leading to high-volume debug output and slow execution.

### Fixes Applied
1.  **Accounting:** Moved `numActiveCntxts` increment to `spawnCntxt` to ensure new tasks are accounted for immediately upon creation.
2.  **Robust Scheduling:** 
    *   Added `cntxtReady()` helper to verify if any non-finished task is in a ready state (`waiter == 0`).
    *   Updated `cntxtsAllSleeping()` to ensure time only advances if no tasks are ready.
3.  **Mutex Logic:** Updated `pthread_mutex_lock` to explicitly set ownership (`MCOUNT = me`) and clear the waiter state (`readyContext`) after waking from a suspension.
4.  **Condition Variables:** 
    *   Added missing queueing logic to `pthread_cond_timedwait`.
    *   Updated both `pthread_cond_wait` and `pthread_cond_timedwait` to loop until the `waiter` status is cleared and correctly handle removal from the waiter queue.
    *   Ensured `pthread_cond_timedwait` checks against `monotonic_time` to detect timeouts.
5.  **State Management:** Updated `readyContext` to clear the `wait` timespec.
6.  **Observability:** Added `FinalReport` function and `atexit` handler to dump state on exit. Improved debug logging for `pthread_cond_timedwait` and `cntxtsAllSleeping`.
7.  **Time Progression:** Modified `cntxtYield` to allow time advancement when context 0 is yielding, even if it is technically ready, to attempt to skip busy-wait periods. Introduced a 1ms "force increment" when context 0 yields but no timer is pending.

## Current Status
The `degas` library now correctly handles basic task spawning and synchronization. However, the Ada runtime's main thread (context 0) remains stuck in a busy-wait loop, calling `pthread_cond_timedwait` with 10ms intervals. This prevents the simulated time from efficiently reaching the longer delays (0.5s and 1.0s) requested by the Ada tasks.

## Plan
1.  **Validate Force Increment:** Test if the 1ms artificial time increment in `cntxtYield` allows context 0 to eventually exit its loop and reach the next scheduled task event.
2.  **Aggressive Time Jumping:** If 1ms is too slow, adjust the scheduler to jump directly to the next `findMinWaitingCntxt()` whenever context 0 yields, regardless of whether it's "sleeping" or just "ready but yielding".
3.  **Audit Interceptions:** Verify if other synchronization primitives (e.g., `pthread_yield`, `sched_yield`, or semaphores) are being used by the Ada runtime and need interception to prevent "real" yielding from breaking the simulated time logic.
4.  **Verify `r` and `s` Tests:** Ensure both test cases produce the expected interleaved output once the busy-wait issue is resolved.

