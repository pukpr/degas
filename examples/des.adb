with Ada.Numerics.Float_Random;
with Ada.Numerics.Elementary_Functions;
with Ada.Strings.Unbounded;
with Ada.Strings.Fixed;
with Pace.Strings;
with Pace.Hash_Table;
with Pace.Event_Io;
with Pace.Log;
with Pace.Queue;
with Text_Io;

procedure Des is
   package Asu renames Ada.Strings.Unbounded;
   use Asu;
   package Rand renames Ada.Numerics.Float_Random;
   package Math renames Ada.Numerics.Elementary_Functions;

   Reset : constant Boolean := 1 = Pace.Getenv ("RESET", 1);
   Ideal : constant Boolean := 1 = Pace.Getenv ("IDEAL", 0);
   Min_Response_Time : constant Float := Pace.Getenv ("MIN", 0.001);

   type Hash_Size is range 1 .. 5003;
   package Eio is new Pace.Event_Io (Hash_Size);

   type S is access all String;

   package Queue is new Pace.Queue (S);

   -- setting up the MACRO hash table
   package Macro_Hash is new Pace.Hash_Table.Simple_Htable
                               (Element => Asu.Unbounded_String,
                                No_Element => Asu.To_Unbounded_String (""),
                                Key => Asu.Unbounded_String,
                                Hash => Pace.Hash_Table.Hash,
                                Equal => Asu."=");

   task type Rt (Name : access String) is
      entry Item (Line : in S);
   end Rt;

   type Runner is access Rt;

   task body Rt is
      Q : Queue.Channel_Link;
      Num : Integer := 0;
      G : Rand.Generator;
      Finished_Reading : Boolean := False;
      Avg_Wait : Float;
   begin
      if Reset then
         Rand.Reset (G);
      end if;
      Pace.Log.Agent_Id (Name.all);
      loop
         accept Item (Line : in S) do
            if Pace.Strings.Select_Field (Line.all, 1) = "!" then
               -- process the macro and add it to the queue
               declare
                  Macro_Commands : constant String :=
                    Asu.To_String (Macro_Hash.Get (Asu.To_Unbounded_String
                                                     (Pace.Strings.Select_Field
                                                        (Line.all, 2))));
               begin
                  -- loop through string and add to Q separating by newlines..
                  for I in 1 .. Pace.Strings.Count_Fields
                                  (Macro_Commands, Ascii.Lf) loop
                     Queue.Append (Q, new String'(Pace.Strings.Select_Field
                                                    (Macro_Commands,
                                                     I, Ascii.Lf)));
                  end loop;
               end;
            else
               if Line.all = "" then
                  Finished_Reading := True;
               else
                  Queue.Append (Q, Line);
               end if;
            end if;
         end Item;
         exit when Finished_Reading;
      end loop;
      while not Queue.Is_Empty (Q) loop
         Num := Num + 1;
         declare
            Line : constant String := Queue.Front (Q).all;
            Key : constant String := Pace.Strings.Select_Field (Line, 1);
            Name : constant String := Pace.Strings.Select_Field (Line, 2);
            Ok : Boolean;
            Min : Float;
         begin
            case Key (Key'First) is
               when '#' =>
                  Text_Io.Put_Line (Duration'Image (Pace.Now) & " " & Line);
               when '+' =>
                  Pace.Log.Wait_Until (Duration'Value (Key));
               when '0' .. '9' =>
                  Pace.Log.Wait (Duration'Value (Key));
               when '~' =>
                  if Pace.Strings.Count_Fields (Line, ' ') = 3 then
                     Min := Float'Value (Pace.Strings.Select_Field (Line, 3));
                  else
                     Min := Min_Response_Time;
                  end if;
                  declare
                     Tau : constant Float :=
                       Float'Value (Key (Key'First + 1 .. Key'Last)) - Min;
                  begin
                     Avg_Wait := Min - Tau * Math.Log (Rand.Random (G));
                     if Ideal then
                        Pace.Log.Wait (Duration'Value (Key));
                     else
                        Pace.Log.Wait (Duration (Avg_Wait));
                     end if;
                  end;
               when others =>
                  if Key = "<>" or Key = "flush" then
                     Eio.Flush (Name);
                  elsif Key = "<<" or Key = "sync" then
                     Eio.Send (Name, Ack => True);
                  elsif Key = "<-" or Key = "async" then
                     Eio.Send (Name, Ack => False);
                  elsif Key = "->" or Key = "drain" then
                     Eio.Await (Name, Ok, Wait => False);
                  elsif Key = ">>" or Key = "wait" then
                     Eio.Await (Name, Ok, Wait => True);
                  else
                     Callback (Key, Name);
                  end if;
            end case;
            Queue.Pop (Q);
         end;
      end loop;
      Pace.Log.Put_Line ("Finished " & Name.all & Integer'Image (Num) &
                         " items @" & Duration'Image (Pace.Now) & " secs.");
   exception
      when E: others =>
         Pace.Log.Ex (E);
   end Rt;

   -- adds a macro to the hash table
   procedure Create_Macro (Macro_Name : String) is
      Macro_Commands : Asu.Unbounded_String := Asu.Null_Unbounded_String;
   begin
      loop
         declare
            Str : constant String := Text_IO.Get_Line;
         begin
            exit when Str = "";
            if Macro_Commands /= Asu.Null_Unbounded_String then
               Asu.Append (Macro_Commands, Ascii.Lf);
            end if;
            Asu.Append (Macro_Commands, Str);
         end;
      end loop;
      Macro_Hash.Set (Asu.To_Unbounded_String (Macro_Name), Macro_Commands);
   end Create_Macro;

   R : Runner;
begin
   loop
      declare
         Str : constant String := Text_IO.Get_Line;
      begin
         if Pace.Strings.Select_Field (Str, 1, ' ') = "MACRO" then
            Create_Macro (Pace.Strings.Select_Field (Str, 2, ' '));
         elsif Pace.Strings.Select_Field (Str, 1, ' ') = "AGENT" then
            R := new Rt (new String'(Str));
         else
            R.Item (new String'(Str));
         end if;
      end;
   end loop;

exception
   when Text_Io.End_Error =>
      Pace.Log.Put_Line ("Finished reading inputs.");
      select
         R.Item (new String'(""));
      else
         null;
      end select;
      Pace.Log.Agent_Id;
      Pace.Log.Wait (Duration'Last);
   when E: others =>
      Pace.Log.Ex (E);
------------------------------------------------------------------------------
-- $Id: des.adb,v 1.6 2006/04/14 23:14:10 pukitepa Exp $
------------------------------------------------------------------------------
end Des;

