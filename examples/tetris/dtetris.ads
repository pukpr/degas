

--::::::::::
--dtetris.adb
--::::::::::
-- Version for display terminal interactivity
with Ada.Text_IO;
with Tetris;
procedure DTetris is new Tetris(Ada.Text_IO.Get_Immediate);

