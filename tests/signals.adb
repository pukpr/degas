with Ada.Tags;

package body Signals is

   protected body Event is
      entry Suspend when Signal'Count > 0 is
      begin
         null;
      end Suspend;

      entry Signal when Suspend'Count = 0 is
      begin
         null;
      end Signal;

      function Waiting return Boolean is
      begin
         return Suspend'Count > 0;
      end Waiting;

   end Event;

   
   package body Multiple is

      type Binary_Semaphores is array (Enum) of Boolean;

      protected Event is
         entry Await_All;
         entry Await (Enum);
         entry Await_Any (Value : out Enum);
         procedure Signal (Value : in Enum);
         procedure Reset;
         function Current_Value return Enum;
      private
         Signals : Binary_Semaphores := (others => False);
         Ready : Boolean := False;
         Current : Enum := Default;
      end Event;

      protected body Event is

         entry Await_All when Signals = (Enum'First .. Enum'Last => True) is
         begin
            null;
         end Await_All;

         entry Await (for I in Enum) when Current = I and Ready is
         begin
            null;
         end Await;


         entry Await_Any (Value : out Enum) when Ready is
         begin
            Value := Current;
         end Await_Any;

         procedure Signal (Value : in Enum) is
         begin
            Signals (Value) := True;
            Current := Value;
            Ready := True;
         end Signal;

         procedure Reset is
         begin
            Signals := (others => False);
            Ready := False;
            Current := Default;
         end Reset;

         function Current_Value return Enum is
         begin
            return Current;
         end Current_Value;

      end Event;

      procedure Signal (Value : in Enum) is
      begin
         Event.Signal (Value);
      end Signal;

      procedure Await_All is
      begin
         Event.Await_All;
      end Await_All;

      procedure Reset is
      begin
         Event.Reset;
      end Reset;

      procedure Await (Value : in Enum) is
      begin
         Event.Await (Value);
      end Await;

      procedure Await_Any (Value : out Enum) is
      begin
         Event.Await_Any (Value);
      end Await_Any;

      function Current_Value return Enum is
      begin
         return Event.Current_Value;
      end Current_Value;

   end Multiple;

 
end Signals;
