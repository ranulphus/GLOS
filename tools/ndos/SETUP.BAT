@ECHO OFF
REM GLOS Setup (docs/v1/native-dos.md, N5; phase G's on-PC installer): GLOS put on one of this PC's hard disks
REM from its setup floppy alone, to start with no DOS. Text, in GLOS's own shell. With nobody at a keyboard every
REM question's answer is the one that changes nothing (CHOICE's /T key): GLOS Setup then stops.
ECHO.
ECHO GLOS Setup puts GLOS on one of this PC's hard disks, to start with no DOS.
ECHO Nothing is written until the last question has been answered.
ECHO.
DISK
ECHO.
CHOICE /C:12340 /N /T:0,30 "Which disk? [1,2,3,4] (0: stop) "
IF ERRORLEVEL 5 GOTO STOP
SET DISKN=4
IF NOT ERRORLEVEL 4 SET DISKN=3
IF NOT ERRORLEVEL 3 SET DISKN=2
IF NOT ERRORLEVEL 2 SET DISKN=1
ECHO.
DISK %DISKN%
IF ERRORLEVEL 1 GOTO NODISK
ECHO.
ECHO   1  beside what is on it, in its free space (the systems there stay, on GLOS's start-up menu)
ECHO   2  on all of it: EVERYTHING ON THE DISK IS ERASED
ECHO   0  stop: nothing is changed
CHOICE /C:120 /N /T:0,30 "How? [1,2,0] "
IF ERRORLEVEL 3 GOTO STOP
SET HOW=/FREE
IF NOT ERRORLEVEL 2 GOTO NAME
ECHO.
ECHO EVERYTHING ON DISK %DISKN% WILL BE ERASED.
CHOICE /C:YN /T:N,30 Erase it
IF ERRORLEVEL 2 GOTO STOP
SET HOW=/ERASE
:NAME
ECHO.
SET PCNAME=glos
SET /P "PCNAME=This PC's name on the network (Enter: glos): "
ECHO.
ECHO Its keyboard:  1  US    2  UK
CHOICE /C:12 /N /T:1,30 "Keyboard? [1,2] "
SET LAYOUT=us
IF ERRORLEVEL 2 SET LAYOUT=uk
ECHO.
ECHO Disk %DISKN% %HOW%, the name %PCNAME%, the %LAYOUT% keyboard.
CHOICE /C:YN /T:N,30 Put GLOS there now
IF ERRORLEVEL 2 GOTO STOP
DISK %DISKN% INSTALL %HOW% /SET:net.hostname=%PCNAME% /SET:region.layout=%LAYOUT% /Y
IF ERRORLEVEL 1 GOTO FAILED
ECHO.
ECHO GLOS is on disk %DISKN%. Take the floppy out, then press a key: the PC starts again from the disk.
ECHO GLOS's programs come over the network afterwards (PKG), or from its USB stick or CD.
PAUSE
REBOOT
GOTO END
:NODISK
ECHO There is no disk %DISKN%.
GOTO STOP
:FAILED
ECHO.
ECHO GLOS Setup stopped: DISK said why, above.
GOTO END
:STOP
ECHO.
ECHO GLOS Setup stopped: nothing was changed.
:END
