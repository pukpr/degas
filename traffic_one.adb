-- Test with just one task
with Ada.Text_IO; use Ada.Text_IO;

procedure Traffic_One is
   
   task Light1 is
      entry Start;
   end Light1;
   
   task body Light1 is
   begin
      Put_Line ("Light1: Created");
      accept Start do
         Put_Line ("Light1: Started");
      end Start;
      Put_Line ("Light1: Running");
      Put_Line ("Light1: Finished");
   end Light1;
   
begin
   Put_Line ("Main: Starting traffic_one test");
   Put_Line ("Main: Task created");
   Light1.Start;
   Put_Line ("Main: Test completed");
end Traffic_One;
