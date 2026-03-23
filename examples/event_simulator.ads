with Ada.Containers.Doubly_Linked_Lists;

generic
   type Item is private;
   Default : in Item;
   with function Image (I : Item) return String;
package Event_Simulator is

   procedure Initialize_Clock (Run_Until_Time : Duration := Duration'Last);
   function Now return Duration;

   type Signal is tagged private;
   procedure Trigger
     (S     : in Signal;
      Value : in Item;
      After : in Duration := 0.0);
   procedure Trigger
     (S     : in Signal;
      Value : in Signal;
      After : in Duration := 0.0);
   procedure Process (S : in Signal);
   function Value (S : in Signal) return Item;
   function "+" (S : in Signal) return Item renames Value;
   function Triggered (S : in Signal) return Boolean;
   function "-" (S : in Signal) return Boolean renames Triggered;
   procedure Set (S : in Signal; Value : in Item);
   function Init (Value : Item) return Signal;

   type Signals is array (Integer range <>) of Signal;
   procedure Process (List : in Signals);

   generic
      with procedure Task_Process;
   package Behavior is
   end Behavior;

   subtype Signal8 is Signals (0 .. 7);
   subtype Signal4 is Signals (0 .. 3);

private

   type Multiplex_Signal;

   type Multiplex_Access is access all Multiplex_Signal;

   package MA is new Ada.Containers.Doubly_Linked_Lists (Multiplex_Access);

   protected type Task_Signal is
      entry Trig (Value : in Item);
      entry CB (Callback : in Multiplex_Access; Index : in Integer);
      function Self_State return Item;
      function Self_Triggered return Boolean;
      procedure Self_Set (Value : in Item);
   private
      Local_CB  : Multiplex_Access := null;
      CB_List   : MA.List;
      Call_Loop : Integer          := 0;
      Cb_Loop   : Integer          := 0;
      State     : Item             := Default;
      Triggered : Boolean          := False;
      C         : MA.Cursor;
      Number    : Integer          := 0;
   end Task_Signal;

   type TS_Access is access Task_Signal;

   type Signal is tagged record
      Behavior : TS_Access := new Task_Signal;
   end record;

end Event_Simulator;
