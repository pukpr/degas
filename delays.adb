with Text_IO;
procedure Delays is

   task R1 is
   end;

   task body R1 is
   begin
       for I in 1 .. 10 loop
          Text_IO.Put_Line("R1" & I'Img);
          delay 1.0;
       end loop;
       Text_IO.Put_Line("R1 end");
   end;
   
   task R2 is
   end;

   task body R2 is
   begin
       for I in 1 .. 5 loop
          Text_IO.Put_Line("R2" & I'Img);
          delay 2.0;
       end loop;
       Text_IO.Put_Line("R2 end");
   end;
   
begin


   for I in 1 .. 5 loop
      Text_IO.Put_Line("M" & I'Img);
      delay 2.0;
   end loop;

   Text_IO.Put_Line("M end");
   
end;
       
            
