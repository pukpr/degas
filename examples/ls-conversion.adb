with Ada.Unchecked_Conversion;

package body LS.Conversion is

   function Convert_To_Array is new Ada.Unchecked_Conversion (
      Bits_Range,
      Bit_Array);
   function Convert_To_Range is new Ada.Unchecked_Conversion (
      Bit_Array,
      Bits_Range);

   procedure To_Signals (BR : in Bits_Range; S : in out Signals) is
      B : Bit_Array := Convert_To_Array (BR);
   begin
      for I in  S'Range loop
         Set (S (I), B (I - S'First + 1));
      end loop;
   end To_Signals;

   function To_Range (S : in Signals) return Bits_Range is
      B : Bit_Array;
   begin
      for I in  S'Range loop
         B (I - S'First + 1) := +S (I);
      end loop;
      return Convert_To_Range (B);
   end To_Range;

   function To_Vector (BR : in Bits_Range) return Bit_Array is
   begin
      return Convert_To_Array (BR);
   end To_Vector;

   function To_Range (BA : in Bit_Array) return Bits_Range is
   begin
      return Convert_To_Range (BA);
   end To_Range;

   function Image (BA : in Bit_Array) return String is
      function Image (V : Boolean) return Character is
      begin
         if V then
            return '1';
         else
            return '0';
         end if;
      end Image;
      Str : String (BA'Range);
   begin

      for I in  Str'Range loop
         Str (I) := Image (BA (Str'Last - I + 1));
      end loop;
      return Str;
   end Image;

   procedure Trigger (S : in Signals; BA : in Bit_Array) is
   begin
      for I in reverse  BA'Range loop
         -- BA starts at 1, but S may start at 0
         Trigger (S (I - 1 + S'First), BA (I));
      end loop;
   end Trigger;

end LS.Conversion;
