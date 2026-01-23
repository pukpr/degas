-- Traffic light simulation with multiple intersections
with Ada.Text_IO; use Ada.Text_IO;

procedure Traffic_Light is
   
   task type Intersection (ID : Integer) is
      entry Start;
      entry Stop;
   end Intersection;
   
   task body Intersection is
      Light_State : String (1..5) := "GREEN";
   begin
      Put_Line ("Intersection" & Integer'Image(ID) & ": Created");
      accept Start do
         Put_Line ("Intersection" & Integer'Image(ID) & ": Started - " & Light_State);
      end Start;
      
      Light_State := "RED  ";
      Put_Line ("Intersection" & Integer'Image(ID) & ": Changing to " & Light_State);
      
      accept Stop do
         Put_Line ("Intersection" & Integer'Image(ID) & ": Stopping");
      end Stop;
      
      Put_Line ("Intersection" & Integer'Image(ID) & ": Finished");
   end Intersection;
   
   Int1 : Intersection(1);
   Int2 : Intersection(2);
   Int3 : Intersection(3);
   
begin
   Put_Line ("Main: Starting traffic_light test");
   Put_Line ("Main: All intersections created");
   
   Int1.Start;
   Int2.Start;
   Int3.Start;
   
   Put_Line ("Main: All intersections started");
   
   Int1.Stop;
   Int2.Stop;
   Int3.Stop;
   
   Put_Line ("Main: Test completed");
end Traffic_Light;
