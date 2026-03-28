with Ada.Text_IO;
use Ada.Text_IO;

procedure Traffic_Light is

   type Color is (Green, Yellow, Red);
   Loops : constant := 3;

   task NorthSouth is
      entry Start;
      entry Switch;
   end NorthSouth;

   task EastWest is 
      entry Start;
      entry Switch;
   end EastWest;

   procedure Display_Traffic_State(NS_Light : Color; EW_Light : Color) is
   begin
      Put_Line("=" & (1..50 => '='));
      Put_Line("TRAFFIC LIGHT STATUS");
      Put_Line("=" & (1..50 => '='));
      Put_Line("");
      
      -- North-South Traffic Light
      Put_Line("    NORTH-SOUTH");
      Put_Line("   +----------+");
      if NS_Light = Red then
         Put_Line("   |   RED    | <-- ACTIVE");
      else
         Put_Line("   |   RED    |");
      end if;
      
      if NS_Light = Yellow then
         Put_Line("   |  YELLOW  | <-- ACTIVE");
      else
         Put_Line("   |  YELLOW  |");
      end if;
      
      if NS_Light = Green then
         Put_Line("   |  GREEN   | <-- ACTIVE");
      else
         Put_Line("   |  GREEN   |");
      end if;
      Put_Line("   +----------+");
      Put_Line("");
      
      -- East-West Traffic Light
      Put_Line("    EAST-WEST");
      Put_Line("   +----------+");
      if EW_Light = Red then
         Put_Line("   |   RED    | <-- ACTIVE");
      else
         Put_Line("   |   RED    |");
      end if;
      
      if EW_Light = Yellow then
         Put_Line("   |  YELLOW  | <-- ACTIVE");
      else
         Put_Line("   |  YELLOW  |");
      end if;
      
      if EW_Light = Green then
         Put_Line("   |  GREEN   | <-- ACTIVE");
      else
         Put_Line("   |  GREEN   |");
      end if;
      Put_Line("   +----------+");
      Put_Line("");
   end Display_Traffic_State;

   task body NorthSouth is
      My_Light : Color := Green;
   begin
      accept Start;
      for I in 1..Loops loop
         My_Light := Green;
         Display_Traffic_State(My_Light, Red);
         Put_Line("North-South: GREEN - Go!");
         delay 20.0;
         
         My_Light := Yellow;
         Display_Traffic_State(My_Light, Red);
         Put_Line("North-South: YELLOW - Caution!");
         delay 3.0;
         
         My_Light := Red;
         Display_Traffic_State(My_Light, Red);
         Put_Line("North-South: RED - Stop!");
         Put_Line("Switching to East-West...");
         EastWest.Switch;
         accept Switch;
         delay 2.0;
      end loop;
   end NorthSouth;

   task body EastWest is
      My_Light : Color := Red;
   begin 
      accept Start;
      delay 1.0;
      for I in 1..Loops-1 loop
         accept Switch;
         delay 2.0;
         
         My_Light := Green;
         Display_Traffic_State(Red, My_Light);
         Put_Line("East-West: GREEN - Go!");
         delay 10.0;
         
         My_Light := Yellow;
         Display_Traffic_State(Red, My_Light);
         Put_Line("East-West: YELLOW - Caution!");
         delay 3.0;
         
         My_Light := Red;
         Display_Traffic_State(Red, My_Light);
         Put_Line("East-West: RED - Stop!");
         Put_Line("Switching to North-South...");
         NorthSouth.Switch;
      end loop;
   end EastWest;

begin
   Put_Line("Starting Traffic Light Simulation...");
   Put_Line("");
   NorthSouth.Start;
   EastWest.Start;
   
   -- Keep main thread alive so tasks can continue
   delay 120.0;
end Traffic_Light;
