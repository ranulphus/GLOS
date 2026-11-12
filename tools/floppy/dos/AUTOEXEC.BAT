@ECHO OFF
SET PATH=A:\FREEDOS\BIN
ECHO.
ECHO GLOS setup floppy: starting GLOS. It asks the network for an address
ECHO (DHCP), then waits for SSH and SFTP as @HOST@.local (user glos).
ECHO From the other computer: tools/floppy/install.py @HOST@.local
ECHO.
:glosrun
A:\GLOS\GLOS.EXE /VERBOSE
IF ERRORLEVEL 200 GOTO glosrun
ECHO GLOS has ended. A:\GLOS\GLOS.EXE starts it again.
