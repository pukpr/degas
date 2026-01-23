# DEGAS Testing - Traffic Light Tests

## Test Files

### traffic_simple.adb
Simple 2-task test with entry calls. Each task has a `Start` entry that the main task calls.

**Status:**
- ✅ Works in real-time mode (without degas.so)
- ❌ Deadlocks with degas.so runtime

### traffic_light.adb  
Multi-intersection traffic light simulation with 3 tasks and entry calls.

**Status:**
- ✅ Works in real-time mode (without degas.so)
- ❌ Deadlocks with degas.so runtime

### traffic_one.adb
Single-task test for comparison (created for debugging).

**Status:**
- ✅ Works in real-time mode
- ✅ Works with degas.so runtime

## Root Cause Analysis

The deadlock with multiple tasks is caused by a **fundamental limitation in the SPINLOCK-based condition variable implementation** in degas.c.

### The Problem

The current `pthread_cond_t` implementation (lines 625-676 in degas.c) uses a single `SPINLOCK` field per condition variable to store the ID of ONE waiting context:

```c
#define SPINLOCK __cond->__data.__wrefs  // Line 625
```

When a task calls `pthread_cond_wait()` (line 662):
1. It stores its context ID in SPINLOCK: `SPINLOCK = currentCntxt + 1` (line 667)
2. It loops until SPINLOCK is cleared: `while (SPINLOCK != 0 && !releaseContext())` (line 669)

When `pthread_cond_signal()` is called (line 643):
1. It reads the waiter ID from SPINLOCK (line 646)
2. Schedules that context to run (line 647)
3. Clears SPINLOCK (line 648)

### Why It Fails with Multiple Tasks

The GNAT Ada runtime uses **multiple condition variables simultaneously** for implementing entry calls. With 2+ tasks:

1. Task1 waits on CV_A (for entry call machinery)
2. Task2 waits on CV_B (for its own entry call machinery)  
3. Both tasks may also wait on additional CVs for rendezvous synchronization

The problem occurs when:
- Multiple CVs are active simultaneously
- The scheduler can't properly coordinate all the SPINLOCK values
- Tasks get stuck in `pthread_cond_wait` loops that never exit

### Why Single-Task Tests Work

With only one task (`simple_ada_test`, `traffic_one`):
- Only one CV is actively waiting at a time
- SPINLOCK mechanism works correctly
- Signals properly wake the waiting task

### Required Fix

To fully support multiple tasks with entry calls, the implementation needs:

**Option 1: Waiter Queue per CV**
```c
typedef struct {
    int waiters[MAX_THREADS];
    int count;
} WaiterQueue;
```
Store a queue of waiting contexts per CV instead of a single SPINLOCK value.

**Option 2: Global Waiter Registry**
Maintain a global registry mapping CVs to waiting contexts, allowing multiple waiters per CV.

**Option 3: Use Real Pthread Mechanisms**
If the simulation environment allows, use actual pthread mutexes/CVs from the host OS.

## Testing

### Build
```bash
./build.sh
```

### Test without degas.so (real-time)
```bash
./traffic_simple  # Should work
./traffic_light   # Should work
```

### Test with degas.so (simulated time)
```bash
LD_PRELOAD=./Linux/libdegas.so ./traffic_simple  # Currently deadlocks
LD_PRELOAD=./Linux/libdegas.so ./traffic_light   # Currently deadlocks
```

### Working tests with degas.so
```bash
LD_PRELOAD=./Linux/libdegas.so ./simple_ada_test  # Works (1 task)
LD_PRELOAD=./Linux/libdegas.so ./traffic_one      # Works (1 task)
```

## References

- degas.c: Main simulation library
- degas_original.c: Original version for comparison
- simple_ada_test.adb: Working single-task example
