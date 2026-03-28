package Signals is
   ----------------------------------------
   -- SIGNALS -- Event triggering utilities
   ----------------------------------------
   -- EVENT is a "wait/signal" binary semaphore.
   --
   -- SHARED_DATA is a shared data trigger.
   --  Memory objects are directly coupled through the object parameter.
   --  The WRITE entry is invoked when READ is signalled.
   pragma Elaborate_Body;

   protected type Event is
      entry Suspend;
      entry Signal;
      function Waiting return Boolean;
   end Event;


   generic

      type Enum is (<>);
      Default : in Enum := Enum'First;

   package Multiple is
      ----------------------------------------
      -- MULTIPLE -- Mult-Event trigger
      ----------------------------------------
      -- AWAIT starts a "wait/signal" binary semaphore.

      procedure Signal (Value : in Enum);
      --
      -- CLIENT: Registers that an Enum signal has arrived

      procedure Await_All;
      --
      -- SERVER: Unsuspends when all Enum signals arrive

      procedure Await (Value : in Enum);
      --
      -- SERVER: Unsuspends when specific Enum signal arrives

      procedure Await_Any (Value : out Enum);
      --
      -- SERVER: Unsuspends when any Enum signal arrives

      function Current_Value return Enum;

      procedure Reset;

   end Multiple;

   ------------------------------------------------------------------------------
   -- $id: pace-signals.ads,v 1.1 09/16/2002 18:18:50 pukitepa Exp $
   ------------------------------------------------------------------------------
end Signals;
