generic
   type Bits_Range is mod <>;
package LS.Conversion is
   use LS.Sim;

   type Bit_Array is array (1 .. Bits_Range'Size) of Boolean;
   pragma Pack (Bit_Array);

   procedure To_Signals (BR : in Bits_Range; S : in out Signals);
   function To_Range (S : in Signals) return Bits_Range;

   function To_Vector (BR : in Bits_Range) return Bit_Array;
   function To_Range (BA : in Bit_Array) return Bits_Range;

   function Image (BA : in Bit_Array) return String;

   procedure Trigger (S : in Signals; BA : in Bit_Array);

end LS.Conversion;
