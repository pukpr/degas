--  main_test.adb
--
--  Minimal demonstration of the Event_Simulator generic package.
--
--  Models a two-input AND gate:
--
--       A ──┐
--           AND──► C
--       B ──┘
--
--  Build:
--     gnatmake -D obj main_test.adb
--
--  Run (with DEGAS for simulated time — adjust path if needed):
--     LD_PRELOAD=../Linux/libdegas.so ./main_test
--
--  Run (real-time, small delays — no DEGAS required):
--     ./main_test
--
--  The libdegas.so library lives under the Linux/ sub-directory of the
--  repository root.  If you have built it elsewhere, set the path
--  accordingly (e.g. LD_PRELOAD=/path/to/libdegas.so ./main_test).

with Ada.Text_IO;  use Ada.Text_IO;
with LS;

procedure Main_Test is

   use LS.Sim;   --  Signal, Signals, Trigger, Process, Init, Behavior, …
   use LS.Ops;   --  "and", "or", "not", "xor" operators over Signal

   --  Import the C exit() function so the program terminates cleanly even
   --  though Behavior's internal Waiter task loops forever.
   procedure OS_Exit (Status : Integer);
   pragma Import (C, OS_Exit, "exit");

   --  ── Signals ─────────────────────────────────────────────────────────
   A : Signal := Init (False);   --  input A
   B : Signal := Init (False);   --  input B
   C : Signal := Init (False);   --  output C = A and B

   --  ── Gate behaviour ──────────────────────────────────────────────────
   --
   --  And_Gate is called in a loop by the Behavior task.
   --  Each iteration:
   --    1. Block until A or B is triggered  (Process)
   --    2. Recompute C = A and B            (Trigger)
   --
   procedure And_Gate is
      --  Collect both inputs into a Signals aggregate so that a change on
      --  *either* A or B wakes this gate.
      Inputs : constant Signals := (0 => A, 1 => B);
   begin
      Process (Inputs);            --  suspend until A or B fires
      Trigger (C, A and B);        --  propagate new output value
   end And_Gate;

   --  Instantiating Behavior spawns an Ada task that runs And_Gate in a
   --  loop, giving the gate continuous reactivity.
   package And_Gate_Behavior is new LS.Sim.Behavior (And_Gate);

begin

   --  Allow up to 5 simulated seconds.  When Now() is called after this
   --  duration has elapsed, Event_Simulator calls the C exit() function to
   --  terminate the whole process — including the infinite Waiter task
   --  spawned by Behavior — cleanly.
   Initialize_Clock (5.0);

   Put_Line ("=== AND Gate Simulation (using Event_Simulator / DEGAS) ===");
   Put_Line ("Initial:  A=" & Boolean'Image (+A) &
             "  B="          & Boolean'Image (+B) &
             "  C="          & Boolean'Image (+C));

   --  Test 1: A=True, B stays False  ──  C should remain False
   Trigger (A, True);
   delay 0.1;
   Put_Line ("A=True,  B=False => C=" & Boolean'Image (+C));

   --  Test 2: B=True as well  ──  C should become True
   Trigger (B, True);
   delay 0.1;
   Put_Line ("A=True,  B=True  => C=" & Boolean'Image (+C));

   --  Test 3: A back to False  ──  C should return to False
   Trigger (A, False);
   delay 0.1;
   Put_Line ("A=False, B=True  => C=" & Boolean'Image (+C));

   --  Test 4: A=True scheduled 0.2 s in the future  ──  C becomes True
   --  after the propagation delay, demonstrating deferred Trigger.
   Trigger (A, True, After => 0.2);
   delay 0.5;
   Put_Line ("A=True (after 0.2s), B=True  => C=" & Boolean'Image (+C));

   Put_Line ("=== Done ===");

   OS_Exit (0);

end Main_Test;
