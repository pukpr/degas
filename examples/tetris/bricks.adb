

--::::::::::
--bricks.adb
--::::::::::
with Text_IO;
with Screen;
package body Bricks is 
   Finished_Flag : Boolean := False; 

   function Finished return Boolean is 
   begin
      return Finished_Flag; 
   end Finished; 

   task body Move is 
      X, NX            : Wall.Width; 
      Y, NY            : Wall.Height; 
      New_Brick, Brick : Wall.Brick_Type; 
      Exit_Flag        : Boolean := False; 

      procedure Rotate(Brick     : in     Wall.Brick_Type; 
                       New_Brick :    out Wall.Brick_Type) is 
         X, Y : Integer; 
         B    : Wall.Brick_Type; 
      begin
         for I in Brick'range loop
            X := Brick(I).Y + 1; 
            Y :=  -(Brick(I).X - 1); 
            B(I).X := X; 
            B(I).Y := Y; 
         end loop; 
         New_Brick := B; 
      end Rotate; 

   begin
      Outer : loop
         Text_IO.Put_Line("Move");
         accept Start; 
         Finished_Flag := False; 
         Middle : loop
            Exit_Flag := False; 
            accept Put(X     : in     Wall.Width; 
                       Y     : in     Wall.Height; 
                       Brick : in     Wall.Brick_Type; 
                       Done  :    out Boolean) do
               if Wall.Examine(Brick, X, Y) then 
                  Done := False; 
               else 
                  Done := True; 
                  Finished_Flag := True; 
                  Screen.MoveCursor((Column => 10, Row => 12));
                  Text_IO.Put_Line ("Try Again [Y/N] ?");
               end if; 
               Move.X := X; 
               Move.Y := Y; 
               Move.Brick := Brick; 
            end Put; 
            Wall.Put(Brick, X, Y); 
            Inner : loop
               select
                  accept Right do
                     if X < Wall.Width'Last then 
                        NX := X + 1; 
                        if Wall.Examine(Brick, NX, Y) then 
                           Wall.Erase(Brick, X, Y); 
                           X := NX; 
                           Wall.Put(Brick, X, Y); 
                        end if; 
                     end if; 
                  end Right; 
               or 
                  accept Left do
                     if Wall.Width'First < X then 
                        NX := X - 1; 
                        if Wall.Examine(Brick, NX, Y) then 
                           Wall.Erase(Brick, X, Y); 
                           X := NX; 
                           Wall.Put(Brick, X, Y); 
                        end if; 
                     end if; 
                  end Left; 
               or 
                  accept Rotation do
                     Rotate(Brick, New_Brick); 
                     if Wall.Examine(New_Brick, X, Y) then 
                        Wall.Erase(Brick, X, Y); 
                        Brick := New_Brick; 
                        Wall.Put(Brick, X, Y); 
                     end if; 
                  end Rotation; 
               or 
                  accept Drop(Ok : out Boolean) do
                     NY := Y + 1; 
                     if Wall.Examine(Brick, X, NY) then 
                        Wall.Erase(Brick, X, Y); 
                        Y := NY; 
                        Wall.Put(Brick, X, Y); 
                        Ok := True; 
                     else 
                        Wall.Place(Brick, X, Y); 
                        Ok := False; 
                        Exit_Flag := True; 
                     end if; 
                  end Drop; 
               or 
                  accept Stop; 
                  exit Middle; 
               end select; 
               if Exit_Flag then 
                  select
                     accept Drop(Ok : out Boolean) do
                        Ok := FALSE;
                     end Drop;
                  or
                     delay 1.0;
                  end select;
                  exit Inner; 
               end if; 
            end loop Inner; 
         end loop Middle; 
      end loop Outer; 
   exception 
      when others => Text_IO.Put_Line("Move error");
   end Move; 

end Bricks; 
