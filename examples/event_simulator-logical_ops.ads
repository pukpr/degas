generic
   with function "and" (L, R : Item) return Boolean is <>;
   with function "or" (L, R : Item) return Boolean is <>;
   with function "xor" (L, R : Item) return Boolean is <>;
   with function "not" (L : Item) return Boolean is <>;
package Event_Simulator.Logical_Ops is
   function "and" (L, R : in Signal) return Boolean;
   function "or" (L, R : in Signal) return Boolean;
   function "not" (S : in Signal) return Boolean;
   function "xor" (L, R : in Signal) return Boolean;

   function "and" (L : in Item; R : in Signal) return Boolean;
   function "or" (L : in Item; R : in Signal) return Boolean;
   function "xor" (L : in Item; R : in Signal) return Boolean;

   function "and" (L : in Signal; R : in Item) return Boolean;
   function "or" (L : in Signal; R : in Item) return Boolean;
   function "xor" (L : in Signal; R : in Item) return Boolean;

end Event_Simulator.Logical_Ops;
