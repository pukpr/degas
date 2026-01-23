# Debug Output Explanation for DEGAS

This document explains the debug output behavior when running Ada programs with the degas library using `SIM_CONTEXT_DEBUG=1`.

## Important: Complete Determinism in DEGAS

**DEGAS provides complete determinism through a simulated clock**, which is fundamentally different from real-time software that depends on the vagaries of the always-changing system clock.

### Key Properties

1. **Simulated Time**: The clock in degas.c is completely simulated and **always gives the same response** for the same sequence of operations. There is no dependency on wall-clock time.

2. **Deterministic Execution**: Given the same program and inputs, DEGAS will produce identical execution traces, thread interleavings, and timing behavior every time. This is critical for:
   - Understanding debugging output
   - Maintaining regression tests on "working" tests
   - Reproducing bugs consistently
   - Verifying timing-dependent behavior

3. **Time Advancement**: Unlike real-time systems where time flows continuously, DEGAS advances its simulated `monotonic_time` only when:
   - All active threads are waiting on timed conditions
   - The scheduler jumps directly to the next wakeup time (no idle waiting)

4. **No Race Conditions**: Because time is simulated and controlled by the scheduler, there are no timing-related race conditions or non-deterministic behavior from system clock variations.

### Understanding Debug Output Values

The debug output shows the internal state of the simulated system. All time values are deterministic and reproducible across runs. When you see variations or unexpected values, they indicate either:
- Different code paths being exercised
- Potential issues with time calculation (see sections below)
- Uninitialized memory (which should be investigated and fixed)

This determinism is what makes DEGAS valuable for testing concurrent Ada programs - you can trust that the same test will produce the same debug output every time.

## Example Output

```bash
SIM_CONTEXT_DEBUG=1 LD_PRELOAD=./Linux/libdegas.so ./delays
```

## Issue 1: Non-Uniform Stack Size (attrsetstacksize)

### Observation
The debug output shows different stack sizes:
```
|p attrsetstacksize 1000000 0    # First call during degas initialization (1MB)
|p attrsetstacksize 2129920 0    # Later calls when Ada runtime creates tasks (2.03MB)
```

### Explanation
**This is CORRECT behavior**: The different stack sizes reflect the Ada runtime's stack allocation strategy.

1. **First call (1000000 = 1MB)**: Occurs during `Scheduler_init()` in degas.c (line 108), which sets the DEFAULT_STACK_SIZE for the degas scheduler's internal use.

2. **Subsequent calls (2129920 ≈ 2MB)**: The Ada runtime (GNAT) determines the appropriate stack size for each task based on various factors including:
   - Task complexity
   - Local variable sizes
   - Compiler settings
   - Runtime library defaults

The Ada runtime autonomously calls `pthread_attr_setstacksize()` with its calculated stack size for each task (R1 and R2 in this case).

### Previous Issue (Now Fixed)
There was a bug where `pthread_attr_init()` did not initialize the stack size field to 0 (line 399 was commented out). This caused uninitialized memory values to appear in debug output when the Ada runtime created a pthread_attr_t on the stack without clearing it first. The fix ensures that the field is properly initialized, though this doesn't change the final values shown (since the Ada runtime immediately calls setstacksize afterward).

**Code Fix**:
```c
int pthread_attr_init (pthread_attr_t *__attr) {
  printDebug("p attrinit", 0, 0);
  STACKSIZE = 0;  // Now properly initialized (was commented out)
  return 0;
}
```

## Issue 2: Timewait Debug Values

### Observation
The timewait debug values show increasing numbers:
```
|c timewait 2 0     # 2 seconds
|c timewait 3 0     # 3 seconds  
|c timewait 4 0     # 4 seconds
|c timewait 15 0    # 15 seconds
```

### Explanation
**This is CORRECT behavior**: The values represent **absolute time** from the monotonic clock, not relative delays.

- `pthread_cond_timedwait()` takes an absolute time value as per POSIX specification
- The monotonic_time starts at 1 second (initialized in `Scheduler_init()` line 92)
- When a task calls `delay 1.0`, it calculates an absolute wakeup time: current_time + 1.0
- Examples from delays.adb:
  - Task R1 delays 1.0 at time 1 → wakes at time 2
  - Task R2 delays 2.0 at time 1 → wakes at time 3
  - Main delays 2.0 at time 0 → wakes at time 2

