with Ada.Text_IO; use Ada.Text_IO;

procedure Traffic_Simple is

   task NorthSouth is
      entry Start;
   end NorthSouth;

   task EastWest is 
      entry Start;
   end EastWest;

   task body NorthSouth is
   begin
      Put_Line("NorthSouth: Created");
      accept Start do
         Put_Line("NorthSouth: Accepted Start");
      end Start;
      Put_Line("NorthSouth: Starting loop");
      FOR i IN 1..3 loop
         delay 5.0;
         Put_Line("NorthSouth: Iteration");
      end loop;
   end NorthSouth;

   task body EastWest is
   begin 
      Put_Line("EastWest: Created");
      accept Start do
         Put_Line("EastWest: Accepted Start");
      end Start;
      Put_Line("EastWest: Starting loop");
      FOR I IN 1..3 loop
         delay 5.0;
         Put_Line("EastWest: Iteration");
      end loop;
   end EastWest;

begin
   Put_Line("Main: Starting");
   NorthSouth.Start;
   Put_Line("Main: NorthSouth started");
   EastWest.Start;
   Put_Line("Main: EastWest started");
   Put_Line("Main: All tasks running");
end Traffic_Simple;
