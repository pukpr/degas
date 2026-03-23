package body Event_Simulator.Logical_Ops is

   function "and" (L, R : in Signal) return Boolean is
   begin
      return +L and +R;
   end "and";

   function "or" (L, R : in Signal) return Boolean is
   begin
      return +L or +R;
   end "or";

   function "not" (S : in Signal) return Boolean is
   begin
      return not (+S);
   end "not";

   function "xor" (L, R : in Signal) return Boolean is
   begin
      return +L xor +R;
   end "xor";

   --
   function "and" (L : in Item; R : in Signal) return Boolean is
   begin
      return L and +R;
   end "and";

   function "or" (L : in Item; R : in Signal) return Boolean is
   begin
      return L or +R;
   end "or";

   function "xor" (L : in Item; R : in Signal) return Boolean is
   begin
      return L xor +R;
   end "xor";
   --
   function "and" (L : in Signal; R : in Item) return Boolean is
   begin
      return +L and R;
   end "and";

   function "or" (L : in Signal; R : in Item) return Boolean is
   begin
      return +L or R;
   end "or";

   function "xor" (L : in Signal; R : in Item) return Boolean is
   begin
      return +L xor R;
   end "xor";

end Event_Simulator.Logical_Ops;
