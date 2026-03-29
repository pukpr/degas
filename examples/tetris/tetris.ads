

--::::::::::
--tetris.ads
--::::::::::
--	Tetris Ada
--
-- This version of Tetris uses Ada tasking and a text-mode screen.
-- 
-- Before running, note the following:
--  1. You must have ANSI screen character handling (i.e. ANSI.SYS for DOS)
--     ANSI.SYS is apparently built-in for Win95 and OS2 
--  2. Use the numeric key-pad arrows (i.e. NumLock on DOS).
--     Otherwise, use the numeric keys: 2=down, 4=left, 5=rotate, 6=right
--
-- Portability issues:
--  1. Get_Immediate is non-portable code for getting chars.
--     Instantiate generic Tetris with version appropriate for OS
generic
   with procedure Get_Immediate( Item : out CHARACTER;
                                 Available : out BOOLEAN );
procedure Tetris;
