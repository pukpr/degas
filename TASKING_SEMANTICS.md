# Ada Tasking Semantics and degas Implementation Notes

## Overview

`degas` simulates Ada's tasking model on a single OS thread using POSIX
`ucontext` for cooperative context switching and a fully synthetic monotonic
clock.  The Ada runtime (GNAT/GNARL) believes it is running atop real
pthreads; `degas` intercepts the pthread API via `LD_PRELOAD` to redirect
every synchronisation call through the cooperative scheduler.

---

## Ada Tasking Primitives and Their pthread Mappings

### 1. Task Creation

| Ada concept | GNAT runtime call | degas action |
|---|---|---|
| `task T` declaration | `pthread_create` | `spawnCntxt()` — allocates a `ucontext_t` on GNAT's stack, schedules it via round-robin |
| Task body entry | `CntxtStart()` | runs the task function; decrements `numActiveCntxts` on return |

GNAT passes the real stack it allocated; degas stores the pointer and size
in the `Cntxt` struct and calls `makecontext` to set the entry point to
`CntxtStart(func)`.

### 2. Delays — `delay D`

| Ada | GNAT call | degas action |
|---|---|---|
| `delay D` | `clock_gettime` + `pthread_cond_timedwait` | sets `cntxtList[i].waiter = 1` and `wait = now + D`; yields |

`clock_gettime` is intercepted to return `monotonic_time`.  When all
contexts are sleeping the scheduler (`cntxtsAllSleeping`) advances
`monotonic_time` to the minimum `wait` across all timer-waiting contexts
and wakes that context.

### 3. Rendezvous — `task entry E` / `accept E`

Ada direct rendezvous is the foundational synchronisation form:

- **Acceptor** (server task): executes `accept E do … end E`, which blocks
  until a caller arrives.
- **Caller** (client task): executes `T.E(…)`, which blocks until the
  server reaches its `accept`.
- Both sides rendezvous inside the accept body, then both continue.

GNAT implements this entirely through its protected-object / entry-call
machinery using the task's private `pthread_mutex_t` (field `L`) and
`pthread_cond_t` (field `CV`) stored inside the ATCB's `LL` field.

**degas handling**: `pthread_cond_wait` / `pthread_cond_signal` with the
per-ATCB CV.  Both the acceptor and the caller use the ATCB CV; GNAT
manages which side signals which via `Common.State` checks before issuing
the signal.  Correct ATCB identity (see §TLS below) is essential.

### 4. Protected Objects — `protected type` with barriers

```ada
protected type Event is
   entry Suspend when Signal'Count > 0;
   entry Signal  when Suspend'Count = 0;
end Event;
```

GNAT's implementation (`s-tassta.adb`, `s-tpobop.adb`):

1. **Entry call**: caller acquires the PO lock, evaluates the barrier, and
   either executes the body immediately or queues itself on the entry queue
   and calls `pthread_cond_wait(Self.CV, PO.L)`.
2. **PO_Service_Entries**: after any call that might change barrier state,
   GNAT re-evaluates all open barriers and wakes queued callers via
   `Wakeup_Entry_Caller(Caller)`.
3. **Wakeup_Entry_Caller**: checks `Caller.Common.State = Entry_Caller_Sleep`
   before issuing `pthread_cond_signal(Caller.CV)`.  If the state is wrong
   (e.g. `Runnable` due to TLS corruption), the signal is silently skipped
   → deadlock.

### 5. Timed Entry Calls — `select T.E or delay D`

GNAT calls `pthread_cond_timedwait(Self.CV, PO.L, deadline)`.  degas
intercepts this, sets `waiter = 1` with the absolute deadline, and yields.
The scheduler fires at the deadline if no signal has arrived.

Return value is critical:
- `0` (success) → signal arrived, entry was accepted.
- `ETIMEDOUT` (110 on Linux) → deadline expired, caller abandons entry call.

---

## The GNAT ATCB and Private_Data Layout

Each GNAT task has an ATCB (`Ada_Task_Control_Block`).  The platform-
specific part (`Private_Data`) on x86-64 Linux is (from `s-taspri.ads`):

```
offset  0  : Thread   (pthread_t,   8 bytes)
offset  8  : LWP      (OSI.Thread_Id, 8 bytes)
offset 16  : CV       (pthread_cond_t,  48 bytes)   ← task's condition variable
offset 64  : L        (pthread_mutex_t, 40 bytes)   ← task's mutex
```

Every `pthread_cond_wait` / `pthread_cond_signal` in GNAT uses these
per-task CV and L fields.  GNAT identifies the current task via
`STPO.Self` (see §TLS below).

---

## TLS and the Core degas Challenge

### The Problem

GNAT 13 (GCC 13) stores the current task pointer (`STPO.Self`) in a
`__thread` C variable declared in `s-tpopsp.adb`:

```c
__thread System_Task_Id  ATCB_Key = NULL;   /* approximate C equivalent */
```

Accessed via:
- `system__task_primitives__operations__specific__selfXnn` — reads TLS
- `system__task_primitives__operations__specific__setXnn` — writes TLS

