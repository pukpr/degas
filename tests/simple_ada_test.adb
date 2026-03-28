-- Simple Ada test program for DEGAS
with Ada.Text_IO; use Ada.Text_IO;

procedure Simple_Ada_Test is
   
   task Worker is
      entry Start;
      entry Done;
   end Worker;
   
   task body Worker is
   begin
      accept Start;
      Put_Line ("Worker: Starting");
      Put_Line ("Worker: Finished");
      delay 1.0;
      accept Done do
         Put_Line ("Worker: In Done accept 1");
      end Done;
      Put_Line ("Worker: After Done accept 1");
      delay 1.0;
      accept Done do
         Put_Line ("Worker: In Done accept 2");
      end Done;
      Put_Line ("Worker: After Done accept 2" );
      delay 1.0;
      accept Done do
         Put_Line ("Worker: In Done accept 3");
      end Done;
      Put_Line ("Worker: After Done accept 3");
      accept Done do
         Put_Line ("Worker: In Done accept 4");
      end Done;
      Put_Line ("Worker: After Done accept 4");
      accept Done do
         Put_Line ("Worker: In Done accept 5");
      end Done;
      Put_Line ("Worker: After Done accept 5");
      accept Done do
         Put_Line ("Worker: In Done accept 6");
      end Done;
      Put_Line ("Worker: After Done accept 6");
   end Worker;
   
begin
   Put_Line ("Main: Starting simple Ada test with DEGAS");
   Worker.Start;
   Worker.Done;
   Worker.Done;
   Worker.Done;
   Worker.Done;
   Worker.Done;
   Worker.Done;
   Put_Line ("Main: Test completed");
end Simple_Ada_Test;
