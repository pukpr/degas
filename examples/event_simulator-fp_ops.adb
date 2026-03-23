package body Event_Simulator.Fp_Ops is

   function "*" (L, R : in Signal) return Float is
   begin
      return (+L) * (+R);
   end "*";

   function "+" (L, R : in Signal) return Float is
   begin
      return (+L) + (+R);
   end "+";

   function "-" (S : in Signal) return Float is
   begin
      return - (+S);
   end "-";

   function "-" (L, R : in Signal) return Float is
   begin
      return (+L) - (+R);
   end "-";

   --
   function "*" (L : in Item; R : in Signal) return Float is
   begin
      return L * (+R);
   end "*";

   function "+" (L : in Item; R : in Signal) return Float is
   begin
      return L + (+R);
   end "+";

   function "-" (L : in Item; R : in Signal) return Float is
   begin
      return L - (+R);
   end "-";
   --
   function "*" (L : in Signal; R : in Item) return Float is
   begin
      return (+L) * R;
   end "*";

   function "+" (L : in Signal; R : in Item) return Float is
   begin
      return (+L) + R;
   end "+";

   function "-" (L : in Signal; R : in Item) return Float is
   begin
      return (+L) - R;
   end "-";

end;