Because all degas simulated contexts run on **one OS thread**, they share
one TLS.  When task B's `enter_task` writes B's ATCB into TLS, every
subsequent `STPO.Self` call from any context returns B's ATCB — including
main (ctx=0), which now incorrectly believes it is task B.

**Consequence**: GNAT operations that test or modify `Self.Common.State`
operate on the wrong ATCB.  `Wakeup_Entry_Caller` reads the wrong
`State`, skips the `pthread_cond_signal`, and the real caller sleeps
forever.

### Why LD_PRELOAD Cannot Intercept GNAT Symbols

An attempted fix — exporting `system__task_primitives__operations__self`
from `libdegas.so` — fails because:

- `libgnarl.so` both **defines** and **calls** this symbol internally.
- The ELF dynamic linker resolves `R_X86_64_JUMP_SLOT` for a
  self-referential symbol to the defining library's own address.
- The `@@Base` default-version marker in libgnarl's VERSYM table means
  the dynamic linker prefers the versioned definition in libgnarl over
  the unversioned one in our LD_PRELOAD.
- `enter_task` also writes TLS inline (via `__tls_get_addr`) rather than
  calling `setXnn` — so even a working `setXnn` intercept would miss the
  initial write.

`LD_DEBUG=bindings` confirms: glibc symbols bind to `libdegas.so` ✓;
GNAT-internal symbols bind to `libgnarl.so` (bypassing LD_PRELOAD).

### The Fix: Save/Restore TLS in cntxtYield

Since the TLS slot is shared, degas explicitly saves and restores it on
every context switch:

```c
/* In cntxtYield(), around swapcontext: */

// Before switching away from lastCntxt:
per_context_atcb[lastCntxt] = real_selfXnn();

swapcontext(&cntxtList[lastCntxt].context,
            &cntxtList[currentCntxt].context);

// After returning to lastCntxt:
if (per_context_atcb[lastCntxt])
    real_setXnn(per_context_atcb[lastCntxt]);
```

`real_selfXnn` and `real_setXnn` are obtained once via
`dlsym(RTLD_DEFAULT, "…selfXnn" / "…setXnn")`.  These symbols **are**
exported from libgnarl and **are** reachable via `dlsym`; we just cannot
intercept them via PLT.

**Bootstrapping**: on the very first switch into a new context,
`enter_task` has not yet run, so `selfXnn()` returns NULL.  The null
guard `if (per_context_atcb[lastCntxt])` prevents restoring a NULL ATCB.

### Why r.adb Worked Without the Fix

`r.adb` uses only `delay` — no entry calls, no `Wakeup_Entry_Caller`,
no `Common.State` checks.  TLS corruption corrupts `STPO.Self` but
nothing in `r.adb`'s runtime path queries it in a way that affects
scheduling.  The degas timer (`cntxtList[i].wait`) is independent of
ATCB identity.

---

## Cooperative Scheduling Rules

1. **Ready** (`waiter == 0`): eligible for round-robin selection.
2. **Timer-waiting** (`waiter == 1`): blocked on a `delay` or timed entry call; has a target `wait` timespec.
3. **Sync-waiting** (`waiter == 2`): blocked on `pthread_cond_wait` (indefinite); not eligible until signalled.

`cntxtYield` logic:
1. `findMinWaitingCntxt()` — finds the timer-waiting context with the smallest `wait`.
2. If all contexts are sleeping (`cntxtsAllSleeping`), advance `monotonic_time` to that minimum and mark it ready.
3. Round-robin among all `waiter == 0` contexts.
4. `swapcontext` with TLS save/restore.

`cntxtsAllSleeping` returns true only when `numWaitingCntxts == numActiveCntxts` AND no context has `waiter == 0` — preventing spurious time jumps while ready tasks exist.

---

## Test Suite

| Test | Ada features | Expected output | Notes |
|------|-------------|-----------------|-------|
| `r.adb` | Two tasks, `delay` only | `0 1 2 … 19` (interleaved) | Passes without TLS fix |
| `t1.adb` | Rendezvous, no delay | 5× `Main calling / Worker got / Main returned` | Passes without TLS fix |
| `t2.adb` | Rendezvous + `delay 0.5` | same 5× pattern, sim 2.51s | **Fails without TLS fix** — canonical regression test |
| `s1.adb` | Protected entry, 1 waiter | `0 1 2 … 9` | Fails without TLS fix |
| `s.adb`  | Protected entry, 2 waiters | `0 0 1 1 … 9 9` | Fails without TLS fix |

Build: `./build.sh`
Run:   `env LD_PRELOAD=./Linux/libdegas.so [SIM_CONTEXT_DEBUG=1] ./test`

---

## Historical Note: Original degas (GNAT ~2008)

The original `degas.c.original` worked because GNAT circa 2008 stored
`STPO.Self` via `pthread_setspecific / pthread_getspecific`.  The original
degas intercepted those calls with a per-context array:

```c
addresses[key].KA[currentCntxt]  /* per-context value store */
```

Each context therefore had its own `getspecific` return value.  Modern
GNAT replaced `setspecific/getspecific` with a `__thread` variable, which
degas cannot intercept — requiring the save/restore approach described
above.
