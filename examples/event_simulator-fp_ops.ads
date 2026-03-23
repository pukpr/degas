generic
   with function "+" (L,R : Item) return Float is <>;
   with function "*" (L,R : Item) return Float is <>;
   with function "-" (L,R : Item) return Float is <>;
   with function "-" (L : Item) return Float is <>;
package Event_Simulator.Fp_Ops is
   function "+" (L,R : in Signal) return Float;
   function "*" (L,R : in Signal) return Float;
   function "-" (L,R : in Signal) return Float;
   function "-" (S : in Signal) return Float;

   function "+" (L : in Item;
                 R : in Signal) return Float;
   function "*" (L : in Item;
                 R : in Signal) return Float;
   function "-" (L : in Item;
                 R : in Signal) return Float;

   function "+" (L : in Signal;
                 R : in Item) return Float;
   function "*" (L : in Signal;
                 R : in Item) return Float;
   function "-" (L : in Signal;
                 R : in Item) return Float;

end;
