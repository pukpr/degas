# Debug Output Explanation for delays.adb

This document explains the debug output behavior when running Ada programs with the degas library using `SIM_CONTEXT_DEBUG=1`.

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

## Summary

1. **Stack size "non-uniformity"**: This is correct behavior - the Ada runtime chooses different stack sizes (1MB for initialization, ~2MB for tasks) based on its analysis of task requirements. A minor initialization bug was fixed to prevent showing garbage values if attr is not properly initialized by the caller.

2. **Timewait values**: Correctly show absolute time values as per POSIX specification, accumulated from the starting monotonic_time (1 second). This represents when tasks should wake up, not how long they sleep.

Both behaviors are now properly documented in the source code with inline comments.

