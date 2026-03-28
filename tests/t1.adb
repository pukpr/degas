with Text_IO;
procedure T1 is

   task Worker is
      entry Ping (V : in Integer);
   end Worker;

   task body Worker is
      Val : Integer;
   begin
      for I in 1 .. 5 loop
         accept Ping (V : in Integer) do
            Val := V;
         end Ping;
         Text_IO.Put_Line ("Worker got:" & Val'Img);
      end loop;
   end Worker;

begin
   for I in 1 .. 5 loop
      Text_IO.Put_Line ("Main calling Ping(" & I'Img & ")");
      Worker.Ping (I);
      Text_IO.Put_Line ("Main returned from Ping");
   end loop;
end T1;
