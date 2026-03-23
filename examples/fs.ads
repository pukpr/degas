with Event_Simulator.Fp_Ops;
package FS is  -- Floating-point simulator
   package Sim is new Event_Simulator(Float, 0.0, Float'Image);
   package Ops is new Sim.Fp_Ops;
end;
