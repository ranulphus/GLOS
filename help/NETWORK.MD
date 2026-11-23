# The network

With a network card GLOS knows (Realtek RTL8139, Intel 8255x, VIA Rhine, AMD PCnet-PCI, DEC 21140 and 21143,
NE2000) and a cable to your router,
GLOS asks the router for an address as it starts. The task bar's network icon shows the address; the Control
Panel's **Network** page shows the rest and sets the PC's name. Other computers can reach the PC as
*name*.local.

If a DOS packet driver is loaded before GLOS, the card is DOS's and GLOS leaves it alone.

## From this PC

- [Web Reader](WEB.MD) reads web pages; [Chat](CHAT.MD) is for IRC.
- The [Terminal](TERM.MD) logs in to other computers: `ssh user@computer`, `telnet computer`, and saved
  sessions for both.
- [Files](FILES.MD) opens another computer's folders: `sftp://user@computer/folder`.
- `FETCH address` at the command line fetches a file from the web; `PKG` installs programs from GLOS's and
  FreeDOS's collections (`PKG LIST`, `PKG INSTALL name`).
- The clock is set from the network (the Date and Time page).

## To this PC

GLOS has an SSH server, so another computer can log in to GLOS's command line (`ssh glos@name.local`), run a
command, and copy files to and from the PC with sftp or scp. It lets in only those whose public key is in the
file KEYS\AUTHKEYS in GLOS's folder; there are no passwords. Without the key files there is no server.

The screen itself can be watched and used from another computer when "Remote Desktop" is turned on in the
Control Panel. While someone is looking, the task bar says so.

## Two PCs and a cable

Two PCs running GLOS can be joined by a null-modem serial cable, with no network card in either: one is the
"server", the other the "client", chosen on the Network page (Cable link). If the server has a network card,
the client can use its network too.