### Timeline Example

From the delays.adb program:
```ada
task body R1 is
begin
    for I in 1 .. 10 loop
       delay 1.0;  -- Each delay of 1 second
    end loop;
end;

task body R2 is
begin
    for I in 1 .. 5 loop
       delay 2.0;  -- Each delay of 2 seconds
    end loop;
end;

begin
    for I in 1 .. 5 loop
       delay 2.0;  -- Main task delays 2 seconds
    end loop;
    delay 5.0;     -- Final delay of 5 seconds
end;
```

The scheduler advances the simulated time to the next wakeup time when all tasks are sleeping. The timewait values show when each task should wake up:

- Time 2: R1 first wakeup, Main first wakeup
- Time 3: R2 first wakeup, R1 second wakeup
- Time 4: Main second wakeup, R1 third wakeup
- Time 5: R2 second wakeup, R1 fourth wakeup
- ...and so on

This allows the simulator to efficiently advance time without waiting for real wall-clock time to pass.

## Issue 3: Suspicious Timewait Values (e.g., 633437444 854775807)

### Observation
In some test outputs, particularly with `simple_ada_test.adb`, extremely large timewait values appear:
```
|c timewait 633437444 854775807
```

Where:
- 633437444 seconds ≈ 20 years
- 854775807 nanoseconds ≈ 0.85 seconds

### Explanation
**These values appear suspicious and warrant investigation**. Given DEGAS's deterministic nature, such values should be reproducible and consistent across runs.

Possible causes:
1. **Uninitialized timespec structure**: The Ada runtime might be passing a partially initialized `timespec` to `pthread_cond_timedwait()`. If only `tv_sec` is set (or vice versa), the other field could contain garbage values from the stack.

2. **CLOCK_REALTIME vs CLOCK_MONOTONIC**: The Ada runtime might be using `clock_gettime(CLOCK_REALTIME, ...)` to compute absolute times. Since DEGAS's `clock_gettime()` implementation (lines 673-678) doesn't distinguish between clock types, it always returns the simulated `monotonic_time`. If the Ada runtime expects real wall-clock time (seconds since Unix epoch, ~1.7 billion in 2024), the resulting calculation could produce unexpected values.

3. **Integer overflow in time arithmetic**: When the Ada runtime computes `current_time + delay`, overflow or wraparound could occur depending on how the calculation is performed.

### Deterministic Nature
**Important**: Because DEGAS is completely deterministic, these large values should appear consistently in the same place during regression testing. If the values change between runs of the same test, that would indicate a serious bug in DEGAS itself (such as using uninitialized memory or reading actual system time instead of simulated time).

For the `simple_ada_test.adb` example, the value `|c timewait 633437444 854775807` appears specifically at "Worker: After Done accept 2", and should appear at the same location every time the test runs with the same DEGAS library.

### Debugging These Values
To understand these values:
1. Verify they are reproducible across multiple runs (confirming determinism)
2. Compare with "normal" timewait values in the same trace (e.g., `|c timewait 1 0`, `|c timewait 2 0`)
3. Examine what the Ada runtime is doing at that point in the code
4. Check if the program still executes correctly despite the unusual values

**Note**: Even if the values look suspicious, if the test "works" (completes successfully with correct output), the scheduler is correctly handling whatever time value was provided. The deterministic nature ensures regression tests will catch any changes in behavior.

## Summary

**DEGAS provides complete determinism**: The simulated clock always produces the same behavior for the same program, unlike real-time software that depends on the system clock. This determinism is essential for understanding debug output and maintaining reliable regression tests.

1. **Stack size "non-uniformity"**: This is correct behavior - the Ada runtime chooses different stack sizes (1MB for initialization, ~2MB for tasks) based on its analysis of task requirements. A minor initialization bug was fixed to prevent showing garbage values if attr is not properly initialized by the caller.

2. **Timewait values**: Correctly show absolute time values as per POSIX specification, accumulated from the starting monotonic_time (1 second). This represents when tasks should wake up, not how long they sleep.

3. **Suspicious large timewait values**: Values like `633437444 854775807` appear in some tests but should be deterministic (reproducible across runs). They may indicate issues with Ada runtime's time calculation or clock type usage, but if the test works correctly, the scheduler is handling them properly.

Both behaviors are now properly documented in the source code with inline comments.

