# Examples — Event_Simulator

This directory contains the generic `Event_Simulator` package and ready-made
instantiations that demonstrate how to build signal-driven discrete-event
simulations in Ada using the DEGAS intercept library.

---

## What `event_simulator.ads` Does

`event_simulator.ads` is the **specification** of a generic Ada package that
models **reactive, signal-driven hardware** (or any event-driven system).
Think of it as a software breadboard: you declare typed signals, wire them
together with `Behavior` packages, and drive the simulation by triggering
signals with new values, optionally after a propagation delay.

### Generic parameters

```ada
generic
   type Item is private;           -- the value type each signal carries
   Default : in Item;              -- initial / reset value for every signal
   with function Image (I : Item) return String;  -- for debug output
package Event_Simulator is
```

| Parameter | Purpose |
|-----------|---------|
| `Item`    | What a signal holds — `Boolean` for digital logic, `Float` for analogue, any discrete type for custom models |
| `Default` | Every freshly created signal starts at this value |
| `Image`   | Converts a value to `String`; used internally for debug traces |

The two pre-built instantiations in this directory show the pattern clearly:

```ada
-- ls.ads  – Logic Simulator
package Sim is new Event_Simulator (Boolean, False, Boolean'Image);

-- fs.ads  – Floating-point Simulator
package Sim is new Event_Simulator (Float, 0.0, Float'Image);
```

---

### The `Signal` type

```ada
type Signal is tagged private;
```

A `Signal` is a tagged record that wraps an internal **protected object**
(`Task_Signal`).  Because the record holds only an access to that protected
object, copying a `Signal` value is a shallow copy — both the original and
the copy refer to the same underlying reactive node.  This is intentional:
it lets you pass signals freely into arrays, closures, and behaviors without
losing connectivity.

#### Reading a signal

| Expression | Meaning |
|------------|---------|
| `Value (S)` or `+S` | Current value of type `Item` |
| `Triggered (S)` or `-S` | `True` if the signal fired since the last `Process` call |

The operator shorthands (`+` and `-`) make expressions in `Behavior`
procedures compact and readable.

#### Writing / firing a signal

```ada
procedure Trigger (S : Signal; Value : Item;  After : Duration := 0.0);
procedure Trigger (S : Signal; Value : Signal; After : Duration := 0.0);
procedure Set     (S : Signal; Value : Item);
```

* `Trigger` with `After = 0.0` fires **immediately**, updating the signal's
  value and waking every registered callback task.
* `Trigger` with a non-zero `After` schedules the update via one of 100
  internal **Surrogate** tasks, so the caller is never blocked.
* `Set` stores a value **without** waking any callbacks; it is used to
  initialise a signal before the simulation starts (`Init` uses it).

```ada
function Init (Value : Item) return Signal;
```

`Init` creates a new signal pre-loaded with `Value` and is the idiomatic way
to declare a signal with a known starting state.

---

### Arrays of signals

```ada
type Signals is array (Integer range <>) of Signal;
subtype Signal8 is Signals (0 .. 7);   -- 8-bit bus
subtype Signal4 is Signals (0 .. 3);   -- 4-bit bus
```

`Signal8` and `Signal4` are convenience subtypes that match common bus
widths.  `LS.Conversion` (see `ls-conversion.ads`) can convert between a
`Signals` array and a modular integer type, which is handy for building
binary adders, multiplexers, and similar components.

---

### The `Behavior` pattern

```ada
generic
   with procedure Task_Process;
package Behavior is
end Behavior;
```

`Behavior` is a **generic child package** with a one-line spec and a body
that spawns a private Ada task (`Waiter`) that calls `Task_Process` in an
infinite loop.

```ada
package body Behavior is
   task Waiter is
      pragma Storage_Size (200_000);  -- 200 KB stack; increase for deep call chains
   end Waiter;
   task body Waiter is
   begin
      ...
      loop
         Task_Process;   --  your logic here, executed repeatedly
      end loop;
   end Waiter;
end Behavior;
```

The contract for `Task_Process` is:

1. **Block** on one or more input signals by calling `Process (S)` or
   `Process (List)`.  The call returns only when at least one of the listed
   signals has been triggered.
2. **Compute** outputs from the current signal values (e.g. using the
   operator packages `Fp_Ops` or `Logical_Ops`).
3. **Drive** output signals with `Trigger`.

