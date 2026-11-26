# CMOS images with the CD-ROM first in the boot order

`jobs.py cdrom`'s cdboot cases give one of these to RetroGPU's run.py (`--nvr`) so that the PC starts GLOS's boot
CD (`make cd-image`) as a PC set up to start from its CD does. The run's CMOS is never kept. Each is named as 86Box
names the machine's CMOS:

- `bf6.nvr`: the Abit BF6 (Award BIOS 6.00PG). Advanced BIOS Features: First Boot Device CDROM, then HDD-0, then
  LS120, Boot Other Device enabled.
- `hot433a_v451pg.nvr`: the Shuttle HOT-433A of the 486 profiles (Award BIOS 4.51PG). BIOS Features Setup: Boot
  Sequence CDROM,C,A. Its first 16 bytes (the clock and its status registers) are Loop A's own CMOS's again: the
  file 86Box wrote held the clock as GLOS left it at the end.

Everything else is as Loop A's own CMOS for the machine had it (the hard disks' types are Auto: the numbers there
are what the BIOS found last). Made 2026-11-26 by starting the machine with a boot CD, Del in the POST, the setup's
keys sent by 86Box's monitor (`key tap`), the change saved (F10, Y, Enter from the main menu): the file 86Box wrote
at its end.
