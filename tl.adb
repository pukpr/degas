with Ada.Text_IO;
use Ada.Text_IO;

procedure TL is

   type Color is (Green, Yellow, Red);

   task NorthSouth is
      entry Start;
   end NorthSouth;

   task EastWest is 
      entry Start;
   end EastWest;

   procedure Display_Traffic_State(NS_Light : Color; EW_Light : Color) is
   begin
      Put_Line("go " & NS_Light'Img & EW_Light'Img);
   end Display_Traffic_State;

   task body NorthSouth is
      My_Light : Color := Green;
   begin
      --accept Start;
      Display_Traffic_State(My_Light, Red);
   end NorthSouth;

   task body EastWest is
      My_Light : Color := Red;
   begin 
      accept Start;
      Display_Traffic_State(Red, My_Light);
   end EastWest;

begin
   Put_Line("Starting Traffic Light Simulation...");
   Put_Line("");
   --NorthSouth.Start;
   EastWest.Start;
   
   -- Keep main thread alive so tasks can continue
   delay 1.0;
   
end;
