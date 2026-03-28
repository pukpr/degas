with Text_IO;
with Signals;
procedure S1 is

E : Signals.Event;

task A;
task body A is
   C : Integer := 0;
begin
   loop
      E.Suspend;
      Text_IO.Put_Line(C'Img);
      C := C + 1;
   end loop;
end;

begin
   for I in 1..10 loop
      delay 1.0;
      E.Signal;
   end loop;
   delay 5.0;
   abort A;
end;
