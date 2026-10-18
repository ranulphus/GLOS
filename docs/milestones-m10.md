# GLOS M10: community features

The task-level breakdown of PRD §19's M10, in the form of [milestones-m9.md](milestones-m9.md). PRD sections:
§8.4 (TLS), §8.5 (services exposed), §9.8 (capture and VNC hooks), §10.6 (apps), §11 (community features),
§18 (layout); decisions D13, D19, D29, D35, D37, D52; questions Q4 and Q13.

**Exit (PRD §19), per feature; each ships when it passes its own gate:**
- HTTPS fetches from GitHub and a FreeDOS mirror with certificate checks.
- A signed package installs and uninstalls cleanly.
- A stock VNC client views and drives the desktop through an SSH tunnel.
- A capture's frames are identical to `glos shot`.

The PRD gives SNTP, the SSH client and UTF-8 with anti-aliased fonts no gate of their own; this plan gives each
one (steps A, D and G).

## What M10 adds, in one paragraph

GLOS learns the time from the network and keeps DOS's clock and the RTC by it, in the zone the person sets. Apps
get the network: names looked up and TCP connections made through the kernel's lwIP, so that ring-3 code can
speak TLS (BearSSL, trusting Mozilla's roots) and fetch over HTTPS. On that come three things a person uses: a
command to download files, an SSH and SFTP client in the terminal, and a package manager that installs and
removes software signed by GLOS's key. The desktop can be watched and driven by a VNC client through an SSH
tunnel, and recorded losslessly to the build host. Text in apps reaches beyond Latin-1 to Greek and Cyrillic, and
fonts can be drawn smoothed.

## Decisions

### The owner's (2026-10-18)

- **Q4 answered: packages are hosted on GitHub and signed with the owner's offline key** (PRD D58). A packages
  repository (to be made, e.g. `ranulphus/glos-packages`) holds a signed index and the packages' zips, fetched over
  HTTPS. The owner generates the Ed25519 key and keeps it off the build host; a host tool (`tools/pkg`) signs a
  release when the owner runs it. GLOS ships the public key. Loop A uses a test key of its own, as it does for SSH.
- **HTTPS trusts Mozilla's root set** (PRD D59): about 150 roots, as curl and most distributions ship them
  (MPL-2.0 data), kept in a file that a package refreshes.
- **Q13 answered (recording): PNG frames and a time index** (PRD D60). `glos rec` sends each changed frame as a
  PNG, the same bytes `glos shot` gives, with the time it was shown; a host tool writes them out as files and an
  index, which ffmpeg turns into a video.
- **The SSH client is a full terminal, with SFTP; no tunnels yet** (PRD D61). An xterm-compatible screen in the
  terminal (cursor addressing, colours: vi, top and nano work), Ed25519 key and password login, host keys checked
  against a known-hosts file, and an `sftp` client; port forwarding comes later.

### Proposals (taken here; they go into the PRD as they are built)

- **Time.** SNTP is lwIP's client (`src/apps/sntp`, from the same lwIP 2.2.0 archive, its hash checked again),
  asking `[time] server` (default `pool.ntp.org`; DHCP's NTP server if it gives one) when the network comes up
  and every `[time] interval` hours. The RTC and DOS keep **local** time, as DOS always has; `[time] zone` is a
  POSIX TZ string (`GMT0BST,M3.5.0/1,M10.5.0`; default `UTC0`), and DJGPP programs get it as `TZ` in their
  environment. A step of more than a second sets the RTC, then DOS's date and time (INT 21h 2Bh and 2Dh, through
  the DOS server). `glos time` over SSH shows and sets. The task bar's clock follows. DNS is lwIP's resolver with
  the servers DHCP gives.
- **Sockets for apps (API 1.3).** `GLOS_FN_NET`: a name looked up; a TCP connection made, written, read (waiting
  as `GLOS_FN_JOB` waits, so that a kill is seen) and closed; `GLOS_EV_NET` when one has data or has closed. Each
  app has its own table of connections, closed when it ends. Listening sockets aren't needed for M10 and are left
  out. `GLOS_FN_RANDOM` gives an app bytes from the kernel's pool (TLS's and SSH's seeds). In the SDK,
  `include/glos/net.h` with a small BSD-style layer (`socket`, `connect`, `send`, `recv`, `close`,
  `getaddrinfo`) so that portable code builds.
- **TLS and HTTPS** (`sdk/tls`, `sdk/http`). BearSSL 0.6 (MIT) built for DJGPP, ring 3 (D19), TLS 1.2 client
  only. The trust anchors are made on the host from curl's `cacert.pem` (pinned by its SHA-256) into
  `C:\GLOS\CERTS.DAT`, loaded at run time. A certificate is checked only once the clock has been set by SNTP
  (or the person has set it); before that, HTTPS refuses with a reason. HTTP/1.1 with redirects, chunked
  bodies and lengths. `FETCH.EXE URL [FILE]`: a downloader for the terminal and SSH, with progress and the reason
  for a refusal (`expired`, `not trusted`, `wrong name`, `clock not set`).
- **The SSH and SFTP client** (`sdk/sshc`, in the terminal). TinySSH's crypto built for ring 3: curve25519-sha256,
  ssh-ed25519 host keys, chacha20-poly1305@openssh.com (OpenSSH's defaults since 6.5); a server without them is
  refused with a reason. Login by the person's Ed25519 key (`ssh-keygen` in the shell makes one, in
  `C:\GLOS\SSH\`) or a password. Host keys are kept in `C:\GLOS\SSH\KNOWNHST.TXT`: a new one is shown and asked
  about, a changed one refused. The terminal gains an xterm screen (cursor movement, erasing, scroll regions,
  the alternate screen, colours, application keys) used while a session runs; its size goes to the server and
  follows the window. `sftp` is a shell command with `ls`, `cd`, `lcd`, `get`, `put`, `rm`, `mkdir`, `rmdir`,
  `rename`.
- **Packages** (`apps/pkg`, `tools/pkg`). A package is a zip of the files to install with a `GLOSPKG.INI`
  manifest (name, version, summary, where it goes, its programs' profiles); a source is an `INDEX.TXT` (name,
  version, size, SHA-256 of each zip) with `INDEX.SIG`, an Ed25519 signature by a key GLOS trusts
  (`C:\GLOS\KEYS\PKG\*.PUB`). Zips are read with stb_image's inflater (already in the tree). What a package put
  where is kept in `C:\GLOS\PKG\NAME.LST`, so that removal takes exactly that away, and its profiles' lines too.
  `PKG.EXE` is both an app (sources, what is installed, what can be, install, remove, upgrade) and a command
  (`PKG INSTALL NAME`, for the terminal and SSH). FreeDOS's repositories (zips with LSM metadata, unsigned) are a
  second kind of source, trusted through HTTPS alone and marked so.
- **VNC** (`kernel/wm/vnc.c`). An RFB 3.8 server in the kernel, fed by the window manager's damage, as PRD §9.8
  has it. Off by default (`[vnc] enable`); in M10 it is reached only through SSH's `direct-tcpip` to
  `127.0.0.1:5900` (the agent's tunnel), so no password: binding it to the LAN (with VNC's password) comes after
  M10. Encodings Raw, CopyRect (moved windows) and Hextile, cheap on a 486; ZRLE would need a compressor the
  kernel lacks. Any true-colour pixel format the client asks for; an 8-bit desktop's palette is converted. Keys
  (X keysyms) and the pointer go in as `glos key` and `glos mouse` do. The task bar shows the remote-session mark
  while a viewer is connected (D35).
- **Capture** (`glos rec`). Frames from the same damage as VNC, at most `--fps` a second (default 10), each a PNG
  the same bytes `glos shot` would give at that moment, streamed down the SSH channel after a line of its time
  and length; `tools/rec.py` on the host writes `NNNNN.png` and `index.txt`.
- **UTF-8 and smoothed fonts.** The fonts gain Latin Extended-A, Greek and Cyrillic (DejaVu has them;
  `tools/mkfont.py` makes the glyph table sparse). DOS file names are shown and typed through the active code
  page (INT 21h 6601h; tables for 437, 850, 852, 866 and 737). Font smoothing (`[display] font_smoothing`, off by
  default, when an app next starts) is the toolkit's, in ring 3: stb_truetype draws the same DejaVu files into
  coverage, cached per glyph, blended at 16 and 32 bits; at 8 bits text stays crisp. The kernel's own text (title
  bars, the task bar) stays bitmap.

## The steps

Each step ends with its tests passing; each feature's gate is its step's exit. Development in `~/wt-glos-m5`
(branch `m10`), long runs in `~/wt-glos-test`.

- **A. Time and names.** SNTP, DNS, `[time]`, `glos time`, the RTC and DOS set, the task bar in local time.
  T: `make loopa-time`: an NTP server on the host (a small one in `tests/host`, its answers known) through SLiRP:
  the clock set, DOS's `DATE`/`TIME` and the RTC agree with it in the zone set, a second sync after a step, the
  zone's summer time; and `pool.ntp.org` answering through SLiRP when the host has the internet.
- **B. Sockets for apps.** `GLOS_FN_NET`, `GLOS_EV_NET`, `GLOS_FN_RANDOM`, `include/glos/net.h`. T: NETTEST
  against an echo server on the host: names looked up, connections refused and timed out, 1 MB each way
  byte-exact, many at once, an app killed with connections open (all closed, the kernel's table clean).
- **C. TLS and HTTPS.** BearSSL, the anchors, `sdk/http`, `FETCH.EXE`. **Gate:** HTTPS fetches from GitHub and a
  FreeDOS mirror with certificate checks. T: `make loopa-https`: FETCH from `raw.githubusercontent.com` and a
  FreeDOS mirror over SLiRP, the files' SHA-256 checked on the host; a local HTTPS server on the host with its own
  CA (added to a test image's anchors) for the refusals: an untrusted root, a wrong name, an expired certificate,
  the clock not set.
- **D. The SSH and SFTP client.** `sdk/sshc`, the terminal's xterm screen, `ssh`, `sftp`, `ssh-keygen`.
  T: `make loopa-sshc`: an OpenSSH server in a container on the host with a known host key: login by key and by
  password; a command's output; vi editing a file (checked on the host); top's screen; a changed host key
  refused; SFTP put and get of 1 MB with matching SHA-256; the xterm screen's sequences against goldens on the
  host.
- **E. Packages.** `PKG.EXE`, `tools/pkg`, the format, sources. **Gate:** a signed package installs and uninstalls
  cleanly. T: `make loopa-pkg`: a repository on the host served over HTTPS (the test CA), signed with the test
  key: a package installed (its files, its profile, its launcher entry), removed (the disk and GLOS.CFG as
  before, compared file by file), upgraded; a tampered zip and a bad signature refused; a FreeDOS source listed.
- **F. VNC and capture.** `kernel/wm/vnc.c`, `glos rec`, `tools/rec.py`. **Gates:** a stock VNC client views and
  drives the desktop through an SSH tunnel; a capture's frames are identical to `glos shot`. T: `make loopa-vnc`:
  `vncdotool` (a stock RFB client, pinned) through `ssh -L`: its screen equal to `glos shot` at 8, 16 and 32
  bits, keys typed into Notepad and a press on a button seen; `glos rec` of a scripted scene, every frame equal
  to a `glos shot` taken with it.
- **G. UTF-8 and smoothed fonts.** The fonts' ranges, code pages, stb_truetype in the toolkit. T: goldens with
  Greek and Cyrillic text, file names through code pages 850 and 866, smoothing on at 16 and 32 bits (host and
  Loop A the same bytes).
- **H. The exit.** `make loopa-m10`, the regression, the gate, the docs, PRD v0.8, the release notes: which
  features have passed their gates.

## Status

- Planned 2026-10-18; the owner answered Q4, the roots, Q13 and the SSH client's scope the same day.
- **Step A done** (2026-10-18): `make loopa-time` passes on bf6 with the ne2kpci and on the 486dx2 with the ne2k.
  Covered: SNTP from a server on the host; summer time and its end seen by the kernel itself; a ten-day step; a
  change of zone; a zone refused; the time set by hand; TZ in a job's environment; names looked up; and
  pool.ntp.org through SLiRP agreeing with the host's clock. `loopa-config` and `loopa-control` pass with the new
  `[time]` settings and the Control Panel's "Date and Time" page. As built: supervisor.md §8.4.
  - It found an 86Box fault. The bf6's BIOS traps port 71h into SMM, and the PIIX4's RTC alias at 74h/75h had an
    index register of its own in 86Box. So after a change of century (the first SNTP set from 1980), every RTC read
    returned the century and the kernel's tick stopped. MGA-Glide's local patch 0118 fixes it, with selftest `rtc`.
    Until 0118 is in MGA-Glide's main and the shared emulator, a bf6 run that sets the clock needs
    `BOX86_DIR=~/.cache/mga-glide/86box-m10`.
- **Step B done** (2026-10-18): API 1.3. `GLOS_FN_NET` (names, connections, sending and receiving with waits a
  kill ends, closes, states, `GLOS_EV_NET` asked for per connection), `GLOS_FN_RANDOM` and `GLOS_FN_TIME` (UTC,
  how the clock was set, the zone's offset; added for step C's certificate checks). The SDK has BSD sockets on
  them (`glos/net.h`, `include/glos/posix`). As built: supervisor.md §17.11.
  - `make loopa-netapp` passes on bf6 and the 486dx2, NETTEST against an echo server on the host:
    - names looked up (a dotted address, an unknown name, pool.ntp.org) and connections refused and timed out;
    - 1 MB echoed byte for byte (1.5 MB/s on bf6, 0.78 MB/s on the 486dx2: Loop A's figures, not silicon's);
    - eight connections at once and a ninth refused, the far end's close, events, and the BSD layer;
    - an app killed holding four connections: the host sees them close, and `glos net` shows `sockets=0`.
  - loopa-net, -ssh, -apps and -console pass with it.
  - Found by NETTEST: DJGPP's `fcntl` passes a file-system extension only the descriptor, so O_NONBLOCK on a
    socket never arrived. `glos/net.h` maps `fcntl` to `glos_fcntl`.
- **Step C done** (2026-10-18): BearSSL 0.6 for DJGPP (`build/sdk/libbearssl.a`, no SSE2, AES-NI or RDRAND), the
  TLS layer (`sdk/tls`, `glos/tls.h`), HTTP GETs (`sdk/http`, `glos/http.h`) and FETCH (`apps/fetch`).
  - `C:\GLOS\CERTS.DAT` holds 121 anchors (80 RSA, 41 EC) in 52 KB. `tools/certs/mkcerts` makes it on the build
    host from curl's cacert-2026-09-25.pem, with BearSSL's own decoder, and checks it read back.
  - Certificates are checked against GLOS_FN_TIME's clock, and HTTPS is refused ("clock not set") until it has
    been set.
  - **The gate passes:** `make loopa-https` on bf6 and the 486dx2. The local cases use HTTPS servers on the host
    with a test CA added to the guest's anchors:
    - the clock not set, refused;
    - a 256 KB file (2.0 s on bf6, 7.3 s on the 486dx2), absolute and relative redirects, and a chunked body,
      each with the host's SHA-256;
    - refused, each with its reason: a name not the certificate's, an expired certificate, a CA nobody trusts, a
      404, an unknown name.
    - With the internet, files from raw.githubusercontent.com (ChaCha20-Poly1305) and the ibiblio FreeDOS
      mirror, checked against Mozilla's roots, with the host's SHA-256.
- **Step D done** (2026-10-18): the SSH and SFTP client.
  - `sdk/sshc` holds the protocol core (`glos/sshc.h`: bytes in and out, as the kernel's server is), keys and known
    hosts, and SFTP 3 (`glos/sftpc.h`, 8 requests in flight). It uses TinySSH's crypto and the kernel's wire
    encoding.
  - The terminal has an xterm screen (`apps/term/vt.c`) and `ssh`, `sftp` and `ssh-keygen` (`apps/term/ssh.c`).
    Keys and `KNOWNHST.TXT` are in `SSH\` beside TERM.EXE.
  - `make sshc-test` passes against OpenSSH 9.6's sshd on the host:
    - a login by key, a command's output, error and status, a pty's size, two channels at once;
    - 1 MB each way, an unknown key refused, a host key refused, known hosts;
    - SFTP put, get, ls, stat, rename, mkdir, rmdir and remove;
    - a key written that OpenSSH reads.
  - `vt_test` (in host-test) checks each family of sequences against xterm's screens.
  - `make loopa-sshc` passes on bf6 (G200, 16 bits) and the 486dx2 (VBE, 8 bits) against Debian's OpenSSH in a
    container (`tests/sshd`):
    - a new host key asked about and kept, a command's output and its status as `%ERRORLEVEL%`;
    - a password login after a wrong one;
    - vi editing a file (read back in the container) and top's screen;
    - a changed host key refused;
    - SFTP of 1 MB with SHA-256 checked on both sides (3.0 s up and 3.4 s down on bf6, 6.5 s and 7.7 s on the
      486dx2);
    - ssh-keygen's key read by OpenSSH.
  - The terminal's own scripts pass at every depth.
- **Step E done** (2026-10-18): packages.
  - `PKG.EXE` (`apps/pkg`) is a command (SOURCES, LIST, INSTALL, REMOVE, UPGRADE) and, without arguments, a
    window.
  - `tools/pkg/pkg.py` makes keys, packages and signed indexes on the host.
  - A GLOS source's `INDEX.SIG` is the Ed25519 signature over `INDEX.TXT`'s bytes, by a key in `KEYS\PKG\*.PUB`.
    Each zip's size and SHA-256 must be the index's, and its names DOS's own (8.3, relative). Every entry is
    inflated (stb_image's inflater) and CRC-checked before anything is written.
  - `PKG\NAME.LST` lists what a package put where, and its profile's keys, and removal takes exactly that away.
  - The kernel's unset now drops a section left empty, with the blank line before it, so GLOS.CFG is byte for
    byte as it was.
  - FreeDOS's repositories (`index.lst`, FD-REPOv1) are a second kind of source: unsigned, trusted through HTTPS
    and their CRC-32, and said so. Their packages go under DOSDIR, without SOURCE\.
  - **The gate passes:** `make loopa-pkg` on bf6 and the 486dx2. A source on the host over HTTPS (the test CA),
    signed with the suite's test key:
    - NOTES listed and installed: its files byte for byte, its profile with a launcher entry, the program run;
    - upgraded to 1.1;
    - removed: files and folders gone, GLOS.CFG byte for byte as before;
    - refused: a zip changed after signing, an index changed after signing;
    - FreeDOS 1.4's base listed (57 packages), CHOICE installed under the profile's DOSDIR and removed;
    - the window installing and removing NOTES.
- **Step F done** (2026-10-18): VNC and capture. As built: supervisor.md §17.12.
  - The VNC server (`kernel/wm/vnc.c`) speaks RFB 3.8 without a password of its own: SSH is the way in. A viewer
    reaches it through a `direct-tcpip` channel to 127.0.0.1:5900 (`ssh -L`) while `[vnc] enable` is on (the
    Control Panel's "Remote Desktop" page). It takes up to four viewers.
  - Updates come from the desktop's own damage, before each compose. Encodings: Raw, Hextile, and CopyRect for a
    window moved where nothing covers it and its old and new places don't overlap. The pixel format is whatever
    the viewer asks for. The cursor is drawn in as `glos shot` draws it, and an 8-bit screen's 6-bit DAC is
    widened as `glos shot` widens it, so a viewer's picture and a shot agree.
  - Keys (X keysyms, US layout) wait in one queue for the keyboard to have room, so a viewer that sends and leaves
    still delivers them. The pointer is absolute. While a viewer is connected, the task bar says VNC (D35).
  - `glos rec [--fps N] [--seconds S]` streams the desktop's frames while they change: each one a line of its
    time and length, then the PNG `glos shot` would give at that moment. `tools/rec.py` writes `NNNNN.png` and an
    ffmpeg `index.txt`.
  - **The gate passes:** `make loopa-vnc`, bf6 with the G200 at 16 bits, the G400 at 32 and the VBE card at 8. A
    stock client, vncdotool 1.2.0 (`tests/vnc`, in a container), works through `ssh -L`:
    - its screen equals `glos shot`'s on the desktop, with Notepad open, after typing, and after Notepad closed;
    - text typed through it into Notepad is saved, and the file read back over SFTP has it;
    - Notepad's close box, pressed through it, closes the window.

    At 16 bits, `glos rec` runs meanwhile, and each of the four shots is one of its frames, byte for byte. It
    passed twice running after the fixes below.
  - Found on the way:
    - `glos rec` lost a frame when a `glos shot` held the capture; now the frame stays owed until the capture
      is free.
    - Keys were lost when vncdotool closed straight after sending them; the shared queue fixed that.
