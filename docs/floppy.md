# GLOS's setup floppy

A floppy (3.5" 1.44 MB or 720 KB, or 5.25" 1.2 MB) that boots a PC into GLOS with its network on, so that GLOS
can be put on the PC's hard disk from another computer, over SSH and SFTP. For bench PCs, and any PC with a floppy drive and a network card GLOS
drives (NE2000 and its clones, ISA or PCI; the RTL8139 family; Intel's PRO/100).

GLOS itself is small enough for it: `GLOS.EXE` (55 KB) and `GLOSK.BIN` (780 KB; 400 KB packed) are the whole of
GLOS with its network, SSH and SFTP. The programs (Files, the Terminal and the rest: about 4.5 MB) go to the hard
disk.

## Making it

    make all apps build/apps/FETCH.EXE build/apps/PKG.EXE      (in the dev container)
    make floppy                                                build/floppy/glos1440.img
    make floppy FLOPPY_SIZE=720                                build/floppy/glos720.img (or 1200)

- **On the floppy:** FreeDOS 1.4's kernel and `COMMAND.COM` with its `FDISK`, `FORMAT`, `SYS`, `XCOPY`, `ATTRIB`,
  `MEM`, `MORE` and `DELTREE` (FreeDOS's own programs, unchanged, from its boot floppy: `tools/setup/versions.mk`);
  `A:\GLOS` with `GLOS.EXE`, `GLOSK.BIN`, a `GLOS.CFG` that names the PC, and `KEYS\`. About 1.1 MB of the disk.
- **The smaller disks** carry the kernel packed (`tools/packkernel.py`: deflate, half the size; `GLOS.EXE` unpacks
  it and checks its sum, and takes either form under the name `GLOSK.BIN`). The 720 KB disk has FreeDOS's `FDISK`,
  `FORMAT` and `SYS` alone, and 12 KB to spare.
- **Who may log in:** `FLOPPY_AUTHKEYS`, an ed25519 public key or a file of them (by default `~/.ssh/glos_bench.pub`,
  else `~/.ssh/id_ed25519.pub`). GLOS takes public keys only.
- **Who the PC is:** a host key made once for the floppy (`build/floppy/keys/HOSTKEY`, kept from one image to the
  next). `install.py` trusts that key and no other, and gives it to the installed system, so the PC stays the PC.
- **Its name:** `FLOPPY_HOST` (`glos-setup`): the PC answers as `glos-setup.local`.

Write the image to a floppy as it is (`dd if=build/floppy/glos1440.img of=/dev/sdX` with a USB floppy drive; a
floppy emulator takes the file). Leave the floppy write-enabled: GLOS keeps its random seed on it.

## Using it

1. Start the PC from the floppy, its network cable in. FreeDOS starts GLOS, which shows what it is doing
   (`/VERBOSE`): the network card it found, `GLOS-SSH listen`. Loading GLOS from a floppy takes most of a minute.
   Then the screen says that GLOS is running, the PC's name and the address it was given, and that
   Ctrl-Alt-Del restarts the PC (GLOS's idle screen: every GLOS PC with no desktop shows it).
2. From the other computer, in GLOS's checkout:

       tools/floppy/install.py glos-setup.local

   - **A PC with DOS on its disk:** GLOS goes into `C:\GLOS`, and the three lines to add to `C:\AUTOEXEC.BAT` are
     shown. With `--autoexec` they are added (the file as it was is kept as `C:\AUTOEXEC.PRE`).
   - **A new or empty disk:** `--new-disk` makes one partition over the first hard disk, restarts the PC (from
     the floppy again) so that DOS sees it, formats it, puts FreeDOS on it from the floppy, then GLOS, started by
     its `AUTOEXEC.BAT`.
   - **A disk to be erased:** `--new-disk --erase-existing`. Everything on the disk is lost. Nothing is ever
     partitioned or formatted without `--new-disk`, and never a disk that has a C: without `--erase-existing`.
   - `--name NAME` is the installed PC's name on the network (`glos`: `glos.local`); `--key FILE` the private
     key to log in with.
3. Take the floppy out and restart the PC (`--reboot` does it, or `ssh glos@glos-setup.local glos reboot`). It
   starts GLOS from the disk and answers as `NAME.local` to the same keys.

What is on the disk then: `C:\GLOS` with `GLOS.EXE`, `GLOSK.BIN`, the programs, `CERTS.DAT`, `SMOOTH.DAT`,
`GLOS.CFG` and `KEYS\` (the host key, `AUTHKEYS`, the package key). GLOS runs without its desktop until
`[display]` in `GLOS.CFG` says otherwise (`glos display on`, the Control Panel).

## Limits

- **The network card** must be one GLOS drives, with DHCP on the network. An ISA NE2000 is found at 300h, 280h,
  320h, 340h or 360h, not in Plug and Play mode, and no packet driver loaded.
- **During `--new-disk` the floppy must be what the PC starts from** (the BIOS's order: A: before C:): between
  the partitioning and the format the disk can't be started from.
- **`FDISK` shows nothing over SSH** (it writes to the screen itself): `install.py` has GLOS's kernel read the
  partition table instead (`glos disktest list`). A disk with partitions of any system is never taken for a new one.
- **A partition that isn't formatted** makes DOS stop and ask (Abort, Retry, Fail) when it is looked at: after
  `--new-disk`'s restart nothing looks at C: before `FORMAT`.
- **Tested** in Loop A (`make loopa-floppy`: a 486DX4 with an NE2000 and a blank disk, from the 1.44 MB and the
  720 KB images; the packed kernel also on the Pentium II and the 486DX2, with and without an XMS driver).
