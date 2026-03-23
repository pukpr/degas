with Ada.Exceptions;
with Ada.Real_Time;
with Ada.Task_Attributes;
with GNAT.Source_Info;
with Text_IO;

package body Event_Simulator is

   Debug_On : constant Boolean := False;

   package TA is new Ada.Task_Attributes (Multiplex_Access, null);
   type String_Access is access String;

   protected type Multiplex_Signal is
      entry W;  -- on rise
      entry T;
      procedure N (Name : in String);
      function S return String;
   private
      Flag  : Boolean       := False;
      SName : String_Access := new String'("unnamed");
   end Multiplex_Signal;

   protected body Multiplex_Signal is
      entry W when Flag is
      begin
         Flag := False;
      end W;

      entry T when True is -- W'Count > 0 is
      begin
         Flag := True;
      end T;

      procedure N (Name : String) is
      begin
         SName := new String'(Name);
      end N;

      function S return String is
      begin
         return SName.all;
      end S;
   end Multiplex_Signal;

   protected Task_Signal_Counter is
      procedure Get (C : out Integer);
   private
      Count : Integer := 1;
   end Task_Signal_Counter;
   protected body Task_Signal_Counter is
      procedure Get (C : out Integer) is
      begin
         C     := Count;
         Count := Count + 1;
      end Get;
   end Task_Signal_Counter;

   protected body Task_Signal is

      procedure Debug (Mode : in String; I : in Integer) is
      begin
         if Debug_On then
            if Number = 0 then
               Task_Signal_Counter.Get (Number);
            end if;
            Text_IO.Put_Line
              (Mode & Number'Img & " " & Local_CB.S & I'Img & " #" & Call_Loop'Img &
               " Q" & Cb_Loop'Img & " := " &
               Image (State));
         end if;
      end Debug;

      entry Trig (Value : in Item) when True is
      begin
         State     := Value;
         Triggered := True;
         Call_Loop := 0;
         while not MA.Is_Empty (CB_List) loop
            Local_CB := MA.First_Element (CB_List);
            pragma Warnings (Off);
            Call_Loop := Call_Loop + 1;
            Local_CB.T;
            pragma Warnings (On);
            Debug ("Called", 0);
            MA.Delete_First (CB_List);
         end loop;
      end Trig;

      entry CB (Callback : in Multiplex_Access; Index : in Integer) when True is
      begin
         Local_CB := Callback;
         if MA.Is_Empty (CB_List) then
            Cb_Loop := 1;
            MA.Append (CB_List, Callback);
            Debug ("Appended", Index);
         elsif MA.Contains (CB_List, Callback) then
            -- Cb_Loop := Cb_Loop + 1;
            C := MA.Find (CB_List, Callback);
            MA.Replace_Element (CB_List, C, Callback);
            Debug ("Replaced", Index);
         else
            Cb_Loop := Cb_Loop + 1;
            MA.Append (CB_List, Callback);
            Debug ("Appended", Index);
         end if;
         if Cb_Loop >= Call_Loop then
            Triggered := False;
         end if;
      end CB;

      function Self_State return Item is
      begin
         return State;
      end Self_State;

      function Self_Triggered return Boolean is
      begin
         return Triggered;
      end Self_Triggered;

      procedure Self_Set (Value : in Item) is
      begin
         State := Value;
      end Self_Set;

   end Task_Signal;

   procedure Process (S : in Signal) is
      CB : Multiplex_Access;
   begin
      CB := TA.Value;
      S.Behavior.CB (CB, 0);
      CB.W;
   end Process;

   procedure Process (List : in Signals) is
      CB      : Multiplex_Access;
      Counter : Integer := 0;
   begin
      CB := TA.Value;
      for I in  List'Range loop
         Counter := Counter + 1;
         List (I).Behavior.CB (CB, Counter);
      end loop;
      CB.W;
   end Process;

   function Triggered (S : in Signal) return Boolean is
   begin
      return S.Behavior.Self_Triggered;
   end Triggered;

   ----------------------------------
   -- Surrogate Area
   ----------------------------------
   type Signal_Access is access Signal;

   type Count_Range is mod 100;
   type Used_Type is array (Count_Range) of Boolean;

   task type Surrogate is
      entry Trig
        (S : in TS_Access;
         D : in Duration;
         V : in Item;
         C : in Count_Range);
   end Surrogate;

   type Surrogate_Access is access Surrogate;
   Surr : array (Count_Range) of Surrogate_Access;

   protected Count is
      procedure Get (C : out Count_Range);
      procedure Reset (C : in Count_Range);
   private
      Counter : Count_Range := Count_Range'First;
      Used    : Used_Type   := (others => False);
   end Count;

   protected body Count is
      procedure Get (C : out Count_Range) is
      begin
         loop
            if Used (Counter) then
               null;
            else
               C              := Counter;
               Used (Counter) := True;
               Counter        := Counter + 1;
               exit;
            end if;
            Counter := Counter + 1;
         end loop;
      end Get;

      procedure Reset (C : in Count_Range) is
      begin
         Used (C) := False;
         Counter  := C;
      end Reset;
   end Count;

   task body Surrogate is
      SA  : TS_Access;
      TD  : Duration;
      Val : Item;
      Cnt : Count_Range;
   begin
      loop
         accept Trig (
           S  : in TS_Access;
            D : in Duration;
            V : in Item;
            C : in Count_Range) do
            SA  := S;
            TD  := D;
            Val := V;
            Cnt := C;
         end Trig;
         delay TD;
         SA.Trig (Val);
         Count.Reset (Cnt);
      end loop;
   end Surrogate;

   ----------------------------------
   -- End Surrogate Area
   ----------------------------------

   procedure Trigger
     (S     : in Signal;
      Value : in Item;
      After : in Duration := 0.0)
   is
      Index : Count_Range;
   begin
      if After = 0.0 then
         S.Behavior.Trig (Value);
      else
         Count.Get (Index);
         Surr (Index).Trig (S.Behavior, After, Value, Index);
      end if;
   end Trigger;

   procedure Trigger
     (S     : in Signal;
      Value : in Signal;
      After : in Duration := 0.0)
   is
   begin
      Trigger (S, +Value, After);
   end Trigger;

   function Value (S : in Signal) return Item is
   begin
      return S.Behavior.Self_State;
   end Value;

   procedure Set (S : in Signal; Value : in Item) is
   begin
      S.Behavior.Self_Set (Value);
   end Set;

   function Init (Value : Item) return Signal is
      S : Signal;
   begin
      Set (S, Value);
      return S;
   end Init;

   package body Behavior is
      task Waiter is
         pragma Storage_Size (200_000);
      end Waiter;

      task body Waiter is
         MS : Multiplex_Access;
      begin
         MS := new Multiplex_Signal;
         MS.N (GNAT.Source_Info.Enclosing_Entity);
         if Debug_On then
            Text_IO.Put_Line ("Started " & MS.S);
         end if;
         TA.Set_Value (MS);
         loop
            Task_Process;
         end loop;
      end Waiter;
   end Behavior;

   T      : Ada.Real_Time.Time;
   Last_T : Duration := Duration'Last;

   procedure Initialize_Clock (Run_Until_Time : Duration := Duration'Last) is
   begin
      T      := Ada.Real_Time.Clock;
      Last_T := Run_Until_Time;
   end Initialize_Clock;

   function Now return Duration is
      use type Ada.Real_Time.Time;
      D            : Ada.Real_Time.Time_Span := Ada.Real_Time.Clock - T;
      Elapsed_Time : Duration                :=
         (Ada.Real_Time.To_Duration (D));
      procedure Xit;
      pragma Import (C, Xit, "exit");
   begin
      -- Check to make sure clock is initialized
      if Elapsed_Time > Last_T then
         Text_IO.Put_Line ("Exiting");
         Xit;
      end if;
      return Elapsed_Time;
   end Now;

begin
   for I in  Count_Range loop
      Surr (I) := new Surrogate;
   end loop;
end Event_Simulator;
