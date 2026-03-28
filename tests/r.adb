with Text_IO;
procedure R is

task A;
task body A is
   C : Integer := 0;
begin
   for I in 1..10 loop
      Text_IO.Put_Line(C'Img);
      Text_IO.Flush;
      delay 1.0;
      C := C + 2;
   end loop;
end;

task B;
task body B is
   C : Integer := 1;
begin
   delay 0.5;
   for I in 1..10 loop
      Text_IO.Put_Line(C'Img);
      Text_IO.Flush;
      delay 1.0;
      C := C + 2;
   end loop;
end;


begin
   delay 1000.0;
end;


