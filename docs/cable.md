# The cable link: the network over a serial cable

**Status: SLIP over a serial port, built and tested (2026-11-13). PPP, Windows 9x's Direct Cable Connection and
the parallel port are to come.**

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
- **No checks of its own:** SLIP relies on IP's and TCP's checksums. Nothing is forwarded between the cable and
  a network card: the other end reaches this PC, not the network behind it.

## Tested

- Loop A (`make loopa-ssh`, the cable cases): on COM2 with the other end made by the test (echo requests of
  several sizes, the SSH port, the link switched off and on again as the server); and with no network card at
  all, the cable alone.
- The 486 bench's COM1 to the build host's `/dev/ttyS0` (2026-11-13): ping 16 ms, `ssh` logs in in 3 seconds,
  300 KB by SFTP each way, the copies identical.

## To come

- PPP (addresses agreed by the two ends, checked frames; what Windows 9x's Direct Cable Connection and modems
  speak), then the parallel port.
- The PC as a way to the network behind it (forwarding).
