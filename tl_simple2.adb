with Ada.Text_IO;
use Ada.Text_IO;

procedure TL_Simple is

   task NorthSouth;
   task EastWest;

   task body NorthSouth is
   begin
      Put_Line("NorthSouth running");
   end NorthSouth;

   task body EastWest is
   begin
      Put_Line("EastWest running");
   end EastWest;

begin
   Put_Line("Starting...");
   delay 1.0;
end;
