# The cable link: the network over a serial or a parallel cable

**Status: SLIP over a serial port, built and tested (2026-11-13); the far PC as a member of this PC's network
(N1, 2026-11-14); PLIP over a parallel port (N2, 2026-11-15, in Loop A: two benches with a LapLink cable are
owed). To come: membership and the installer over the parallel cable (N3). Not planned: PPP and Windows 9x's
Direct Cable Connection. The design: [v1/cable-network.md](v1/cable-network.md).**

A PC that runs GLOS can be reached over a serial cable instead of, or beside, a network card: IP between the
two ends of the cable. SSH, SFTP, the installer, VNC and the apps' sockets work over it as over Ethernet. It is
how a PC whose network card GLOS has no driver for is installed and worked on (the owner's idea, 2026-11-13:
"direct cable connection").

## Using it

- **The cable:** a null-modem (crossed) serial cable between the two computers' COM ports. Three wires are
  enough (send, receive, ground): no handshake lines are used.
- **On the PC,** in `GLOS.CFG`:

      [net]
      cable = client COM1

  `server` or `client`, `COM1` to `COM4`, and a speed after it if not 115200 (`cable = client COM1 57600`).
  While GLOS runs: `NETWORK CLIENT COM1`, `NETWORK SERVER COM2 38400`, `NETWORK CABLE OFF` (`glos net client com1`
  over SSH). `NETWORK` shows the link and its counts. A PC with no network card GLOS drives needs the setting
  in `GLOS.CFG`: the network is started for the cable alone.
- **The setup floppy** carries the setting when made with `make floppy FLOPPY_CABLE="client COM1"`, and
  `install.py --cable "client COM1"` gives it to the installed system.
- **On a Linux computer** at the other end: `tools/cable.sh up` (then `down`): `sudo slattach` on `/dev/ttyS0`
  and an interface with the server's address. Then `ssh glos@192.168.250.2`, `tools/floppy/install.py
  192.168.250.2 ...`.
- **Between two PCs that run GLOS:** one is the server, the other the client.
- **A parallel cable instead** (N2): a LapLink ("null printer") cable between the two PCs' parallel ports, and
  `LPT1` to `LPT3` where the serial link has `COM1`: `cable = client LPT1` in GLOS.CFG, `NETWORK SERVER LPT1`,
  `NETWORK CLIENT LPT1`. No speed is given. Any parallel port will do. One cable link at a time: serial or
  parallel.

The roles say only which address is whose: **the server is 192.168.250.1, the client 192.168.250.2.** There is
no DHCP and no `.local` name on the cable.

## What it is

- **SLIP** (RFC 1055) on the port, 8 bits, no parity, one stop bit; packets of 576 bytes at most.
- **The port is the kernel's while the link is up:** DOS programs read nothing there, and its interrupt (IRQ 4
  for COM1 and COM3, 3 for COM2 and COM4) is GLOS's. It is DOS's again when the link is switched off or GLOS
  leaves. **On COM1 the kernel's log stops going to the port** (it is in memory: `LOG`).
- **Speed:** 115,200 baud is about 11 KB a second: 300 KB each way took 64 seconds on the 486 bench, an install
  of GLOS (5.5 MB) takes 8 to 9 minutes. A chip with no FIFO (an 8250 or 16450: `fifo=0` in `NETWORK`) loses
  bytes at that speed (`overruns`); TCP sends them again, and a lower speed stops it.
- **No checks of its own:** SLIP relies on IP's and TCP's checksums.
- **The far PC as a member of this PC's network** (first cut, 2026-11-14; [v1/cable-network.md](v1/cable-network.md)):
  a PC with a network card and a server link lets the PC at the cable's far end onto its network, with an
  address of its own from the network's DHCP server. `[net] cable_share = off` keeps the cable to the two PCs;
  `[net] cable_join = off` on the far PC stops it asking.

## The parallel link (N2)

- **PLIP** as Crynwr's packet driver and Linux speak it (`kernel/drv/plip.c`): Ethernet frames, four bits at a
  time over the cable's five crossed lines, each nibble acknowledged; a packet is announced on the other PC's
  ACK line (its interrupt), then its length, its bytes and their sum. The interface is an Ethernet one to the
  network stack (ARP, DHCP), with a made-up hardware address of its own.
- **The port is the kernel's while the link is up:** nothing prints through it, and its interrupt (IRQ 7 for the
  ports at 378h and 3BCh, IRQ 5 for 278h) is GLOS's. It is DOS's again when the link is switched off or GLOS
  leaves.
- **The wire has a thread of its own**, which runs beside the PC's programs and not ahead of them: every nibble
  is a busy wait, 6000 of them in a full packet. Its waits are long (a quarter of a second a nibble), so two
  GLOS PCs keep a packet going while either is busy. Linux and Crynwr wait about three milliseconds: a GLOS PC
  cabled to one of those keeps up only while it has nothing else to do, and Linux's plip, which does not answer
  ARP, has not been tried.
- **Speed:** 40 to 60 KB a second is what PLIP gives on period hardware (to be measured on the benches). 86Box's
  PLIP device has no such limit: Loop A's 256 KB each way takes under two seconds.
- `NETWORK` shows the link: `cable=client lpt=1 io=378 irq=7 ip=... peer=...` and the wire's counts (`rx_errors`
  and `tx_errors`: packets that stopped part way or whose sum was wrong; `collisions`: both ends began at once).

## Tested

- Loop A (`make loopa-ssh`, the cable cases): on COM2 with the other end made by the test (echo requests of
  several sizes, the SSH port, the link switched off and on again as the server); and with no network card at
  all, the cable alone.
- The 486 bench's COM1 to the build host's `/dev/ttyS0` (2026-11-13): ping 16 ms, `ssh` logs in in 3 seconds,
  300 KB by SFTP each way, the copies identical.
- Loop A (`make loopa-ssh`, `ssh-plip-*`, N2): a PC with no network card and 86Box's PLIP device on LPT1 (the
  Loop A runner's `--net plip`), `cable = client LPT1`: an address by DHCP over the cable, SSH, 256 KB by SFTP
  each way with the copies identical and no error on the wire, and the port and the interrupt controller as DOS
  had them after `glos exit`. On the Pentium II and the 486 profiles.

## To come

- The parallel port (a LapLink cable, Crynwr's PLIP: several times a serial port's speed).
- **One bridged network** (the owner, 2026-11-14): GLOS PCs link by Ethernet, serial or parallel, and a cable link
  can be made part of the network its PC's Ethernet card is on, so that the PC at the cable's far end is a
  member of that network with an address of its own. The design comes first.
- Not planned: PPP and interworking with Windows 9x's Direct Cable Connection (it was the idea's example, not a
  requirement).
