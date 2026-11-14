# Cables as part of the network: the parallel link and the bridged network (design)

**Status: agreed by the owner (2026-11-14, the answers at the end). Nothing here is built yet; N1 is next.** The
serial link it builds on is [docs/cable.md](../cable.md).

## What the owner asked for (2026-11-14)

GLOS PCs link to each other by Ethernet, a serial cable or a parallel cable, and together form one bridged
network: a cable link optionally becomes part of the network its PC's Ethernet card is on. Not wanted:
interworking with Windows' Direct Cable Connection, and so no PPP.

In a picture: PC A has a network card on the house's network and a cable to PC B, which has no card GLOS drives.
B should be on the house's network like any other PC: an address of its own from the same DHCP server, reached
by name (`b.local`) and by SSH from anywhere on it, and able to reach the Internet through the same router.

## Two parts

1. **The parallel link**: a second kind of cable, several times faster than serial.
2. **Membership**: what A does so that B, at the far end of either kind of cable, is a member of A's network.

They are independent: membership works over the serial link GLOS already has.

## 1. The parallel link

- **Cable:** a LapLink ("null printer", "InterLink") cable between two parallel ports. Any parallel port will
  do: only the standard signals are used (five wires each way).
- **Protocol: PLIP as Crynwr's packet driver and Linux speak it** (mode 0: four bits at a time, each nibble
  acknowledged on the fifth line; a packet is a length, the frame, a checksum byte).
  - Why this one: 86Box emulates it (a PLIP device on a parallel port, joined to the emulated network), so Loop A
    tests the driver against an implementation that was itself tested against Linux's and Crynwr's; and a GLOS PC
    can then also be cabled to a Linux PC or to plain DOS with Crynwr's PLIP.
  - What it gives: 40 to 60 KB a second on period hardware, against 11 KB a second for serial at 115,200 baud.
    An install of GLOS in about two minutes instead of nine.
  - What it costs: the CPU waits on every nibble. The kernel does that on a thread of its own at the lowest
    priority that still moves data, as SFTP's bulk work is done, so a program in front keeps its turn.
- **In GLOS it is a network card**: PLIP carries Ethernet frames, so the link is given to the network stack as
  one more card (a made-up address of its own, as PLIP has), and DHCP, names and SSH work over it unchanged when
  the far end is a network (86Box's, or a GLOS PC that offers membership: part 2).
- **Opt-in as the serial link is:** `NETWORK SERVER LPT1`, `NETWORK CLIENT LPT1`, `[net] cable = client LPT1` in
  GLOS.CFG. The port (LPT1 378h IRQ 7, LPT2 278h IRQ 5, LPT3 3BCh) is the kernel's while the link is up; DOS
  programs cannot print through it meanwhile.
- **Not chosen:** a faster protocol of GLOS's own over EPP or ECP ports (hundreds of KB a second, but both ends
  must have such a port and a different cable, nothing else speaks it, and 86Box cannot test it). It can follow
  if PLIP's speed proves the limit.

## 2. Membership: the far PC on the near PC's network

### What A (the PC with the card) does

A keeps a short list of the PCs at the far ends of its cables. For each:

1. **An address from the network's own DHCP server.** B asks for an address over the cable exactly as it would
   over Ethernet. A passes the request on to its network and the answer back (a relay), so the house's router
   gives B an address of its own and sees it as one more PC. A reads the answer as it passes and so learns B's
   address and how long it holds.
2. **A answers for B on the Ethernet** (proxy ARP): when another PC asks who has B's address, A says "send it to
   me", and passes what arrives down the cable. Packets from B go the other way. No setting of the network card
   is changed and every card GLOS drives can do it.
3. **Names and announcements cross the cable**: the `.local` name lookups, and broadcasts (how LAN games and
   file shares find each other), are copied both ways, held to a rate a serial cable can carry.

B needs nothing special: it runs the same DHCP client over its cable that it would over a card. Until an answer
comes (or if the far end is not a GLOS PC with a network, such as the Linux end of `tools/cable.sh`), the link
has today's fixed pair of addresses, so nothing that works now stops working.

### Why this way and not a true Ethernet bridge

A true bridge copies every Ethernet frame both ways and needs A's card to listen to all traffic.

- A serial cable carries 11 KB a second. A house's network easily has more than that in broadcasts and other
  PCs' chatter alone; a true bridge would have to throw most of it away anyway.
- It needs a listening mode in every card driver, and some cards do it badly.
- What it would add is non-IP traffic (IPX), and GLOS's own network is IP: DOS programs do not get at the card
  at all today.

The way above looks the same from every other computer (B has its own address, name and lease) and costs the
cable only B's own traffic.

### Not NAT

B is not hidden behind A's address (the owner, 2026-11-14): other PCs reach B directly by its own address.

### Chains and several cables

A can have a serial and a parallel cable at once, each with a PC on it. A PC on a cable that has a second cable
to a third PC (a chain) is not in the first version: the third PC would be reachable only from the second.

### When A has no network card either

Two GLOS PCs joined only by a cable have the fixed pair of addresses, as today, and reach each other by them.

### What is opt-in

The owner's word was "optionally". Proposed: membership is offered by a PC whose link is in the server role
and that has a network card, unless `[net] cable_share = off`. The far PC joins only if its own link asks
(the DHCP client on a cable is on by default, off with `cable_join = off`). The question below.

## How it would be tested

- **Loop A, the parallel link:** a PC with no network card and 86Box's PLIP device on LPT1: DHCP, the SSH suite's
  transfer checks, the link switched off and on.
- **Loop A, membership:** two emulated PCs with their COM2 ports joined on the host: A with a network card, B
  with none. B gets an address from the emulated network's DHCP server through A, is reached by SSH from the
  host at that address, reaches a server on the host, and loses its membership when the cable is unplugged.
- **Benches:** the 486 bench and the P3 are cabled serially already: the 486 joins the house's network through
  the P3 with its own card unused, then the other way round. The parallel link needs a LapLink cable between
  two benches.

## Steps

| | What | Shows it works |
|---|---|---|
| N1 | Membership over the serial link: the relay, the proxy answers, the copying of names and broadcasts; the DHCP client on a cable | Loop A with two PCs; the 486 through the P3 |
| N2 | The parallel link (PLIP as a network card), its commands and setting | Loop A against 86Box's PLIP; two benches with a LapLink cable |
| N3 | Membership over the parallel link; the setup floppy and installer offered over it | Loop A; an install over the parallel cable |

N1 first: the hardware for it is cabled already, and it is the larger unknown.

## Decided by the owner (2026-11-14)

1. **Membership as designed**: the address request relayed, A answering for the far PC on the Ethernet, its
   traffic passed, names and broadcasts copied. Not a true Ethernet bridge.
2. **On by default for a server link** on a PC with a network card; `[net] cable_share = off` switches it off.
3. **PLIP** for the parallel cable.
4. **Serial first** (N1, then N2, N3). The owner has a LapLink cable for the benches' proof of N2.
