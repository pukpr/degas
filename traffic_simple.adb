-- Simple traffic light simulation with two tasks
with Ada.Text_IO; use Ada.Text_IO;

procedure Traffic_Simple is
   
   task Light1 is
      entry Start;
   end Light1;
   
   task Light2 is
      entry Start;
   end Light2;
   
   task body Light1 is
   begin
      Put_Line ("Light1: Created");
      accept Start do
         Put_Line ("Light1: Started");
      end Start;
      Put_Line ("Light1: Running");
      Put_Line ("Light1: Finished");
   end Light1;
   
   task body Light2 is
   begin
      Put_Line ("Light2: Created");
      accept Start do
         Put_Line ("Light2: Started");
      end Start;
      Put_Line ("Light2: Running");
      Put_Line ("Light2: Finished");
   end Light2;
   
begin
   Put_Line ("Main: Starting traffic_simple test");
   Put_Line ("Main: Both tasks created");
   Light1.Start;
   Light2.Start;
   Put_Line ("Main: Test completed");
end Traffic_Simple;
