package body Testing_Input is

   Counter : Integer := 0;


   procedure Get_Immediate( Item : out CHARACTER;
                            Available : out BOOLEAN ) is
   begin
     
      if Counter mod 18 = 0 then
         Item := '4';
         Available := True; 
      elsif Counter mod 19 = 0 then
         Item := '6';
         Available := True; 
      elsif Counter mod 17 = 0 then
         Item := '5';
         Available := True; 
      elsif Counter mod 267 = 0 then
         Item := '2';
         Available := True; 
      else
         Available := False; 
      end if;        
      Counter := Counter + 1;  
   end Get_Immediate;

end;
