-- sync_test.adb  -- Demonstrates eight Ada synchronization primitives:
--   1. Protected type with procedure + function        (Counter)
--   2. Protected entry with 'when' barrier guard       (Sem, Gate, Finish)
--   3. Basic task rendezvous (accept)                  (Worker.Init)
--   4. Selective accept with multiple alternatives     (Dispatcher)
--   5. Requeue from one task entry to another          (Dispatcher Submit->Execute)
--   6. Guarded alternative in selective accept         (Scorer when not Claimed)
--   7. Timed entry call  (select ... or delay)         (Worker Phase 3)
--   8. Conditional entry call (select ... else)        (Worker Phase 4)
--
-- Checksum = sum(id, id=1..4)           Phase 1 semaphore  =  10
--          + sum(id*10, id=1..4)        Phase 3 dispatcher = 100
--          + 1*1000                     Phase 4 scorer     =1000
--                                                    Total =1110
with Ada.Text_IO; use Ada.Text_IO;

procedure Sync_Test is

   Num_Workers : constant := 4;

   -----------------------------------------------------------------------
   -- 1. Protected counter: mutual exclusion via procedure + function
   -----------------------------------------------------------------------
   protected Counter is
      procedure Add (N : Integer);
      function  Value return Integer;
   private
      Total : Integer := 0;
   end Counter;

   protected body Counter is
      procedure Add (N : Integer) is begin Total := Total + N; end Add;
      function  Value return Integer is begin return Total; end Value;
   end Counter;

   -----------------------------------------------------------------------
   -- 2a. Counting semaphore: protected entry with 'when Count > 0' guard
   -----------------------------------------------------------------------
   protected Sem is
      entry    Acquire;
      procedure Release;
   private
      Count : Natural := 2;          -- allow 2 workers concurrently
   end Sem;

   protected body Sem is
      entry Acquire when Count > 0 is begin Count := Count - 1; end Acquire;
      procedure Release is           begin Count := Count + 1; end Release;
   end Sem;

   -----------------------------------------------------------------------
   -- 2b. Arrival barrier: all Num_Workers must arrive before any passes
   -----------------------------------------------------------------------
   protected Gate is
      procedure Arrive;
      entry     Pass;
   private
      Arrived : Natural := 0;
   end Gate;

   protected body Gate is
      procedure Arrive is begin Arrived := Arrived + 1; end Arrive;
      entry Pass when Arrived >= Num_Workers is begin null; end Pass;
   end Gate;

   -----------------------------------------------------------------------
   -- 2c. Finish gate: main blocks until all workers have signalled Done
   -----------------------------------------------------------------------
   protected Finish is
      procedure Done;
      entry     Wait_All;
   private
      N : Natural := 0;
   end Finish;

   protected body Finish is
      procedure Done is begin N := N + 1; end Done;
      entry Wait_All when N >= Num_Workers is begin null; end Wait_All;
   end Finish;

   -----------------------------------------------------------------------
   -- 4+5. Dispatcher: selective accept with requeue Submit -> Execute
   --   Workers make a timed entry call to Submit (Phase 3).
   --   Submit immediately requeues the caller onto Execute so the
   --   worker remains suspended until Execute actually runs.
   -----------------------------------------------------------------------
   task Dispatcher is
      entry Submit  (Id : Integer);
      entry Execute (Id : Integer);
      entry Stop;
   end Dispatcher;

   task body Dispatcher is
   begin
      loop
         select
            accept Submit (Id : Integer) do
               Put_Line ("Dispatcher: queued  worker" & Id'Img);
               requeue Execute;         -- 5. requeue: caller moves to Execute queue
            end Submit;
         or
            accept Execute (Id : Integer) do
               Counter.Add (Id * 10);
               Put_Line ("Dispatcher: execute worker" & Id'Img
                         & " +" & Integer'Image (Id * 10));
            end Execute;
         or
            accept Stop;
            exit;
         end select;
      end loop;
   end Dispatcher;

   -----------------------------------------------------------------------
   -- 6. Scorer: guarded alternative -- only the first claimer succeeds.
   --   Workers make a conditional entry call to Claim (Phase 4).
   --   Once Claimed is True the 'when not Claimed' guard closes,
   --   so later conditional calls hit the else branch immediately.
   -----------------------------------------------------------------------
   task Scorer is
      entry Claim    (Id : Integer);
      entry Shutdown;
   end Scorer;

   task body Scorer is
      Claimed : Boolean := False;
   begin
      loop
         select
            when not Claimed =>              -- 6. guarded alternative
               accept Claim (Id : Integer) do
                  Counter.Add (Id * 1000);
                  Put_Line ("Scorer:     claimed  worker" & Id'Img
                            & " +" & Integer'Image (Id * 1000));
               end Claim;
               Claimed := True;
         or
            accept Shutdown;
            exit;
         end select;
      end loop;
   end Scorer;

   -----------------------------------------------------------------------
   -- 3. Worker: basic rendezvous on Init, then four sync phases
   -----------------------------------------------------------------------
   task type Worker is
      entry Init (Id : Integer);    -- 3. basic rendezvous
   end Worker;

   task body Worker is
      My_Id : Integer;
   begin
      accept Init (Id : Integer) do -- 3.
         My_Id := Id;
      end Init;

      -- Phase 1: counting semaphore (at most 2 workers in critical section)
      delay Duration (My_Id) * 0.1;
      Sem.Acquire;
      Counter.Add (My_Id);
      Put_Line ("Worker" & My_Id'Img & ":  Phase 1 sem   +" & My_Id'Img);
      Sem.Release;

      -- Phase 2: arrival barrier -- all four must reach this point
      Gate.Arrive;
      Gate.Pass;
      Put_Line ("Worker" & My_Id'Img & ":  Phase 2 barrier passed");

      -- Phase 3: timed entry call to Dispatcher (7. timed entry call)
      delay Duration (My_Id) * 0.1;
      select
         Dispatcher.Submit (My_Id);          -- 7.
      or
         delay 5.0;
         Put_Line ("Worker" & My_Id'Img & ":  Phase 3 TIMEOUT (unexpected)");
      end select;

      -- Phase 4: conditional entry call to Scorer (8. conditional entry call)
      select
         Scorer.Claim (My_Id);               -- 8.
      else
         Put_Line ("Worker" & My_Id'Img & ":  Phase 4 no claim");
      end select;

      Finish.Done;
   end Worker;

   Workers : array (1 .. Num_Workers) of Worker;

begin
   Put_Line ("Main: starting sync test");
   for I in Workers'Range loop
      Workers (I).Init (I);
   end loop;

   Finish.Wait_All;     -- block until all workers have completed all phases
   Dispatcher.Stop;
   Scorer.Shutdown;

   Put_Line ("CHECKSUM:" & Counter.Value'Img);
end Sync_Test;