Because `Waiter` loops automatically, the behavior re-registers itself after
each firing, giving you the continuous reactivity of a hardware gate without
writing any looping or synchronisation code yourself.

---

### Process — blocking until a signal fires

```ada
procedure Process (S    : in Signal);            -- single signal
procedure Process (List : in Signals);           -- any signal in the list
```

`Process` suspends the calling task until the named signal (or any signal in
the array) is triggered.  Internally it registers the task's private
`Multiplex_Signal` handle with the `Task_Signal` protected object, then
blocks on that handle's `W` entry.  When `Trigger` fires it calls all
registered handles, releasing the waiting tasks.

---

### Simulation clock

```ada
procedure Initialize_Clock (Run_Until_Time : Duration := Duration'Last);
function  Now return Duration;
```

* `Initialize_Clock` must be called once at program start (or at the start
  of each simulation run).  Passing a finite `Run_Until_Time` causes the
  program to call the C `exit()` function automatically when `Now` is
  invoked after the limit has elapsed.
* `Now` returns elapsed real (or, under DEGAS, **simulated**) time as a
  `Duration`.

Under `LD_PRELOAD=../Linux/libdegas.so` all `delay` statements and
`Ada.Real_Time.Clock` calls are intercepted so the simulation runs
**instantaneously** in wall-clock time while still producing a correct,
deterministic event order.

---

### Child operator packages

| Package | Operators provided | Return type |
|---------|--------------------|-------------|
| `Event_Simulator.Fp_Ops`      | `+`, `-`, `*` between `Signal`/`Item` pairs | `Float`   |
| `Event_Simulator.Logical_Ops` | `and`, `or`, `not`, `xor` between `Signal`/`Item` pairs | `Boolean` |

Both packages are themselves generic and are instantiated through the wrapper
packages (`LS.Ops`, `FS.Ops`), so you can write arithmetic or logical
expressions directly over signals without explicitly calling `Value`.

---

## Files in this directory

| File | Description |
|------|-------------|
| `event_simulator.ads` | Generic package specification (described above) |
| `event_simulator.adb` | Package body: protected objects, Surrogate tasks, Behavior task |
| `event_simulator-fp_ops.ads/adb` | Arithmetic operator child package |
| `event_simulator-logical_ops.ads/adb` | Logical operator child package |
| `ls.ads` | Logic Simulator — `Event_Simulator (Boolean, False, Boolean'Image)` + `Logical_Ops` |
| `ls-conversion.ads/adb` | Generic conversion between a `Signals` array and a modular integer (`Bits_Range`) |
| `fs.ads` | Floating-point Simulator — `Event_Simulator (Float, 0.0, Float'Image)` + `Fp_Ops` |
| `des.ads/adb` | Full digital-logic demonstration (encoder / decoder circuit) |
| `main_test.adb` | Minimal standalone test (AND gate) — see below |

---

## `main_test.adb` — instantiated test procedure

`main_test.adb` demonstrates the complete `Event_Simulator` usage cycle with
the simplest possible combinational gate: a **two-input AND gate** built from
Boolean signals.

```
   A ──┐
       AND──► C
   B ──┘
```

### How it works

1. Three `Boolean` signals are declared using the `LS` (Logic Simulator)
   package: `A` and `B` as inputs, `C` as the output.
2. A procedure `And_Gate` is defined that:
   - blocks on `Process (Inputs)` until `A` **or** `B` changes, then
   - re-computes `C := A and B` via `Trigger`.
3. `And_Gate` is wrapped in a `LS.Sim.Behavior` instantiation, which spawns
   an Ada task that runs the gate continuously in a loop.
4. The main procedure triggers `A` and `B` in four test combinations,
   waits a short propagation interval, and prints the resulting value of `C`.

### Building

```bash
cd examples
gnatmake -D obj main_test.adb
```

### Running

```bash
# With DEGAS (instantaneous simulated time):
LD_PRELOAD=../Linux/libdegas.so ./main_test

# Without DEGAS (real-time delays of 0.1 s each step):
./main_test
```

### Expected output

```
=== AND Gate Simulation (using Event_Simulator / DEGAS) ===
Initial:  A=FALSE  B=FALSE  C=FALSE
A=True,  B=False => C=FALSE
A=True,  B=True  => C=TRUE
A=False, B=True  => C=FALSE
A=True (after 0.2s), B=True  => C=TRUE
=== Done ===
```
