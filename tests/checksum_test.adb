-- Multi-task checksum test for DEGAS.
-- Four Worker tasks each compute id * (1+2+3+4+5) using staggered delays,
-- then rendezvous with a Collector task that sums the partial results.
-- Expected checksum: 1*15 + 2*15 + 3*15 + 4*15 = 150.
with Ada.Text_IO; use Ada.Text_IO;

procedure Checksum_Test is

   Num_Workers : constant := 4;
   Iterations  : constant := 5;

   -- Collector serialises results from all workers then exposes the total.
   task Collector is
      entry Submit (Id : in Integer; Value : in Integer);
      entry Result (Total : out Integer);
   end Collector;

   task body Collector is
      Sum : Integer := 0;
   begin
      for I in 1 .. Num_Workers loop
         accept Submit (Id : in Integer; Value : in Integer) do
            Put_Line ("Collector: received" & Value'Img & " from worker" & Id'Img);
            Sum := Sum + Value;
         end Submit;
      end loop;
      accept Result (Total : out Integer) do
         Total := Sum;
      end Result;
   end Collector;

   -- Each Worker accumulates id*step over Iterations steps with a delay
   -- proportional to its id, ensuring finish order 1 < 2 < 3 < 4.
   task type Worker is
      entry Init (Id : in Integer);
   end Worker;

   task body Worker is
      My_Id : Integer;
      Accum : Integer := 0;
   begin
      accept Init (Id : in Integer) do
         My_Id := Id;
      end Init;
      for Step in 1 .. Iterations loop
         delay Duration (My_Id) * 0.1;
         Accum := Accum + My_Id * Step;
      end loop;
      Collector.Submit (My_Id, Accum);
   end Worker;

   Workers  : array (1 .. Num_Workers) of Worker;
   Checksum : Integer;

begin
   Put_Line ("Main: starting checksum test");
   for I in Workers'Range loop
      Workers (I).Init (I);
   end loop;
   Collector.Result (Checksum);
   Put_Line ("CHECKSUM:" & Checksum'Img);
end Checksum_Test;
