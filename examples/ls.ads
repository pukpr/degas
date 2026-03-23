with Event_Simulator.Logical_Ops;
package LS is  -- Logic Simulator
   package Sim is new Event_Simulator (Boolean, False, Boolean'Image);
   package Ops is new Sim.Logical_Ops;
end LS;
