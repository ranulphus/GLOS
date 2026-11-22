# Phase F, as built: the new apps

The route (`docs/v1-route.md`, F) and each app's plan (`docs/v1/apps/`) say what is wanted; this says what was
built, in the order it was built, and what is left. Phase F began on 2026-11-20, when phase E's list was done
(`phase-e.md`); its order is the route's: the audio apps first.

## The Mixer (2026-11-20)

Plan: `docs/v1/apps/mixer.md`. `apps/mixer/mixer.c`, MIXER.EXE: one window of strips, each a name, a slider
upright, a balance and a mute.

- **Volume:** GLOS's master, the same number the task bar's volume icon has, with the output's level beside it
  (a meter of two channels).
- **Sound card:** the chip's own channels where it has them (its master, wave, FM, CD, line, microphone: the
  SB16's mixer or the Crystal chip's codec, as AUDIO's CHANNELS lists them), each with a balance (the chip's
  left and right levels, shown as one volume and how far it leans), and the input that is recorded.
- **Programs:** a strip for every stream that plays, under its program's name, with its level; strips come and
  go as programs begin and stop playing, and the window is as wide as they need.
- **Set at once, and shown when another sets it.** What is moved here is written to the mixer as it moves. Ten
  times a second the mixer is read: a slider is moved from outside only when GLOS's number is another than the
  one read last. (The chip takes its levels in steps: what is read back is not always what was written, and a
  slider that followed every reading would creep under the hand that moves it.)
- **The volume icon's double-click opens it** (`kernel/wm/desktray.c`; the Control Panel's Sound page until
  now). It is in the Start menu with GLOS's other programs, and not among the desktop's icons: those are for
  what is used every day (`apps/files/files.c`: the programs' list says which).
- **Tests.**
  - `tests/gui/mixer.txt`, host and Loop A, on the test's own numbers (`MIXER -demo`: a card that is always
    there, streams that come and go at F9; the pictures are the same on every PC): no stream, one, two, the
    master by the keyboard, a stream's and a channel's mute and the input by the mouse, the first stream gone.
  - The audio suite (`audio_case`), the real Mixer on a tone that plays: its window opens; the master to
    nothing (Home) silences the output while the stream's own level stays; the stream's mute silences it;
    its volume ten steps down gives an output of that share. `AUDTEST levels` (new) reads the mixer back; the
    Mixer's own lines say what it set; the tone ends with no underrun.
- **Not yet:** each program's icon on its strip; the level of what is being recorded; a button for it on the
  Control Panel's Sound page; the chip's FM level against FMTEST's notes in the suite; the bench (by hand,
  with the owner).

## The Sound Recorder (2026-11-20): the first part

Plan: `docs/v1/apps/sound-recorder.md`. `apps/recorder/recorder.c`, RECORDER.EXE, on `apps/shared/wave.c` (sound
as samples in memory, its edits, WAV files), which the Player will read WAV files with too.

- **A take is recorded** from AUDIO's stream in (the input the Mixer has chosen) onto the take's end, at the
  take's rate and channels (a new take's: Format's, 8000 to 44100 Hz, one channel or two; 16 bits always). The
  input's level is beside the waveform while it records; "Clipped" stays in the status bar once a sample was at
  the top.
- **The waveform** shows each channel's lowest and highest sample for every column of pixels; a press sets the
  cursor and a drag selects, as do Left and Right (with Shift), Home and End; View zooms in on the cursor and
  out. Play (F5) plays the selection, or from the cursor to the end, through a stream out, a line going along
  with what is heard; Stop is F6 or Esc.
- **Edits,** each undone by Undo (one step, and Undo again does it again): Cut, Copy, Paste (in place of the
  selection), Delete, Trim to Selection; Effects on the selection, or on all of the take when nothing is
  selected: louder (a quarter more), softer, as loud as fits, fade in, fade out, backwards, silence; Format
  makes the take over as one channel or two, or at another rate.
- **Files:** WAV opened (PCM of 8, 16, 24 and 32 bits and IMA ADPCM, one or two channels; what a file's sizes
  say beyond its end is ignored) and saved as 16 bits, 8 bits or IMA ADPCM (Format says which; a quarter of
  the size). A take that was changed is asked about before it goes.
- **The take is in memory:** two bytes a sample, 5 MB a minute at 44100 Hz in stereo. A recording stops with a
  message when 24 MB are full or memory runs out. (`wave.c` has a WAV writer that takes samples as they come:
  for recordings longer than memory, not used by the window yet.)
- **`wave.c`'s arithmetic is whole numbers throughout,** so that the host's test holds every edit to the
  sample (`tests/host/wave_test.c`: ramps and steady levels whose results are known in closed form) and the
  same test passes on any compiler. `tests/host/wave_cases.py` writes the WAV files it must read (each kind of
  PCM, extra chunks, the "extensible" header, sizes that lie, a file cut short, IMA ADPCM from another packer
  than `wave.c`'s) and the files that must be refused, and reads back what it wrote with Python's own `wave`
  module and its own ADPCM unpacker. ffmpeg plays the ADPCM files `wave.c` writes (looked at once: it rounds a
  step its own way, and its samples are within half a hundredth of full scale of ours).
- **Tests.** `tests/gui/recorder.txt`, host and Loop A, on the test's own sound (`RECORDER -demo`: each Record
  puts a second of it on the take; no card is asked for): recorded, made as loud as fits, a piece selected by
  the keyboard, faded, cut and pasted, undone, a second second recorded, one channel, half the rate, saved, a
  new take, the file opened again and played. The audio suite: the real Recorder records some seconds from
  the card while a tone plays, saves, and the WAV fetched from the PC has the frames it said (118,528 on the
  Pentium's profile, 120,320 on the 486's). 86Box's SB16 records silence (the take's peak is 0): the sound in
  the samples is the bench's to hear.
- **Not yet:** takes longer than memory (recorded straight to a file, edited in pieces); the level while
  waiting; the timer (start and stop at a time); more steps of undo; the monitor; a scroll bar under the
  waveform; the waveform's columns kept (a long take is looked through for every picture).

## The Player (2026-11-20): the first part

Plan: `docs/v1/apps/audio-player.md`. `apps/player` (PLAYER.EXE): `player.c` the window, `engine.c` the playing;
`apps/shared/source.c` a sound file as samples had as they are wanted, `wave.c` and `mod.c` under it.

- **A list of files,** each with its title (the song's own name where the file has one), length and kind; Enter
  or a double click plays a row. The one playing has a mark; its title, kind and rate are above the list, with
  the time, a slider that shows where it is and goes there when moved, and the volume of the Player's own
  stream (the Mixer shows the same). When a file ends the next begins; Repeat begins the list again, Shuffle
  takes any other. Files are added one at a time or a folder's at once; a list is saved and opened as an M3U
  playlist (a path a line; a path with no drive is beside the playlist). Files opens WAV, MOD and M3U files
  with it, and gives sound files their icon.
- **WAV** (PCM of 8 to 32 bits, IMA ADPCM) is read piece by piece from its file (`wave_in_*`: the same reader
  `wave_load` is made of), so a file may be longer than memory.
- **MOD modules** (`apps/shared/mod.c`): ProTracker's format and its descendants of 1 to 32 channels ("M.K.",
  "xCHN", "xxCH"), played in ticks as the format's descriptions have it: notes by the Amiga's periods with fine
  tuning, the volume and its slides, slides of pitch (up, down, to a note), vibrato and tremolo with their
  waves, arpeggio, sample offset, position jump and pattern break, pattern loop and delay, note cut, delay and
  retrigger, speed and tempo. A side's two channels of four at their loudest are full scale, a quarter of each
  side in the other. Each sample is taken on the line between the two it falls between (`mod_smooth(0)`: the
  nearer, for a slow PC). A song that goes round for ever ends where it comes to a row it has played; its
  length is found by going through it without sound, and seeking does the same to the place asked for (the
  samples after a seek are those of playing to there: tested). Whole numbers throughout.
  - `tests/host/mod_test.c` on songs `mod_cases.py` writes: each note's pitch by its cycles counted, loudness
    and sides, lengths to the frame for speeds, tempos, jumps, breaks, loops and delays, slides' ends, the
    arpeggio's mean, fine tuning, eight channels, a sample that ends by itself, files cut short, and 400
    modules with bytes changed at random (no harm, under the sanitizers too).
  - ffmpeg's libopenmpt plays the same songs to the same pitches, the same loudness of a soft note against a
    full one (0.250) and the same share of a channel in the other side (3.00), looked at once on the host.
- **The playing is a thread's** (`engine.c`): every 15 ms it fills the stream's ring from the file, under a lock
  it shares with the window's calls, so that the music goes on while a menu is open or a dialog is up. A
  module is played at the mixer's own rate. A seek while it plays closes the stream and opens another (what was
  queued is of the old place). FILES, IMAGES and NOTEPAD already link with the C library's lock for threads;
  the Player is the fourth.
- **Tests.** `tests/gui/player.txt`, host and Loop A, with no card and no clock (`PLAYER -demo`: F9 is ten
  seconds heard): a module and a WAV added, a text named .WAV refused, the module played, paused and on, the
  WAV begun by itself at its end, the list through, a playlist saved (its bytes checked), cleared, a playlist
  opened. The audio suite: the real Player given a WAV of 1175 Hz and a module of 259 Hz plays one after the
  other, says so, and both tones are in 86Box's recording.
- **Not yet:** IT modules; VOC; CD audio; tags; drag and drop and sorting in the list; the small view; a
  visualisation and the pattern view; gapless playing; a folder's files added in the background. (MP3 and Ogg
  Vorbis, S3M and XM modules, and MIDI files came after: below.)

## Files: folders on other computers (2026-11-20)

The owner's choice of 2026-11-20 (`docs/v1-route.md`, F, "More of SSH"): a remote GLOS or Linux PC opened in
Files over SFTP, like a drive. `apps/shared/remote.c` (a connection whose calls wait for their answers),
`apps/files/site.c` (the connections Files keeps, addresses, a folder asked for) and its hooks in `folder.c`.

- **An address is a folder:** `sftp://user@computer/folder` (`:port` after the computer for another port; no
  folder: the login's own) typed into the browser's address bar, given to `FILES -browse`, or asked for by
  **Go's Connect to a Computer...** (as `ssh` is told: `user@computer`). Back, Forward and Up go along it as
  along any path; the address bar keeps the ones gone to.
- **The login is `ssh`'s own:** the same key (`SSH\ID_ED25` beside the programs) and the same known hosts
  (`KNOWNHST.TXT`), so a computer `ssh` has logged in to needs no question here, and the other way about. A
  computer met for the first time is asked about with its key's fingerprint and kept on a yes; one whose key
  has changed is refused. Where the key does not log in, the password is asked for (stars as it is typed), up
  to three times.
- **It is listed and worked on off the window's thread.** A folder asked for is connected to and listed by a
  background task, the status bar saying so meanwhile; its questions are put on the window's thread and the
  task waits for the answer. (The task has no progress dialog of its own: the first try had one, and it came
  up over the host's question a second into it and took the keyboard.) One connection is kept for each
  computer and login while Files runs, shared by its windows and jobs, one call at a time (a lock each). A
  connection the other side has ended is made again at the next folder asked for.
- **What can be done there:** a folder gone into; a file opened (fetched under a name DOS can have, beside
  FILES.EXE, and opened by its kind); Copy and a paste into a folder of this PC (a folder with what is in it;
  long names as DOS can have them, numbered where two would be alike); things of this PC pasted there (their
  names as they are); New Folder, Rename and Delete (asked about: it can't be had back). All of the copying
  and deleting are jobs, with the progress window and Cancel every job has.
- **What can't, yet:** Cut (a move), Find, properties, thumbnails, the tree (another computer's folders are
  not in it), from one computer straight to another, a clash asked about (a file of the same name there or
  here is written over), a file opened there kept in step with its fetched copy, the saved connections as
  places (they are the Terminal's session manager's, next).
- **Tests.** The sshc suite's new case (`filesnet_case`, both its machines), against OpenSSH in the suite's
  container: the browser started on an address; the new host asked about and kept; the key's login; the
  folder listed; a folder gone into, Back and Up; a file fetched (its bytes), everything fetched at once (a
  300 KB file by its SHA-256, a long name's DOS name, a folder with its file); a 150 KB file of this PC pasted
  there (SHA-256 in the container); a folder made, renamed and deleted there; then with no key, the password
  asked, a wrong one, the right one.

## SSH: tunnels, and the PC's own ports (2026-11-20)

The owner's choice of 2026-11-20 (`docs/v1-route.md`, F, "More of SSH"): port forwarding, as `ssh -L` has it.

- **`ssh -L [ADDRESS:]PORT:HOST:HOSTPORT`:** connections to PORT of this PC go through the session and come
  out of the other computer to HOST:HOSTPORT, a "direct-tcpip" channel each (`sshc_direct` in `sdk/sshc`). Up
  to four `-L`'s a session, six connections through them at a time (the client has eight channels). **`-N`:**
  no shell or command, the tunnels alone, until Ctrl+C.
- **Whose the port is.** As OpenSSH and PuTTY have it, a tunnel's port is this PC's programs' alone unless it
  is opened: it is listened on at 127.0.0.1. `-g` opens a session's tunnels to every computer that reaches
  this PC; `*:` before a port opens that one, `127.0.0.1:` keeps one to the PC under `-g`.
- **That needed the PC to reach itself: API 2.9.** GLOS's network had no 127.0.0.1, and a packet for the PC's
  own address went out of the card for nobody. lwIP's loopback is on now, and LISTEN has `GLOS_NETF_LOCAL`
  (`docs/v1/api-2.md`, NET; `docs/supervisor.md` §17.11). It is every program's gain: one that serves can be
  its own client (a game that hosts and joins on one PC), and two programs on a PC can talk, or find each
  other by a broadcast (which the PC's other datagram ports now hear too).
- **Carried how.** SSH.EXE's poll takes new connections and what each has to send, no further than 32 KB
  ahead of the far side; what comes back is written to the connection at once (where this PC's side is slow
  that waits, up to ten seconds, and the session's other traffic with it); either end's last byte is told to
  the other; a refusal on the far side closes the connection here. On a GLOS before 2.9 a tunnel that is not
  opened is refused with the reason: there it would be everyone's.
- **Not yet:** remote tunnels (`-R`), a SOCKS port (`-D`), tunnels kept with a saved connection (the session
  manager's, next).
- **Tests.** On the host, `sshc_test` against OpenSSH: a direct channel to the sshd's own port brings its
  banner and its answer to ours (written before the channel was open); one to a port with no listener is
  refused. On Loop A, the sshc suite's `sshprog_case` on both machines: through a tunnel opened with `*:` the
  host comes in (a port SLiRP forwards) and logs in to the container's sshd through the guest's session: a
  command, 200 KB each way by SHA-256, two logins at once; a tunnel to a port nothing listens on closes what
  comes to it; a tunnel of the PC's own is nobody's for the host, and a second SSH.EXE on the guest logs in
  through it at 127.0.0.1; Ctrl+C ends them. `APITEST net`: `net-own` and `net-local`.

## The clock: a lost answer (2026-11-20)

Gate 28 failed once on the time suite's question to pool.ntp.org, and passed when the suite was run again.
The suite had asked the pool twice within a second; but it showed a fault of GLOS's own: lwIP waited 15 s for
an answer and 15 s more before asking again, so a second question went out just as `glos time sync` gave up.
One lost answer failed the sync. The wait is 5 s now (the second question at 20 s), the suite's NTP server can
be told to lose an answer (`asked-again`), and the pool is asked once.

## The Terminal: saved sessions, as PuTTY has them (2026-11-20)

The owner's choice of 2026-11-20 (`docs/v1-route.md`, F, "More of SSH"; in his words: saved connections and
config like PuTTY). The Terminal's Connections (a name, a host, a user and a port) became **Sessions**.

- **A session** is a name for a computer to log in to and all that goes with it: the host, port and user; a
  key's file, where it is not SSH's own; a command to run there in place of a shell; a keep-alive (seconds of
  quiet before a packet that says nothing, for a connection that a router on the way would forget); a file
  its output is logged to; up to four tunnels; and the look of its tab: one of six colour schemes (the theme's, black on white, white on
  black, a green screen, an amber one, white on blue), the text's size (the theme's, small, normal, large)
  and the lines kept above the screen (100 to 20,000).
- **The dialog** (File's Sessions..., Ctrl+Shift+O): the saved sessions down the left with New and Remove;
  three pages on the right, Computer, Tunnels (a list, and a tunnel added from a port of this PC, where it
  goes, and whether other computers may use it) and Look; Open and Save under them. Choosing a session in the
  list fills the pages; Enter or a double click on it opens it. Open opens what the pages say, saved or not,
  as PuTTY does; what is wrong (a port that is none, a name with a space) is said and nothing is done.
- **Opening** makes a tab with the session's look and types the SSH command into its shell: `ssh NAME` for a
  session as it is saved, the whole command (`ssh -p ... -i ... -o ServerAliveInterval=... -L ... user@host
  command`) for one that is not. So the login, its questions and the tunnels are SSH.EXE's as ever, and
  nothing secret is the Terminal's. **`TERM -session NAME`** starts the Terminal on a session.
- **`ssh NAME`, `sftp NAME`** from any shell: a saved session's name in the host's place takes its computer,
  port, user, key, command, tunnels and keep-alive, each unless the command line says its own (`ssh -l root
  NAME`, `ssh NAME uptime`). The tab's title is the session's name. `ssh -o ServerAliveInterval=N` is new
  with it (OpenSSH's name for the keep-alive).
- **A session's log** (the first of PuTTY's other things the owner chose): a session's `log`, or
  `ssh -sessionlog FILE`. What the session writes is added to that file as text as it comes: the terminal's
  sequences (colours, the cursor's moves, titles) and the control characters are left out, a line ends as
  DOS ends one, and each run begins under a line that says what and when (`=== ssh office, 2026-11-20
  16:40:02 ===`). It is SSH.EXE's doing (`apps/shared/sesslog.c`), so `ssh NAME` from any shell logs, not
  the Terminal's tabs alone; what is typed is not logged, except as the far side echoes it. A log that can't
  be written is said, and the session goes on without one.
- **Files** asks for a computer the same way: Go's Connect to a Computer takes a saved session's name (its
  line drops the names down) as well as an address, and a session's own key logs in to its computer.
- **Kept where:** `SESSIONS.CFG` beside SSH's key and known hosts (`SSH\` beside the programs, or `GLOSSSH`),
  text, a section a session (`apps/shared/sessions.h` has its words). The Terminal writes it; SSH.EXE and
  Files read it. No passwords and no keys are in it: a key is its file's name. The connections of phase E's
  `TERM.CFG` are taken over the first time, where there are no sessions yet.
- **The toolkit** gained a terminal's own look: `gui_term_colours` (the text and the ground a program leaves
  as they are; a program's own colours stay) and `gui_term_font` (one of the three sizes; the grid is laid
  out again, and the program told its new size). `sshc_keepalive` in `sdk/sshc` sends SSH's ignore message.
- **In the Start menu** the saved sessions are the Terminal's tasks (its own page there: More, or Shift+F10
  on its row): each starts the Terminal on that session.
- **Not yet:** a look for the Terminal's own shell tabs.
- **Decided by the owner, 2026-11-20** (asked which of PuTTY's things beyond SSH the Terminal should have):
  all three offered. **A session's log to a file** (built: above); **serial sessions** (a COM port in a
  computer's place: speed, bits, flow control; it needs a call for an app to own a COM port, beside the
  kernel's own serial log and the cable link); **Telnet and raw TCP sessions** (old BBSes and gear without
  SSH; code page 437 and ANSI colours drawn right). Telnet next, then serial, each a step of its own.
- **Tests.** `sessions_test` on the host: names, a tunnel's words (19 bad ones refused), 32 sessions written
  and read back the same, a file written by hand with its faults, 300 files of rubbish that do no harm (366
  checks, under the sanitizers too). `tests/gui/term-sess.txt` (the host's player and Loop A's native
  suite): a session made with two tunnels and a look, saved, a wrong port told and put right, opened in a
  tab, removed. `terminal.txt`: the terminal widget in its own colours and the large text (its grid laid out
  again: 49 by 10 in the test's window). The sshc suite: `ssh NAME` and its command, the command line's own command in its place,
  `sftp NAME`, a session's tunnel reached from the host and its keep-alive packets seen
  (`sshprog_case`); the Terminal started on a session, its tab in the session's look, logged in
  (`sshc_case`); Files' Connect by a session's name, logged in with the session's own key where the folder
  has none (`filesnet_case`). `start-session.txt` (Loop A): a session saved in the Terminal is a task on the
  Terminal's page of the Start menu, and starts it. The log: `sesslog_test` on the host (what is kept of 17
  kinds of output, each also cut in two at every place; the file begun and added to; 2,000 pieces of
  rubbish), and on Loop A a session's two runs in its file and a login with a pty logged with no escape in it.

## The display's state, told a mode too early (2026-11-20)

Gate 29 failed two cases, the desktop's and the test card's pictures at 16 bits on the Matrox cards, with
nothing in its commits to do with either. An old race, found by it:

- A mode asked for (`glos display on`, the Control Panel) is made by the VM thread at its next turn, which
  takes seconds (the card's BIOS). From the asking on, `glos display` said **"display on"** with the new
  mode's numbers: the tests waited for that line and took their pictures. As a rule the VM thread had begun
  by then (the line said "off" meanwhile); in gate 29 it was a second late (it was still starting the
  console), and the pictures were of the mode before.
- Worse, a second mode asked for while the first was being made changed the first's numbers under the VM
  thread's hands: the log's `mode=0 640x480x32 pitch=1280 rgb=5.6.5` (a 16-bit mode told as 32).
- **Now** `glos display` says **"display changing"** from the asking until the mode is on the screen, and a
  mode asked for while another is being made waits for that one's end (`display.c`: `turning`, `next`).
- **Tests** (the display suite, every card): the answer right after an asking is "display changing" (or the
  mode asked for, with its real number, where the card was quicker than the question); a mode asked for a
  second after another ends as the mode on the screen, and no line of the log says `mode=0`.

## Telnet and raw sessions (2026-11-20)

The second of PuTTY's other things the owner chose: for what has no SSH (old systems, bulletin boards,
switches and their like). Nothing such a session carries is secret from the network, and it says so nowhere
but here: it is what Telnet is.

- **TELNET.EXE** (`telnet [-r] [-c PAGE] [-sessionlog FILE] HOST [PORT]`): a program at a terminal as SSH.EXE
  is, on the same terminal code (`apps/ssh/cli.c`) with `telnetc.c` in the SSH client's place. Telnet's own
  words among the bytes are `apps/shared/telnet.c`: it agrees to the far side's echo, to go-aheads left out
  and to binary both ways, tells the terminal's type and its size (again when the window changes), and
  refuses everything else once. To a Telnet server's own port (23) it says what it has first; to any other
  port it speaks Telnet only when spoken to, so a plain service on a port is not sent Telnet's bytes.
- **Every key, or a line.** Where the far side echoes (most do) every key goes at once and Ctrl+C is the far
  side's; where it does not, a line is typed here, shown here and sent at Enter, and Ctrl+C ends the
  session. **Ctrl+]** ends it at any time.
- **`-r`, raw:** a TCP connection and nothing else, a line at a time.
- **`-c PAGE`, a bulletin board's characters:** the far side's bytes are a DOS code page's (437, 850, 852,
  866, 737: those GLOS has); they are shown as they should be (the box and block characters are in the
  terminal's font), and what is typed is sent in that page. The terminal then calls itself ANSI, which is what
  boards ask for; the colours and the cursor's moves are the ones the terminal's screen has always known.
- **Saved sessions have a kind** (the Terminal's Sessions: SSH, Telnet, Raw) and their characters (the Look
  page); `telnet NAME` opens one, its log kept as an SSH session's is; `ssh NAME` on one says whose it is.
  The Start menu lists them with the rest; Files' Connect lists the SSH ones, which have folders.
- **Found by its test: what is typed to a terminal program was not UTF-8.** The shell's session turned typed
  characters into the PC's code page for every reader, its own line editor and a program at the terminal
  alike, though SYS's TERMINAL says a program has them as the terminal sent them. An e acute typed to
  SSH.EXE went to the far side as 82h. A terminal program now has the bytes as they are (`shsess.c`), and
  SFTP, which had local file names right by that accident, turns what is typed for this PC's files into
  DOS's code page itself.
- **Tests.** `telnet_test` on the host (3,110 checks: a server's opening answered, cut in two at every place;
  the type and size told; text with Telnet's words among it; what is typed as Telnet carries it; an offer
  taken and refused; rubbish). Loop A, the sshc suite's `telnet_case` on both machines, against two servers
  of the suite's own: a board's picture in code page 437, its type and size, keys echoed by the far side, an
  e acute sent as 82h, Ctrl+]; the saved session and its log; a raw session and a Telnet one with no echo, a
  line at a time; a port nobody listens on. `sshprog_case`: an e acute typed to an SSH session arrives as
  UTF-8.

## Serial sessions, and a COM port for an app (API 2.10, 2026-11-20)

The third of PuTTY's other things the owner chose: a COM port in a computer's place, for what is at the end of
a serial cable (a router's or a switch's console, a modem, another PC).

- **SERIAL.EXE** (`serial [-f none|rts|xon] [-c PAGE] [-sessionlog FILE] COMn [SPEED] [FRAME]`): TELNET.EXE's
  source built again (`apps/ssh/telnetc.c`, `CLI_WHICH` 4) with a COM port for its connection. SPEED is 50 to
  115200 (115200 if none), FRAME the bits, the parity and the stop bits as `8N1` (which it is if none), `-f`
  the flow control: the RTS and CTS wires, or XON and XOFF. Every key goes down the line at once and nothing is
  shown that the far side does not send back; Ctrl+] ends the session; `-c` and `-sessionlog` as Telnet's.
- **API 2.10: NET's SERIAL** (`glos_net_serial`). A COM port opened as a connection: SEND and RECEIVE are its
  bytes, CLOSE its end (what waits goes out first), its events and its WAITING and ROOM a connection's; its
  wires are the LINES property (DTR and RTS set, CTS, DSR, DCD and RI read), BREAK holds the line at a
  break. No network is needed. One line for all apps at a time; a port the cable link has is refused.
- **In the kernel.** `kernel/drv/uart.c` was one line, the cable link's; it is two now, the second an app's
  (4 KB rings of its own), with a frame, any speed the chip's divisor comes within a hundredth of, and flow
  control done in the interrupt: RTS dropped (or XOFF sent) when three quarters of the ring wait, raised
  again at a quarter; nothing sent while CTS is off (or after an XOFF) but the XON or XOFF this side owes; XON
  and XOFF are no text then. Two lines on one interrupt (COM1 and COM3) share it. The port is hidden from DOS
  programs while it is open and as DOS left it afterwards; COM1 carries none of GLOS's log meanwhile, as for
  the cable link. `kernel/net/sock.c` has a serial flavour of a connection: the net thread moves bytes between
  the driver's rings and the app's on its turns (the driver kicks it when bytes come and when there is room).
- **Saved sessions:** the kind Serial, its host the port (COM1 to COM4), and a Serial page in the Terminal's
  dialog (speed, data bits, parity, stop bits, flow control); `serial NAME`; `ssh` and `telnet` given such a
  name say whose it is.
- **Found by its test: a page pasted to a terminal program lost its end.** What is typed to a program at a
  terminal waited in a ring of a kilobyte, and what found no room was dropped; 4,000 characters typed at once
  to SERIAL.EXE arrived as 1,024. The shell's session now takes no more from the terminal while that ring is
  full (the 4 KB before it then fills, and is as it was), and the Terminal app keeps what a tab's session has
  no room for and gives it later (a paste of up to 256 KB), where it was dropped too.
- **Not yet:** a break sent from the keyboard; the wires shown; more than one serial session at a time; a
  serial session for a full-screen DOS program (they have the port itself, as ever).
- **Tests.** Loop A, the sshc suite's `serial_case` on both machines, the guest's COM2 in the host's hands
  through the emulator's bridge: the port opened (the kernel's line says how), both ways a key at a time,
  4,000 bytes each way whole, a second session refused, COM4 not there, a frame and a speed that are none,
  7E2 set, the port DOS's again; the saved session with XON and XOFF (an XOFF holds what is typed until the
  XON; neither is text) and its log. The cable link's cases (the ssh suite) run on the driver's first line as
  before. Real hardware is owed: a bench's COM port against the host's.

## The Player: MP3 and Ogg Vorbis (2026-11-20)

The route's next of the Player's kinds (`docs/v1/apps/audio-player.md`): today's packed music, by decoders that
are others' work, unchanged (`THIRD_PARTY.md`): **minimp3** (CC0: MPEG audio layers I to III) and
**stb_vorbis** (MIT or public domain).

- **`apps/shared/packed.c`** stands between them and `source.c`: a file is unpacked as it is played, a few
  frames of it in memory, never the whole. An MP3's old and new tags are stepped over (the old one gives the
  title), its first frame is looked for in its first 128 KB, its length is the count in its Xing header or, with
  none, reckoned from its size (frames of one size); a seek goes to the place as far through the file as the
  time is through the sound, and plays on from the first frame found there. An Ogg Vorbis file's length and
  seeks are exact; its TITLE comment is the title; more than two channels are mixed down. Each decoder is a
  file of its own to the compiler (`packedv.c` is stb_vorbis's): their names meet otherwise.
- **The kind is the file's bytes**, as for the rest: `OggS`, an ID3 tag or a frame's first bytes; what begins
  as neither and is no module is still tried as an MP3.
- **A Pentium's work.** They do their sums in floating point. The feeding thread now does a tenth of a
  second's work at most in a turn (the window's calls never wait longer), and where the stream has run dry at
  twenty of its looks in a row it stops the file and the Player says that this PC can't unpack it as fast as it
  plays: said, rather than broken sound. So a 486 is told, and a mono file at a low rate that it can manage
  plays. The thread's stack is a quarter of a megabyte (the unpackers' tables).
- **Files** opens MP3, MP2 and OGG files with the Player; the Player's dialog and its folders' listing know
  them.
- **Not yet:** the tags' other fields (artist, album); a seek table for files of varying frames; gapless ends.
- **Tests.** `packed_test` on the host, with six small files made by ffmpeg and kept (`tests/host/sndfix`):
  what each says of itself (kind, rate, channels, length, title), its sound from end to end (each channel's
  pitch by its zero crossings), a seek to the middle and back to the start; and 300 damaged copies of each
  (bytes changed, cut short, a piece overwritten), opened or refused and played to their end, under the
  sanitizers on the host. Loop A's audio suite, on the Pentium: an MP3 and an Ogg file after the WAV and the
  module in the Player, their two tones in the recording.

## The Player: S3M and XM modules (2026-11-20)

The route's next of the Player's kinds: the two module formats of the PC's own trackers, by players written
here from the formats' public descriptions (Scream Tracker 3's TECH.DOC, FastTracker 2's XM.TXT, and what is
told of how the two trackers play; no code of another player's). Whole numbers throughout, as `mod.c`: a 486's
work.

- **`apps/shared/trkmix.h`**: what the two share, a tracker's voices mixed into samples. A voice's place in its
  sample is a whole part and a 32-bit fraction; 8 and 16 bits, signed or not; loops forwards, and back and
  forth (XM's: the place runs on past the loop's end for the way back, so one sum serves both); each sample on
  the line between the two it falls between, or the nearer one. Four channels at their loudest are the top;
  songs of more are mixed at 2 / the root of their number (they are seldom loud together), and what still goes
  over is held at the top. Most frames of a voice are made in runs that need no look at the sample's end or
  loop (as many as surely lie before it), with sums of 32 bits and a 16-bit sample read in one load.
  - **Measured on bench-g400** (a Pentium II Deschutes; `PLAYER -time`, under GLOS, 2026-11-20), two songs of a
    minute with all 16 channels sounding: the first, frame-by-frame version took 11% of the PC's time at
    22050 Hz and 22% at 44100 Hz (S3M 7.03 s and 14.01 s for 61.4 s of sound; XM 7.25 s and 14.39 s); this
    one 2.6% and 5.3% (S3M 1.59 s and 3.24 s; XM 1.42 s and 2.80 s): 4.4 to 5.1 times less. For comparison
    there, a minute of MP3 (128 kbit/s, stereo, 44100 Hz) takes 3.18 s and of Ogg Vorbis 2.96 s. A 486's own
    figures are owed from bench C.
- **S3M** (`apps/shared/s3m.c`): up to 32 channels of samples (the AdLib's channels and instruments are passed
  over), the file's own places for them or left and right as Scream Tracker has them, a song that is not stereo
  in the middle; the orders with their markers; samples of 8 or 16 bits, signed (the old way) or not, each with
  its own rate for a middle C. The commands A to X: speed and tempo, jumps, breaks and the pattern loop (one for
  the song, the next beginning after the last, as Scream Tracker has it), the row gone through again, volume
  slides (on the first tick too for the first Scream Tracker 3's files), slides in pitch with the fine and
  extra fine ones, the slide to a note, vibrato and the fine vibrato, tremor, arpeggio, the sample's offset,
  the note struck again, tremolo, the note cut and put off, tunings, the song's volume, places.
- **XM** (`apps/shared/xm.c`): up to 32 channels, patterns of 1 to 256 rows, both kinds of period (the linear
  ones, 64 to the semitone, from a table of 2^(i/768) reckoned in whole numbers when the file is opened; and
  the Amiga's), instruments of several samples chosen by the note, each with its volume, place, tuning and
  note of its own; the volume and place envelopes with their held point and loop; the fade when the key is
  let go (and the stop at once where there is no envelope); the instrument's own vibrato. A note with no
  instrument keeps the volume and the envelopes where they are; an instrument with no note begins them again.
  The commands 0 to F, the E's, G, H, K, L, P, R, T and X, and the volume column's. Its samples are stored as
  differences: they are made whole where they lie when the file is opened (no second copy of the file), after
  everything else about it has been found sound.
- **Both** end a song that would go round for ever where it comes to a row it has played before (a pattern
  loop's rows apart); tell a song's length by going through it with no voices (a row's place in its pattern is
  kept from the row before: the packed patterns are not gone through from their start each row); and seek by
  playing to there silently, which leaves every voice as playing would (a test holds them to it).
- **The kind is the file's bytes**: `Extended Module: ` at the start, `SCRM` at 2Ch. An XM of before
  FastTracker 2's last form (1.04) is said to be packed in a way that isn't played here.
- **Files** opens S3M and XM files with the Player; the Player's dialog and its folders' listing know them.
- **Not yet:** IT modules; an S3M's AdLib channels; XM's glissando; the pattern view.
- **`PLAYER -time [-rate N] FILE ...`** (no window, no card, and under plain DOS too): each file's whole sound
  made as fast as the PC can, and the time that took beside the time the sound lasts (`share=37%`: the PC's
  time the file needs while it plays). For the benches: what a real 486 has left with a 16-channel module at
  the mixer's 22050 Hz, or a Pentium with an MP3, is measured there, not reckoned from 86Box. Loop A's audio
  suite looks at the lines' shape alone.
- **Tests.** `s3m_test` and `xm_test` on the host, each with some thirty small songs written byte by byte
  (`tests/host/s3m_cases.py`, `xm_cases.py`) and held to what must be heard: every note's pitch (the cycles
  counted), how loud on which side, each command's effect on pitch, volume, place or the song's length, the
  envelopes' and the fade's levels tick by tick, seeking against playing sample for sample; files cut short;
  and 800 and 1000 copies with bytes changed at random, played, sought in and measured under the sanitizers.
  `packed_test` plays one of each through `source.c` (`tests/host/sndfix/modules.py` writes them). Loop A's
  audio suite: an S3M and an XM after the WAV and the MOD in the Player, on every PC of the suite, their two
  tones in the recording.

## The Player: MIDI files (2026-11-20)

The route's next: `.MID` files (and `.RMI`, the same in a RIFF wrapper) through AUDIO's MIDI port, which GLOS
has had since phase B (API 2.0, step 10): the card's FM chip played by the kernel's own General MIDI driver, or
an MPU-401 with whatever is plugged into it. The Player is that port's first real user.

- **`apps/shared/smf.c`** reads a Standard MIDI File of type 0 or 1: its tracks side by side, the earliest
  event next, so that the channel messages come out in the order they are to be sent, each with its time in
  milliseconds: the tempo changes are worked in as they are met (a tick is so many to the quarter note, or to
  a frame of film time). Running status; a note on with no force as a note off; system exclusive messages and
  the events that are for a reader passed over, but for the tempo, the first track's name (the song's title)
  and a track's end. A file cut short gives what there is of it. Type 2 (songs side by side) is said to be a
  kind that isn't played here.
- **The engine** (`apps/player/engine.c`) sends, at each look of its thread, the messages of the next 400 ms
  with their times on GLOS's clock; GLOS plays each at its time, whatever the Player is doing. The FM chip
  where there is one, else an MPU-401; where there is neither, or another program has the port, the Player
  says so. To the window a MIDI file is 1000 "frames" a second, so the time, the slider and the list's length
  are as for any other file; the last notes have 0.8 s to die away.
- **Pause, going on and seeking.** A pause drops what waits and ends the notes. Going on, or a seek, first
  tells the port how things stand at that place: for each channel every controller as General MIDI begins,
  then what the file had said up to there (its controllers, its instrument, its bend, and the bend's range
  where the file set it), and then the messages from there on. No note from before the place is struck late,
  and a seek backwards doesn't keep a later instrument.
- **The volume** of the Player is the force of the notes (a MIDI port has no stream to turn down); the Mixer
  has the FM chip's own level.
- **Files** opens MID and RMI files with the Player; the Player's dialog and its folders' listing know them. A
  file added to the list is asked what it is through the engine now (`eng_probe`), not played to find out.
- **Not yet:** a choice of the MIDI device in the Player (FM first, as built); system exclusive messages to
  an MPU-401; the software synthesiser with a SoundFont (a fast Pentium's); karaoke words.
- **Tests.** `smf_test` on the host: files written byte by byte (`tests/host/smf_cases.py`) held to every
  message and its time (two tempos, three tracks together in their order, film-time ticks, the wrapper, a file
  cut in a message), refusals, and 2000 copies with bytes changed at random (what comes out never goes back
  in time and is always a channel message), under the sanitizers too. `tests/gui/player-kinds.txt`, host and
  Loop A, with no card: a MIDI file, an S3M and an XM added and played through, a type 2 file refused. Loop
  A's audio suite: the real Player plays a MIDI file of one flute note through the card's FM chip after the
  modules, on every PC of the suite, and its 370 Hz is in the recording.

## MIDI Studio (2026-11-20): the first part

Plan: `docs/v1/apps/midi-studio.md`. `apps/studio/studio.c`, STUDIO.EXE, in the Start menu; on three pieces
shared with the Player:

- **`apps/shared/smf.c`**, the MIDI file reader, now also says what an editor needs: each message's tick, what
  a tick is, the tempo changes, the first time signature, the tracks' names.
- **`apps/shared/song.c`**: the song. Up to 32 tracks, each a name, a channel, an instrument, a volume and a
  place, whether it is muted or solo, its notes (start, length, key, force; kept in order) and whatever else
  its channel is sent (controllers, bends: kept as they were read); the tempo with its changes; the time
  signature; 480 ticks to the quarter note.
  - **Read from a MIDI file:** a track for each of the file's tracks' channels (a type 0 file comes apart by
    channel); a note on is held until its note off, the same key struck again, or the song's end; the first
    instrument, volume and place a channel is given before its first note are the track's own; the file's
    ticks are scaled.
  - **Written as one:** type 1, a first track with the title, the time signature and the tempo, then a track
    for each of the song's, with running status. Written "as heard" for playing: no muted track, and where a
    track is solo, those alone.
  - What is read, written and read again is the same song, and written again the same bytes: the test holds
    2000 damaged files to that.
- **`apps/shared/midiplay.c`**: a MIDI file's messages sent to AUDIO's MIDI port as their times come, taken
  out of the Player's engine, which uses it too: the port told how every channel stands at the place playing
  begins, the messages of the next 400 ms sent at each look.

The app:

- **The tracks** in a list: name (or "Track 3"), instrument (General MIDI's 128 names; "Drums" on channel
  10), channel, and Muted or Solo. New Track takes a channel no track has (never the drums'); New Drum Track
  is on channel 10; Remove Track; Instrument... (a list to choose from; Enter or a double click on a track
  opens it too); Mute and Solo.
- **The piano roll** (a toolkit canvas view: scroll bars, the wheel, the middle button) of the chosen track:
  time across, a line a beat and a darker one a bar; the keys down it, a black key's row shaded, each C named
  at the left of what shows. A click on nothing puts a note there, on the grid (sixteenths) and as long as
  the last note made; the drag that follows moves it. A click on a note chooses it (Ctrl: one more, or one
  fewer); a drag moves what is chosen in time (by the grid) and in pitch; a drag of a note's right end
  stretches the chosen notes. The right button takes the note under it out; Del the chosen ones; Ctrl+A
  chooses all. Zoom In and Out: 2 to 32 ticks a pixel.
- **The history**: every change to a track's notes is a step (the toolkit's undo: Ctrl+Z, Ctrl+Y, named in
  the menu): the track's notes before and after. A change to the tracks themselves (one added or removed)
  starts the history anew.
- **Playing** (F5, F6): the song as heard is written to memory as a MIDI file, read by the Player's reader
  and sent by `midiplay.c`, from a thread of the app's own (the music goes on under menus and dialogs); a
  line goes along the roll and "Bar and beat" counts. What is edited while it plays is heard at the next F5.
- **Files**: New, Open, Save, Save As (.MID); a changed song is asked about before it is left. A type 2 file
  is refused with its reason.
- **`STUDIO -demo`** (the GUI scripts'): no MIDI device is asked for and no time passes by itself: F9 is a
  second heard.
- **Not yet** (the plan's rest): the arrangement view (regions along bars); velocity and controller lanes;
  step entry from the PC's keys, a MIDI keyboard (MPU-401 in, USB MIDI) and recording; quantise, transpose;
  the tempo's changes and the time signature edited (those of a file are kept and played); loops; the
  software synthesiser and WAV export; the choice of device; mute and solo kept in the file; undo of track
  changes; other tracks' notes shown behind the chosen one's.
- **Tests.** `song_test` on the host (34 checks, under the sanitizers too): notes kept in order, the tempo's
  arithmetic, a built song written and read back the same and its messages at the right milliseconds by the
  Player's reader, muted and solo tracks as heard, `smf_cases.py`'s files as songs, and the 2000 damaged
  files' round trips. `tests/gui/studio.txt`, host and Loop A: notes put in, moved, stretched and removed by
  the mouse, undone and redone, a second track with an instrument and a note, muted, the song played to its
  end (the muted track's messages not sent), saved (116 bytes), a new song, the saved one opened with its
  two tracks and three notes, a type 2 file refused. Loop A's audio suite: the real program opens a MIDI
  file, F5 plays it through the card's FM chip, and its 294 Hz is in the recording, on every PC of the suite.

## The PDF Viewer (2026-11-20): the reader's objects

Plan: `docs/v1/apps/pdf-viewer.md`. The first of its steps, with no program yet: **`apps/shared/pdf.c`**, the
file's objects. The plan left the parser open (PDFio as the first candidate, or GLOS's own): it is GLOS's own,
written from the specification (ISO 32000-1): small (some 1300 lines), permissive where real files
are wrong, reading the file by pieces through the caller's function (never whole), and on the unpacker GLOS
has already (`loader/inflate.c`). PDFio was not tried; no code of another reader's was looked at.

- **Objects**: numbers (their digits taken as a whole number and divided once), names with their `#xx`, strings
  with their escapes and in hex, arrays, dictionaries, references; nesting to 64 deep. A numbered object is
  read when it is first asked for (a piece of the file at its place, a larger one if it doesn't end there)
  and kept; `pdf_trim` lets the kept ones go between pages.
- **Cross-reference tables**: from `startxref`, the newest first and each one's `Prev` after it (an update's
  newer objects stand); tables of text with their trailers, and 1.5's cross-reference streams (their `W`
  widths and `Index` runs), hybrid files' `XRefStm`; objects packed in object streams (the two last used kept
  unpacked).
- **Damaged files**: where the tables can't be read or lead to no catalog with pages, the file is gone through
  for `N G obj` at lines' starts and for a trailer; object streams found so give up their objects, and a file
  with no trailer of text takes its cross-reference stream's dictionary, or the catalog by its type.
- **Streams**: their bytes by `Length` (which may be another object) when the end is where that says, else up
  to `endstream`; unpacked through Flate, LZW (both early-change ways), ASCII85, ASCIIHex and RunLength, in
  chains, with PNG's and TIFF's predictors undone. A picture's own packing (a JPEG's `DCTDecode`) is left to
  the caller, who is told its name.
- **Pages**: counted by the tree's `Count`, found by going down the tree by the counts; what a page has by its
  parents (`Resources`, `MediaBox`, `CropBox`, `Rotate`).
- **Encrypted files** are refused as locked for now.
- **Tests.** `pdf_test` on the host (56 checks, under the sanitizers too) over files written byte by byte
  (`tests/host/pdf_cases.py`): words of every kind, two pages and their inherited boxes, an update, a file of
  1.5's kind (object stream, cross-reference stream with a predictor), sixteen streams packed every way, a
  page tree three deep, the tables lost in both kinds of file, a file cut in its last object, locked, and no
  PDF; and 2500 copies with bytes changed at random, every object, page and stream of those that open gone
  through.
- **The pages' drawing** (`apps/shared/pdfdraw.c`; the second step, without text yet): a page's content stream
  run word by word into a canvas of pixels, at any scale and from any corner of the page (a part at a time,
  for big zooms).
  - **Paths**: lines, curves (cut into straight pieces by their length on the screen), boxes; filled by the
    nonzero or the even-odd rule, with smoothed edges: each edge's exact share of every pixel it crosses is
    added into sums a row of pixels wide, a band of 64 rows at a time, and the running sum along a row is how
    much of each pixel is inside. A stroke is filled as its outline (a box a piece, round joins and caps where
    the line is thick enough to show them, square caps; never thinner than a pixel).
  - **State**: the matrix (`cm`), saved and restored (`q`, `Q`); colours as gray, RGB and CMYK; how much shows
    (`gs` with `ca`, `CA`); line width, caps, joins; clipping to the box around the clip's path.
  - **Pictures**: of 1 to 16 bits a sample, gray, RGB, CMYK, indexed (the table a string or a stream), ICC
    spaces by their number of components; stencils in the fill colour; a picture's own soft mask; JPEGs by a
    decoder the app gives (`pdf_draw_jpeg`); drawn through any matrix by asking the picture for each pixel
    under it. Forms (`Do` of a stream of words) with their own matrix and resources, twelve deep.
  - **Pages** by their CropBox or MediaBox, turned by Rotate.
  - **Not yet**: text; dashes; shadings and patterns (a flat gray); clipping to shapes other than boxes;
    pictures among the words (`BI`: passed over); blend modes.
  - **Tests.** `pdfdraw_test` on the host (23 checks, under the sanitizers too): a page with a shape of every
    kind at a known place (`pdf_cases.py`'s draw.pdf), its pixels looked at: box, line, triangle, a hole by
    the even-odd rule, a clip, a matrix, a disc of curves, three kinds of picture, half strength, CMYK, a
    form, a smoothed edge; the page turned a quarter and cropped; a part of it at three times the size; and
    1500 copies with bytes changed at random, their pages drawn at odd scales and corners.
- **Fonts, the first part** (the third step's ground): what the text will be drawn with.
  - **`apps/shared/ttf.c`**: GLOS's own TrueType reader: the character maps (formats 0, 4, 6, 12; Unicode,
    symbol and Macintosh), advances, and outlines (simple glyphs, and glyphs put together from others by
    their matrices, eight deep), given piece by piece to whoever draws them. The plan named stb_truetype; its
    own notes say it is not for fonts one cannot trust, and a PDF's embedded font is whatever its writer put
    there: so every offset and count here is checked against the table it lies in. Not used: stb_truetype.
  - **Stand-in fonts** (`apps/pdf/fonts`, 490 KB in twelve files): sans, serif and mono, each regular, bold,
    italic and bold italic, for PDFs that name Helvetica, Times or Courier (or anything else) and carry no
    font. Made by `tools/mkpdffonts.py` from the Liberation fonts (their widths are those three's): cut down
    to some 250 glyphs and seven tables, glyphs renumbered; SIL OFL 1.1 (`apps/pdf/fonts/OFL.txt`,
    `THIRD_PARTY.md`), under names of their own as that licence asks of changed fonts.
  - **Tests.** `ttf_test` on the host (89 checks, under the sanitizers too): each of the twelve opens; A, o,
    space and an accented e have the contours they should, every contour closed, every glyph drawn, the
    advances are Helvetica's, Times's and Courier's; and 3000 copies with bytes changed at random, every
    glyph of those that open drawn.
- **Text** (`apps/shared/pdffont.c`, and the text operators in `pdfdraw.c`).
  - **A PDF's font** opened from its dictionary: simple (codes of a byte) or composite (`Type0`: two bytes,
    the glyphs of the font it is made from, straight or by its `CIDToGIDMap`); its widths (`Widths` and
    `MissingWidth`; a composite font's `W` runs and `DW`); its characters: the three standard tables
    (Windows', the Macintosh's, Adobe's), changed by `Differences` through a table of some 230 glyph names
    (and `uniXXXX`), and `ToUnicode`'s `bfchar` and `bfrange` runs.
  - **Its outlines**: from the TrueType font the PDF carries (`FontFile2`, by `ttf.c`): a symbolic font's
    glyphs by its symbol or Macintosh map, the others by their characters; from a carried font of a
    PostScript kind (below); else from a stand-in, chosen by
    the font's name and flags (mono, serif or sans; bold; italic), the glyph of the code's character, made as
    wide as the PDF says where that is near the stand-in's own (0.6 to 1.6 of it). A standard font with no
    widths said (old PDFs) takes the stand-in's.
  - **The operators**: `BT ET Tf Td TD Tm T* Tj TJ ' "`, the spacings `Tc Tw`, `Tz`, `TL`, `Ts`, and `Tr`
    (filled, stroked, both, or unseen: a scanned page's hidden text). Each glyph's outline goes through the
    text matrix, the size and the page's matrix into the same filler as every path, its curves cut by their
    length on the screen; glyphs out of the canvas's sight are not made.
  - **Not yet**: Type 3 fonts, the Symbol font, vertical writing, CJK encodings other than by glyph numbers,
    a cache of glyphs (each is made anew every time).
  - **Tests.** `pdfdraw_test` (34 checks now): a page of text at twice the size: the stand-in sans, serif
    (with `TJ`'s kerning), mono (with `Tc` and `Tw`: a line's length to the pixel by its font's advances),
    bold with accents and the euro, italic by `T*`, a TrueType font the file carries, one by glyph numbers
    (`Identity-H`), text turned a quarter, unseen text (no ink), small blue text; the damaged-file runs take
    the text page too.
- **The program** (`apps/pdf/pdf.c`, PDF.EXE, "PDF Viewer" in the Start menu; Files opens PDF files with it):
  its first part.
  - A page at a time in a toolkit image view (scroll bars, the wheel, the arrows; the left button moves the
    page): the page is drawn whole into a picture of its own at the zoom, at most three million pixels (the
    zoom stops there).
  - **Pages**: Next and Previous (Ctrl+PgDn, Ctrl+PgUp), First and Last (Ctrl+Home, Ctrl+End), Go to Page...
    (Ctrl+G). **Zoom**: In and Out by steps from 25% to 400%, Actual Size (a point a pixel), Fit Width (as a
    file opens: the page as wide as the view).
  - The file is read through `fseek` and `fread` as its objects are wanted; what a page needed is let go
    (above 2 MB) when the next is drawn. JPEGs in a page are decoded by `apps/shared/picfile.c`.
  - **The stand-in fonts** are one file beside the program, `PDFFONTS.DAT` (487 KB; made at build time from
    `apps/pdf/fonts` by `tools/mkpdffonts.py --pack`; a face is read from it when first wanted), found by
    the toolkit's `gui_open_beside`, now a public call. The installer and the suites put it there.
  - A locked file, a file that is no PDF, a damaged one and one that can't be read are each refused with
    their reason.
  - **Not yet** (the plan's rest): drawing off the window's thread and a cache of pages; continuous and
    two-page views; search, selecting and copying text; the outline and thumbnails; links; encrypted files
    with an empty password; a part of a page at zooms beyond the picture's limit.
  - **Tests.** On `tests/gui/mkpdf.py`'s files: nothing open, a two-page PDF opened (its first page fitted
    to the view: title, text in three stand-in faces, shapes), the next page, the actual size, Go to Page,
    Fit Width again, a locked file and a WAV refused; each drawn page is said with its size and zoom.
    `tests/gui/pdf.txt` on the host, with pictures to look at; `tests/gui/pdf-run.txt` the same on Loop A
    with the real program, without pictures and not on the 486 (`#pentium`, a new mark the native suite
    heeds: the PDF Viewer's floor is a Pentium, and a page takes the emulated 486 longer than a script
    waits).
  - **Why Loop A's pictures are not held to the host's here**, as every other program's are: the drawing's
    sums are floating point, and the two compilers (the host's gcc, DJGPP) round a shade's last step
    differently (a first run differed in thousands of pixels by one step of 255; 0.7 of 255 is 178.5, and
    came out 178 in one and 179 in the other: that one is settled now, the smoothed edges are not). The
    pages look the same. A filler in whole numbers would make them the same to the bit, and is noted as
    work to come, not needed for reading.
- **Embedded fonts of the PostScript kinds** (`apps/shared/psfont.c`, some 1100 lines; papers made by TeX
  and most made by publishing programs carry these, and until now a stand-in drew them: right letters and
  places, not their own shapes, and a font of symbols wrong).
  - **CFF** (`FontFile3`: `Type1C`, `CIDFontType0C`, or the `CFF ` table of an `OpenType` font): GLOS's own
    reader from Adobe's Technical Notes 5176 and 5177: the tables and dictionaries, the glyphs' names (the
    391 standard strings by their characters, the font's own by `pdffont.c`'s table of names), the font's own
    encoding (and the standard one), a CID-keyed font's numbers and its several private dictionaries; and the
    glyphs' programs (Type 2) run straight into the filler: every move, line and curve operator, the four
    flexes, hints and their masks passed over, subroutines of both kinds ten deep, the width that may come
    first.
  - **Type 1** (`FontFile`): from "Adobe Type 1 Font Format": the clear part (matrix, encoding), the part
    after `eexec` decrypted (binary or hex), the subroutines and glyphs by name decrypted again (`lenIV`);
    the programs run: `hsbw` and `sbw`, the lines and curves, `div`, flex by its calls out of the program, a
    letter and its accent put together (`seac`), hints passed over.
  - **Which glyph a code is** (`pdffont.c`): by the name the `Differences` give the code, else by the
    character a named table gives it, else (no table named) by the font's own encoding; a composite font's
    codes are the CFF's own numbers (CIDs), or the glyphs' numbers where the CFF is not CID-keyed. A font
    nothing can be made of is drawn by a stand-in, as before.
  - Curves with two control points go to the filler as they are (`cubic`, new in the sink that `ttf.c`'s
    outlines use; the PDF's own curves went that way already).
  - Every count, offset and stack depth is checked: the font is whatever the PDF's writer put there.
  - **Tests.** `psfont_test` on the host (21 checks, under the sanitizers too) on two made-up fonts
    (`tests/host/psfont_cases.py` writes them byte by byte: a CFF and a Type 1, plain and in hex) that between
    them use every operator read: glyphs found by name, character, code; each outline's pieces where they
    should be; 4000 copies with bytes changed at random, every glyph of those that open drawn. `pdfdraw_test`
    (44 checks now) draws a page that carries them each way a PDF can (a Type 1 by its own encoding, a CFF by
    `Differences`' names, in a composite font by glyph numbers, within an OpenType font by a named table) and
    one nothing can be made of: each letter in its place and of its shape (a triangle's pixels counted, a
    hole a hole); its damaged-file runs take that page too, the fonts' bytes among those changed.
  - **Found by the 32-bit run**: a negative number written in five bytes came out wrong by 2^32 where a
    `long` is 32 bits (as it is under DOS): the 64-bit run under the sanitizers could not see it. Fixed; the
    made-up Type 1 font writes its numbers that way.
  - **Not yet**: a glyph's width from the font itself (the PDF's `Widths` are used, as they must be there);
    hints (at reading sizes on a screen the smoothing carries it); CFF2 and multiple masters.
- **The pages' text: Find and Copy.**
  - **`pdf_text`** (`pdfdraw.c`): a page gone through as for drawing, nothing drawn: each letter noted with
    its character, where it begins on its baseline, its step along the line and its size, on the page as it is
    shown (turned as the page is). A page's words seldom say their spaces and never their lines' ends: where a
    letter begins more than 0.15 of its size after the last one ended, a space is put between; where it is
    off the last one's line by more than half the larger size, or back before it, a line's end. Joined letters
    (fi, fl, ffi...) are said singly. Unseen text (a scanned page's) is there too.
  - **A letter's character**: a simple font's by its encoding (a carried Type 1 or CFF font's own encoding
    through its glyphs' names); `ToUnicode` where there is one; a composite font without one by the font it
    carries: a TrueType font's character map read backwards (once, when first asked), a CFF font's names.
  - **`pdf_text_find`**: the next place a text is, or the one before; capitals and small letters one (Latin-1
    and Latin Extended-A too), a line's end a space, curled quotes and long dashes plain. **`pdf_text_utf8`**:
    letters as UTF-8, lines ended CR LF.
  - **In the program**: Edit, Find... (Ctrl+F), Find Next (F3), Find Previous (Shift+F3): on this page after
    what was last found, then page after page round the file's end to where it began; what is found is tinted
    yellow in the page's picture (a mark a line; what was under it is kept and put back) and scrolled into
    sight, at any zoom. Copy Page Text (Ctrl+C) puts the page's text on the clipboard.
  - **Tests.** `pdfdraw_test` (54 checks): the text page's 174 letters as text, to the letter (the kerned
    line's gap a space, the unseen line there, the line by glyph numbers read by its font's character map),
    letters' places and sizes (one turned a quarter), finding forwards and backwards, over a line's end, in
    another case; the carried fonts' page as text; the damaged-file runs read each file's text too. The
    program's scripts copy both pages' text (346 and 87 bytes), find a phrase that runs over two lines (two
    marks), a word through the file and round its end, backwards, and one that isn't there.
  - **Not yet**: choosing text with the mouse (the button moves the page: a text tool beside it); a long
    search can't be stopped; text in columns comes in the order the file draws it; right-to-left text.
- **The outline** (a file's own list of its chapters, "bookmarks").
  - **`apps/shared/pdfnav.c`**: the outline's chain of entries read into a list in order, each with its title,
    its depth, whether the file shows it open, and the page it leads to; at most 4000 entries, 16 deep (a
    chain that never ends is cut by counting). A destination is found each way a file can say it: written
    out (an array that begins with the page), an action of the kind `GoTo`, a name in the catalog's `Dests`,
    a string in the catalog's tree of names (`Names`, `Dests`: `Kids` between their `Limits`, leaves of
    `Names`), a page's number; anything else (another file, the web) leads nowhere.
  - **`pdf_page_number`** (`pdf.c`): which page an object is: up by its parents to the tree's top, counting
    at each the pages its parent has before it. **`pdf_text_string`**: a title as UTF-8, from the format's own
    table of characters (PDFDocEncoding) or UTF-16 after its mark (pairs beyond 65535 too), or UTF-8's.
  - **In the program**: a pane at the left of the page (a splitter: dragged, folded by View, Outline: F9),
    opened as a file with an outline opens, folded away for one without; a tree, the entries the file shows
    open opened; an entry chosen (the mouse, the arrows) shows its page. Fit Width fits what the pane leaves.
  - **Tests.** `pdf_test` (64 checks): a file of five pages in a tree two deep: each page's number, what is
    no page (a page of no tree, a branch, the catalog); its outline's eight entries each with title, depth
    and page: every way of saying a destination, one that leads out, a title with the format's own bullet
    and trade mark, one in UTF-16 with a face from beyond 65535; a file with none; the damaged-file runs
    (3000 now, the new file among them) read each file's outline and ask every object's page. The program's
    scripts: the test document's outline of three opens the pane, an entry chosen by the keyboard goes to
    the second page, the pane folds away and the page is fitted to the room.
- **Locked files** (`apps/shared/pdfcrypt.c`). Most locked PDFs open for anyone: they are encrypted only so
  that a reader may refuse to print or copy them, and their key is made from what the file itself says. They
  were refused; now they open. A file that does want a password asks for it.
  - **The format's standard handler, every kind**: RC4 with keys of 40 to 128 bits (V 1, 2), crypt filters
    naming RC4 or AES of 128 bits for strings and for streams (V 4), AES of 256 bits (V 5) with both ways of
    making its key (R 5's SHA-256, R 6's rounds of AES and SHA-256, -384, -512). The key is made with no
    password and checked against the file's `U`; a file whose check fails wants a password
    (`PDF_E_LOCKED`), and `pdf_open_password` tries one: as the user's, then as the owner's (which gives the
    user's, or the key itself). The older kinds take a password in the format's own characters (Latin-1's,
    from the UTF-8 typed), the newest in UTF-8.
  - **In the program**: a file that wants a password asks for it (shown as stars), again while it is wrong,
    until it is given up. What the file's lock forbids (printing, copying) is not looked at: the Viewer
    prints nothing yet, and copying a page's text is the reader's own business.
  - **Unlocking** (`pdf.c`): an object's strings as it is read from the file (each object has a key of its
    own from the file's, its number and its generation), a stream's bytes before its filters. Not unlocked,
    as the format has it: the lock's own dictionary, the tables (streams of the XRef type), what lies in an
    object stream (the whole stream was), metadata where the lock says it is left open, a stream whose own
    `Crypt` filter says Identity.
  - **Hashes and AES are BearSSL's** (in the tree since M10 for TLS; `third_party/bearssl`, MIT): MD5,
    SHA-256, -384, -512, AES in chained blocks. RC4 is a dozen lines here. The DOS program links
    `libbearssl.a` (only what it uses comes in); the host's 32-bit program and tests take the fourteen files
    they use (`build/host/libpdfbr32.a`).
  - **Tests.** `tests/host/pdf_lock.py` locks files as a writer does (RC4 and AES written out in plain
    Python, its AES checked against FIPS 197's examples); the files it makes were opened with two other
    programs too (poppler's and MuPDF's tools, run as programs: titles, pages and text as the plain file's,
    with no password, the user's and the owner's), so reader and writer are not only right by each other.
    `pdf_test` (144 checks): eight locked files, each
    way: their pages' words, their strings (an empty one, one of 19 bytes: AES's padding), let go and read
    again; objects packed in a locked object stream; what a lock leaves open; seven files that want a
    password (each kind; two with letters beyond ASCII): locked without it and with a wrong one, opened by
    the user's and by the owner's; the damaged-file runs (5000 now) take four locked files too. The
    program's scripts open a locked copy of the test document (its outline and its text as the plain one's),
    and one that wants a password: a wrong one, given up, then the right one.
  - **Not yet**: locks of other makers' handlers (public keys); passwords prepared as the newest kind asks
    (SASLprep: plain letters are the same either way).
- **Next**: drawing off the window's thread with a cache of pages, choosing text with the mouse, links.

## The Games Library (2026-11-21): the first part

The plan is `docs/v1/apps/games-library.md`. GAMES.EXE, "Games" in the Start menu.

- **The known games** (`apps/shared/gamedb.c`): GLOS's own list, written by hand: 85 well-known DOS games, each
  with its title, year, maker and kind, the program that runs it, and a file (or folder) that lies beside it
  where the program's name alone is not enough (Doom is DOOM.EXE with DOOM.WAD or DOOM1.WAD; Final Doom runs by
  DOOM2.EXE as Doom II does, and its own WAD tells it: the list is tried in order, the more telling first).
  A game is in the list only where its program's name is beyond doubt; the plan's "a few hundred" comes by
  adding to it. **Not by size**, as the plan had it: sizes would have to be measured from the games' own
  files, which GLOS's source has none of; names and companions are facts anyone can state.
- **The library** (`apps/shared/gamelib.c`): the games with what the user gave them (a title, a picture,
  notes, a favourite) and what was counted (time played, how often, when last), kept as lines of text in
  GAMES.CFG in GLOS's folder; lines it doesn't know are passed over (a newer GLOS's file reads).
  - **A folder of games gone through**: each folder in it is a game if it holds a known game's files, or a
    program that looks like one: named as its folder (or nearly: KEEN4E.EXE in KEEN), else a batch file
    (there to start it), else the largest; set-up and installing programs, sound choosers, DOS extenders
    and the like are no games (a set-up program is kept for the game's page). A folder with none but with
    folders is looked into, once (C:\GAMES\ID\DOOM).
  - **Scanned again**: nothing twice; what the user changed stays; a game whose program has gone is marked,
    not let go (its times and notes are the user's), and found again when it is back.
  - The folders to begin with: GAMES on the drive DOS started from, and C:\GAMES, where they are there.
- **The program** (`apps/games/games.c`):
  - the games as a grid of pictures (the toolkit's thumbnail view: a game's picture where the user gave it
    one, any format Images reads, made small and kept by `apps/shared/thumbs.c`) or as a list (title, year,
    maker, kind, time played); sorted by title, by when last played, by time played; favourites alone;
    Find narrows them as it is typed (titles, makers, kinds);
  - a game's page at the right: what is known of it, where it is, how long and how often it was played and
    when last, the user's notes; Play, and Set Up where it has a set-up program;
  - Play (Enter, a double click) runs the program by JOB as any program of DOS's (its profile decides how:
    full-screen when it wants the screen); its end is seen (JOB's event), the time it ran counted and kept;
    a program that could not be run is said so and counts as no play;
  - Add Game... (any program, by hand), Add Folder..., Scan Now (F5; and as it starts), Properties (title,
    picture, notes, favourite), Remove (from the library: nothing on the disk is touched).
- **Tests.**
  - `gamelib_test` on the host (32 checks, under the sanitizers too): the list whole and no game hidden
    behind another; a made-up tree of game folders: known games (the more telling companion, a companion
    that is a folder, names in any case), unknown ones each way a program is chosen, folders in folders,
    folders with no game; the file written and read back the same, a newer GLOS's and a damaged one; a second
    scan; games added by hand and removed; the orders and the search.
  - `tests/gui/games.txt` (host and Loop A, pictures compared) on `tests/gui/drives/C/GAMES` (made by
    `tests/gui/mkgames.py`: names only, nothing of any game; each "program" two bytes that end it at once
    should it ever be run): five games found, a game's page, favourites, Find, properties with a picture,
    the list, a game added by hand and removed.
  - `tests/gui/games-run.txt` (Loop A alone: the host runs no program of DOS's): a real program of five bytes
    played twice: its end and its exit code seen, the plays counted.
- **Not yet** (the plan's rest): the scan in the background and folders watched; a game's profile as a form
  (exclusive or windowed, memory, BLASTER, the controller's mapping); pictures dropped onto a game, the
  games' own icons, screenshots taken while they run; DOSBench from a game's page; the Start menu's Games
  group and pinned games; grouping by kind and year.

## Chat (2026-11-21): the first part

The plan is `docs/v1/apps/chat.md`. CHAT.EXE, "Chat" in the Start menu: an IRC client.

- **IRC** (`apps/shared/irc.c`): GLOS's own protocol code, a connection's whole state as a machine with no
  I/O of its own: the server's bytes are fed in, the bytes to send come out through a call, and what there is
  to show is kept there and told by events. So one code serves the app and the host's tests, which feed it
  sessions written out line by line.
  - **A line taken apart**: tags (their escapes undone), the prefix and its nick, the command, up to sixteen
    parameters, the last with its spaces.
  - **The user announced**: `CAP LS 302`, the capabilities wanted of those the server has (`server-time`,
    `multi-prefix`, `away-notify`, and `sasl` where the user has an account) asked for, in however many lines
    they come; SASL PLAIN; a server with no SASL has the password given to NickServ once on; one with no
    `CAP` at all is as good; a nick that is taken: the other, then the nick with underscores.
  - **What is kept**: a buffer for the server's own words, one a channel, one a private conversation; in
    each its last 400 lines (who, when: the server's time where it stamps lines, what kind: said, an action,
    a notice, a coming or going, a rank, a topic, an error), a channel's topic and its users in order (by
    rank as the server's `PREFIX` orders ranks, then by name as its `CASEMAPPING` compares names), counts of
    what is unread and of lines that name the user (as a word: "glossary" does not name glos).
  - **What happens**: joins, parts, quits, kicks, nicks changed (a private conversation follows its nick),
    ranks given and taken (modes with and without parameters, by `CHANMODES`), topics, away and back,
    messages to channels and to the user, notices (the server's and the services': no tab each), CTCP
    (an action is shown, VERSION and PING are answered, anything else, files offered among it, is not
    taken), the numbers (errors go to the tab they are about).
  - **What the user types**: said, or a command begun with a slash (/join, /part, /msg, /query, /me, /nick,
    /topic, /notice, /away, /kick, /mode, /names, /quit, /raw; any other is sent as it is); long lines go
    in pieces a line can carry, cut at a space and never within a character; a line's end typed or pasted
    is no line's end sent. Tab's nicks: those who spoke latest first.
  - **mIRC's codes** (bold, italic, underline, reverse, strike, monospace, the sixteen colours, colours by
    red, green and blue passed over) made runs of one look, or only taken out.
- **The connection** (`apps/shared/ircnet.c`): a thread of its own looks the name up, connects, and does
  TLS's handshake (seconds on a 486: the window stays alive), then goes round: what is queued to go is sent,
  what comes within a tenth of a second is queued for the window's thread. **TLS** is BearSSL's as for HTTPS
  (`sdk/tls`), which gains `tls_read_wait`: a read that waits a given time and loses nothing when none
  comes (HTTP's reads end the connection when their time runs out; a chat's connection stays open and
  mostly has nothing to say).
- **The toolkit** gains `gui_keys`: an app's own keys in a widget that has the keyboard, asked before the
  widget (Tab among them, which would else move on): Chat's line completes nicks and has its history so.
- **The program** (`apps/chat/chat.c`): the tabs at the left (a tree with the unread counts as badges, "!"
  where the user was named), the lines in a rich-text view that stays at its end (times, nicks each in a
  colour of its own, the sent colours and bold, links: pressed, a link is copied until there is a Web Reader
  to open it), a channel's topic above and its users at the right, the line at the bottom (Enter, Tab, the
  arrows for what was typed before), Connect and Disconnect, Join..., Leave and Close, Settings (server,
  port, TLS, nick, other nick, real name, account, password, channels to join, connect at start: CHAT.CFG
  in GLOS's folder; the password in GLOS's secrets, never in the file).
  `CHAT -demo` talks to a server made up inside it, with a clock of its own: Chat without a network, and
  the same every time for the suite.
- **Tests.**
  - `irc_test` on the host (75 checks, under the sanitizers too): lines and tags, the codes, whole sessions
    (with and without SASL, SASL refused, no SASL, no CAP, nicks taken), a channel's life, messages of every
    kind, every command typed, long lines, Tab, names compared both ways, more lines than are kept, lines
    too long and in pieces, closing and ending; then 3000 sessions with bytes changed and commands typed at
    random, and 200 of no sense at all.
  - Loop A's https suite: `IRCTEST` (`tests/dos/irctest.c`: the connection's thread and IRC with no window)
    against an IRC server on the host (`tests/loopa/jobs.py`): over TLS with an account (SASL), and plain:
    announced, a channel joined (topic, names), a line said, the bot's answer heard with the server's time
    on it, the link closed by QUIT; a certificate of another name refused ("wrong name"), a port nobody
    listens on.
  - `tests/gui/chat.txt` (host and Loop A, pictures compared) with `CHAT -demo`: on and joined, lines said
    and answered (colours, a link, the user named), an action, Tab and the arrows, a private conversation
    and its tab, another channel by the menu, the topic changed, a tab closed, the link ended.
- **Not yet** (the plan's rest): more than one network at a time and a list of them; logs to files and a
  search over them; notifications when the window is not in front and the task bar's flash; IRCv3's chat
  history, account-notify and message tags beyond the time; stripping colours by a setting; joins and parts
  hidden by a setting; reconnecting by itself; a ping of its own when the server is silent.

## The Web Reader (2026-11-21): the first part

The plan is `docs/v1/apps/web-reader.md`. WEB.EXE, "Web Reader" in the Start menu: pages read as text with
their links, over HTTPS; no scripts, no styles' sheets.

- **URLs** (`apps/shared/url.c`), from RFC 3986: taken apart (scheme, host, port, path, query, fragment;
  an address in brackets), a link made whole against its page's address (the RFC's rules, the dots of its path
  removed), what a URL cannot have written as escapes, a search's words escaped.
- **HTML** (`apps/shared/html.c`): GLOS's own reader in the manner of HTML5's tokenizer: whatever the bytes, a
  page comes out. No tree is kept: the page is laid out as it is read, into calls (a block of a kind and
  depth, a run of text with its style and link, a row's next cell, a colour, a picture's place, the title, an
  anchor, a form's field) which the app makes a rich-text document of and the tests lines of text.
  - **Text**: blanks made one (but in `pre`), character references (the named ones a page is likely to
    have, the vowels with their marks by rule, numbers in tens and sixteens, Windows-1252's where pages mean
    them; in an attribute, one with no semicolon before "=" or a letter is left alone: a query's `&copy=`).
  - **Elements**: headings, paragraphs and the other blocks, styles in styles (bold, italic, code,
    struck), links (made whole; a link in a link ends the first), lists in lists with their depth, quotes,
    preformatted text, line breaks, rules, tables as rows of cells (a block in a cell is a blank, not a
    block), definitions, pictures (their address, their words, a link they are in), `base`, forms' fields
    (told; shown as a word in brackets until forms are done).
  - **Not shown**: scripts, styles, templates, SVG, frames and their like with all in them; elements marked
    `hidden` or `display: none`; comments. A colour written on an element is heeded where it can be read on
    white.
  - **Tags out of order**: the open elements are a stack; an end tag closes back to its start, one with
    none is passed over, one does not reach out of a table's cell; a paragraph is ended by a block, an
    item by the next item, a cell by the next cell; a tag not known is as if it were not there.
  - **Characters**: as the sender says; else the page's own `meta`; else UTF-8 if its bytes are that, and
    Latin (Windows-1252) if not.
- **Fetching** (`apps/shared/webget.c`): a thread runs GLOS's HTTP client and keeps what comes (2 MB at most);
  the window's thread asks how far it is, and may give it up. `sdk/http` now says what a body is
  (`Content-Type`) and takes who asks and what is wanted (`User-Agent`, `Accept`).
- **The program** (`apps/web/web.c`): the address (a whole address, a site's name, a DOS path, or words to
  search for: DuckDuckGo's plain pages, as the owner chose), the page in the rich-text view (links pressed
  or reached by Tab and Enter), Back, Forward, Reload, Stop, Home; `http:`, `https:`, `file:` and its own
  start page; text files as they are; a picture that a link leads to is shown (Images' decoders); other
  files are named, not saved yet. A page that can't be had is said in the page's place, with why.
  `WEB -dump URL` fetches and reads a page with no window and prints it a line a block.
- **Tests.**
  - `url_test` (64 checks): the RFC's own examples of references made whole, normal and abnormal, every
    one; what pages have (blanks, capitals, ports, other schemes, letters beyond ASCII); 20000 URLs of
    random bytes.
  - `html_test` (30 checks): pages written out, each read into lines and held against what it should be:
    every element and rule above, pages in Latin and UTF-8 each way they can say so, a paragraph longer
    than a run's buffer, elements nested 800 deep; 6000 pages with bytes changed and 500 of no sense.
  - `tests/gui/web.txt` (host and Loop A, pictures compared) on `tests/gui/drives/C/WEB`: a page of
    everything, its link followed by Tab and Enter to a page in Latin letters, Back and Forward, a page
    that is not there, a text file, a picture, the start page.
  - Loop A's https suite: `WEB -dump` of a page from the suite's HTTPS server: its title, heading, text
    with a link made whole, list, table and picture's place; the same page sent in Latin letters; a 404.
- **Not yet** (the plan's rest): pictures in pages (their places are shown by their words); forms; a part
  of a page gone to by its `#name`; tabs, history kept between runs and bookmarks; find in page; downloads;
  cookies; the reader view; Gemini and Gopher; compressed bodies (none are asked for); styles' sheets.

## Disk Tools (2026-11-21): the first part

The plan is `docs/v1/apps/disk-tools.md`. DISKS.EXE, "Disk Tools" in the Start menu: the PC's disks as the
BIOS has them, images of them, and what is inside an image.

- **FAT volumes** (`apps/shared/fatimg.c`): GLOS's own small reader and writer, FAT12 and FAT16 from
  Microsoft's specification, over a call that reads and writes sectors (an image's file, a disk). A volume's
  first sector is held to the rules before anything is believed; its FAT is kept in memory; folders are
  listed with their long names; files are read from any byte; folders and files are added by their DOS
  names (a chain is bounded by the volume's clusters, a folder grows by a cluster, what could not be finished
  is taken back).
- **Sector jobs** (`apps/shared/diskjob.c`): a disk to a file, a file to a disk and read back against it,
  every sector read. 32 sectors a call, or a floppy's track (each call is then one turn of the disk: a
  1.44 MB floppy is read in about 50 seconds, where runs of 32, which cross tracks, took 73); a run that fails
  is gone through a sector at a time, each tried three times; a sector that stays bad is counted and its place kept (the first sixteen), and is zeros in an image,
  so that every other sector is at its own place. A tick says how far, and stops the job.
- **GLOS_FS_SECTORS, faster** (`kernel/app/api2fs.c`): the call went to the BIOS a sector at a time. A
  floppy's next sector may be past the head by the time it is asked for, and the disk then turns once for
  each: ten minutes for a disk. It now asks for runs: to the end of the track by cylinder, head and sector,
  all 32 with the LBA calls, through a larger block of DOS's memory laid so that the sectors never cross a
  64 KB line (the floppy's DMA). No change to the call itself.
- **The program** (`apps/disks/disks.c`):
  - **The disks**: the floppy drives and the hard disks, their sizes; a hard disk's partitions (kind, start,
    size, which one starts the PC); a floppy's label, file system and free space once it is looked at (F5, or
    a job: a floppy is not made to spin by the list).
  - **An image of a disk** to a file: bad sectors tried again, counted and named at the end.
  - **An image written to a disk**: its size must be the disk's; asked once for a floppy and twice for a
    hard disk (by its size and what is on it); the first sector is read and written back first, which finds
    a write-protected disk and the disk GLOS runs from (the kernel refuses that one, with no way round);
    then every sector is read back against the image. DOS is told to forget its buffers (INT 21h's disk
    reset); after a hard disk the PC is to be restarted.
  - **Check**: every sector read.
  - **Format** a floppy: a new empty FAT12 volume with a label, as the drive's own size has it (the
    kernel's `fat_format`, the code GLOS's DISK command uses).
  - **Look inside** an image file or a floppy: folders entered and left, files and folders copied out to a
    folder (dated as they were).
  - **A new image from a folder**: 360 KB to 2.88 MB, the folder's files and folders added; names that are
    not DOS's are left out and counted; a file that does not fit stops it, and no image is left.
  - Long jobs are the toolkit's tasks: progress, Cancel, one at a time. A stopped or failed image is removed.
- **Buttons' numbers** (a fault found here): a button's function is told what happened, not the button's
  number, which is `b->id`. Four dialogs took the one for the other, so that Cancel and Esc did what OK does:
  the PDF Viewer's Go to Page, MIDI Studio's Instrument and Tempo, Chat's Settings, the Games Library's
  Properties (whose Choose button closed the dialog, and whose Set Up button played the game). Put right;
  each script now leaves its dialog by Esc and waits for the program to say so.
- **Tests.**
  - `fatimg_test` (69 checks) and `fatimg_test.sh`: volumes of 360 KB, 1.44 MB and FAT16 made, filled and
    read back; `fsck.fat` finds each clean and mtools reads its files to the byte; a volume of mtools'
    making, with long names, is listed and read; 900 damaged volumes neither crash nor run on.
  - `diskjob_test` (34 checks): a disk in memory whose sectors fail for ever, a number of times, on
    writing, or keep other bytes than those written; stops; files of the wrong size and files that can't be
    written.
  - `tests/gui/disks.txt` (the host's made-up disks, pictures): format, two new images, an image of the
    wrong size refused, written and read back, looked inside, copied out, the floppy's image, checks.
  - `tests/gui/disks-run.txt` (Loop A, the BIOS's floppy, four machines): a new image, opened, copied out;
    the PC's own boot floppy read to an image, the image written back and verified (the same bytes: the
    floppy is as it was), looked inside. About four minutes a machine, most of it the floppy's turning. A
    script's `said` waits two minutes; `within N` before it (new) makes the next one wait N seconds.
  - APITEST: 32 sectors in one call are the same bytes as 32 calls, on the hard disk and the floppy; a run
    written to the second disk and read back in two.
- **Not yet** (the plan's rest): a full format and floppies of another size than their drive's (both want
  the BIOS's track format and the disk's own geometry in GLOS_FS_SECTORS: an API step); the system put on a
  formatted disk; images in parts for disks over 2 GB; logical drives in an extended partition; an image
  opened as a folder in Files; long names written; backups over SFTP and their schedule.

## Help (2026-11-21): the first part

The plan is `docs/v1/apps/help.md`. HELP.EXE, "Help" in the Start menu, and F1 in every program.

- **The guide** (`help/*.MD`): GLOS's user guide, 28 pages of Markdown written for the user: the desktop,
  Files, DOS programs and games, the command line, a page for each program (named as the program is:
  NOTEPAD.MD), sound, keyboards and pads, the network, settings, and what to do when something goes wrong.
  Written from what the programs do now (their menus and keys read out of the sources), not from the
  plans: what is not built is not promised. **The owner has yet to read it.**
- **One file** (`tools/mkhelp.py` makes `build/apps/HELP.DAT`, beside the programs): the pages one after
  another. Making it holds the guide to its rules, and a page that breaks one stops the build: a link goes
  to a page that is there, every page is in the first page's contents, a page begins with its title, names
  are DOS's.
- **The program** (`apps/help/help.c`): the contents as a tree (the first page's list of links, nested as
  it is), a page in the rich-text view by the toolkit's Markdown reader, links between pages (pressed, or
  reached by Tab), Back, Forward and First Page, and a search of every page for the words typed (all of
  them in a line, letters in either case), whose lines open their pages. The window is titled with the
  page's title. `HELP NAME` opens NAME.MD.
- **F1** (`gui_help`, sdk/gui/core.c): a key nobody took (no command of the app's, not the widget with the
  keyboard, not the window's own key function) that is F1 starts HELP.EXE, beside the program, with the
  program's name: its page, or the first page if the guide has none for it. A terminal's F1 stays the
  program's in it. `gui_help("NAME")` is for an app that wants another page.
- **Tests.** The build's own checks of the guide; `tests/gui/help.txt` (host and Loop A, pictures
  compared): the contents, a link by Tab and Enter, Back, Forward, a search and its page, F1 in Help
  itself; `help-page.txt`: a program's page by its name; `help-f1.txt` (Loop A): F1 in Notepad opens
  Help at Notepad's page.
- **Not yet** (the plan's rest): pictures in pages; the place in a page a link names and where a search's
  words are; the welcome tour on the first start and the tip of the day; a dialog's own Help button; one
  Help window for every F1 (each F1 starts another now); the guide in other languages.

## The skiing game (2026-11-21): the first part

The plan is `docs/v1/apps/skiing.md`. SKI.EXE, "Skiing" in the Start menu at first (a working name; **Fall Line** since 2026-11-21, the owner's choice, with falls costing time only). The name, the
skier's figure and whether a fall can end a run are the owner's to choose (the plan's last section); built
with the plan's defaults (one figure with a jacket's colour chosen; a fall costs time and no more), and the
name is one line of the program. GLOS's own game: its skier, pictures, mountains and rules were made for it.

- **The mountains** (`apps/shared/skimount.c`): a mountain is a number. The piste's middle wanders and its
  width swells and narrows by values every few hundred pixels with an easy curve between; one line down it is
  kept clear (in a slalom it swings through the gates), and nothing is ever put within a skier's width of
  that line; beside the piste powder, then forest; on it, a piece of 128 pixels at a time and each piece
  from the number alone: rocks, lone firs, moguls, ramps, short fences, and from blue up patches of ice. Four
  grades (wander, width, how much is in the way, ice, the gates' gap and width, the length).
- **The runs** (`apps/shared/skirun.c`): sixty steps a second from the keys held. Seventeen headings (eight
  each side of the fall line, and the fall line: the plan said sixteen); the slope pulls along the skis by
  how nearly they point down it; the snow drags by its kind and takes away the speed across the skis (a
  quarter of it a step on the piste, a twenty-fourth on ice: the skier slides on); a tuck, a plough; ramps
  and moguls throw the skier into the air, where turning turns the skier: a half turn or a whole one landed
  is points, part of one a fall; trees, rocks, fences and the hut are falls of a second and a half, with
  three quarters of a second afterwards in which nothing is; gates passed or missed (five seconds); a free
  run's points. `skirun_auto` is the careful skier: the keys that follow the clear line and brake.
- **Whole numbers only**, in both: a run is its mountain's number and its keys, and ends at the same step
  with the same score on the host's 64-bit and 32-bit builds and on DJGPP's.
- **The tables** (`apps/shared/skitable.c`): the best ten of each grade and kind, and of the mountain of the
  day, with names; SKI.DAT in GLOS's folder, which also keeps the last name and choices.
- **The program** (`apps/ski/ski.c`, `art.c`): the field of 480 by 360 and a strip of figures beside it.
  The snow in bands by its kind, the ice, the skier's tracks, then everything in sight from the top down
  with the skier among it. The pictures are made by rule when the game starts (`art.c`: one-colour shapes
  laid over one another; the skier in three poses and 32 directions), in colours the 8-bit palette has, so
  the game is the same at every depth. Slalom, free run, the mountain of the day, a mountain by its number,
  four grades, five jackets, the tables' window, Pause, and the careful skier to watch.
- **Tests.**
  - `ski_test` (58 checks): a mountain made twice is the same; 1200 mountains (8000 with `more`) keep to
    the slope and keep their things off the clear line; a way down is found on its own over a grid of the
    piste with everything on it taken out, through every gate (320 mountains; 2000); the careful skier gets
    down 1200 (24000) without a fall or a missed gate; speeds by heading, tuck, plough, snow and grade; a
    ramp, the turns that land and the one that does not; a rock; gates missed; four runs whose last step
    and score are written in the test; the tables' order, file and day.
  - `tests/gui/ski.txt` (host and Loop A, pictures compared at 8, 16 and 32 bits): the careful skier on
    mountain 7 held at three steps and at the valley; then a run of the player's with no key held, which
    falls eight times, misses sixteen gates and still arrives, to the step; its place in the table under a
    name; a mountain by number; a jacket.
- **Sound** (`apps/ski/sound.c`, added the same day): one stream into GLOS's mixer, made by rule as it
  plays: the skis' hiss (noise through a filter that ice opens and powder closes, as loud as the skier is
  fast, silent in the air), a gate's bell, a miss's buzz, a jump's rush of air, a landing's thump, a fall,
  the valley's three notes. Game, Sound turns it off. Silent where there is no card GLOS plays through,
  and in the suite's runs.
- **The ghost** (added the same day): a slalom that is its mountain's best is kept as its keys (a byte a
  step: SKIGHOST.DAT beside the tables, the last mountain's) and skied again beside the player on that
  mountain, a grey shape on the snow: the same run, since a run is its mountain and its keys.
- **Not yet** (the plan's rest): weather and night (and the wind's sound); other skiers, the groomer, the hare, lift pylons; the mouse and pads; the window doubled and full
  screen; the demonstration playing by itself when nobody does. **To be measured**: the frame rate on the 486 bench
  (the plan's floor: thirty a second on a 486DX2-66 at 8 bits).

## The hovercraft game (2026-11-21): its core, not yet a program

The plan is `docs/v1/apps/hovercraft.md`. No program yet: the game's truth, which every later part stands
on, written and tested on the host. Its name, its setting and whether anything is picked up are the owner's
to choose (the plan's last section); nothing here depends on them.

- **Arenas** (`apps/shared/hvarena.c`): a text file, a letter a square (wall, floor, floor one and two
  levels up, ramps and their way, water, each side's dock, beacons, the lamp, a lap's buoys) under a short
  head. The reader refuses a file with a square it cannot read, a row of another length, no dock for a
  side, a ramp that comes from nowhere or leads nowhere, a place nobody can reach, or a place a craft could
  never leave, and says the line (and the column). Where a craft can go from square to square (level, a
  drop, up a ramp; never up a step, across a ramp's side, or into a wall), and how far every square is from
  a place by those ways: the computer's driving goes by it.
- **The game's truth** (`apps/shared/hvgame.c`): thirty steps a second from every craft's controls. A
  craft's fans change its speed, never its place: thrust along its heading, side fans across it, a boost
  with a tank; the air takes a twenty-fourth of its speed a step; set down on its skirt it stops in a
  length and cannot be pushed. An edge it may not cross sends it back with half its speed; water takes it
  three seconds and puts it at its dock. Craft that touch pass speed along the line between them. The four
  games' rules: flags (a hard bump gives the flag to whoever bumped: home if it is theirs, in their hands
  if they are the carrier's side; a carrier set down keeps it), beacons counted each second, the lamp held
  for points and taken by any bump, laps round the buoys in order.
- **The computer's craft** (`hv_auto`): the place wanted by the game's state, the next square nearer to it,
  and the fans that go there; three strengths (the side fans against a slide, the boost on a straight run).
- **Whole numbers only**: a game is its arena and its controls, the same on every build (and, later, on
  every PC of a network game).
- **Tests** (`hv_test`, 90 checks): the arena's letters, ways and distances, and each fault a file can
  have; speeds, the slide, set down, the boost and its tank; walls, steps, ramps, drops, water; ten
  thousand steps of eight craft with random fans never put one in a wall; bumps head on and against a
  craft set down; each game's rules; the computer's craft against one that stands still score in every
  kind of game at every strength; every game played twice is the same.
- **Not yet**: the drawing (DOS-GL), the lobby, the network's messages and their model, sound, the six
  arenas, the computer's craft against each other measured over many games. (A craft whose goal was dead
  astern neither turned nor pushed: one of four made no lap. It turns about now, by the right. And two that
  met head on pushed at each other for good: a craft with another close ahead slides to its own right, and
  they pass, where the lane is two craft wide: the arenas' lanes are.) The test plays every arena at every
  kind of game with four of the computer's craft, which must score in each. **Known**:
  the keenest driver's boost wins it laps and loses it flags (it overshoots): the strengths want their own
  measuring.

### Its first playable part (the same day)

HVCRAFT.EXE, "Hovercraft" in the Start menu at first (a working name; **Cushion** since 2026-11-21, the owner's choice, with no pickups): `apps/hover/hvcraft.c`. Six arenas, built
in (`apps/shared/hvarenas.c`: the Old Basin, the Dry Dock, the Timber Yard, the Lock Gates, the Customs Quay,
the Roundhouse; Tab goes from one to the next); the player's craft and three of the computer's (the middle strength); the four kinds of game on keys 1
to 4; the picture drawn with DOS-GL in a window, as the savers draw: the arena's squares as boxes lit by the
way they face (walls, floors, quays, ramps as wedges, water, docks in their sides' colours), beacons, the
lamp, buoys and flags as posts, the craft as a few boxes turned to their headings, seen from behind and
above the player's (the eye comes in closer where a wall is behind the craft). Over the scene, flat: the
score in the sides' colours, the boost's tank, and a small map of the arena with the craft, the flags, the
beacons, the lamp or the next buoy. Not a toolkit program: its window is DOS-GL's.

- **Loop A** (`hover_case`, in the desk suite, on the G200 and on the S3): `HVCRAFT -demo`, every craft
  the computer's, each kind of game to step 450: the state it says there is the one written in the suite
  (made on the host by the same code: `hv_test demo KIND 450`, from the arena both share),
  the same whether the card's engine draws the window or DOS-GL's software renderer does; and a picture
  taken while it plays has a scene's worth of colours.
- **Sound** (`apps/hover/hvsound.c`, added the same day): one stream into the mixer, made by rule: the
  fans' hum, whose pitch follows their work (idling, pushing, boosting; low when set down), air by the
  craft's speed, the skirt's scrape, bumps, walls, the water, a flag or the lamp taken and lost, a point, a
  beacon. Silent with no card, and in the suite's games.
- **The user's own arenas** (added the same day): text files in the folder ARENAS beside the program, read
  after the game's six; one the reader refuses is left out and why is said on the program's output. The
  guide's page says how one is written. Loop A's hover case has one read and played, and one refused with
  its line.
- **Not yet**: all of the plan beyond that (the lobby, network games, pads and the mouse, full screen,
  names over the craft). **To be measured**: frames a second on
  the benches' cards and with the software renderer.

### Its network's messages (the same day; no sockets yet)

`apps/shared/hvnet.c`: what the PCs of a game say to each other, as bytes in and out, so that the host's test
can put a bad network between copies of the game. One PC opens a game and runs its steps for everyone; the
others send their craft's controls each step and are sent the whole game's state every other step (157 bytes
for eight craft: every craft's place, speed and heading, the scores, flags, beacons and lamp). Nothing depends
on a message before it: a datagram lost is the next one's to mend; an older one that comes late is passed
over (controls by their count, states by their step). FIND and OFFER (a game looked for and shown), JOIN and
TAKEN or FULL (asked again, the same craft is given), KEYS, STATE, BYE. A joiner not heard for three seconds
is the computer's. Every message has the game's mark and version: another release's is not answered, but
for a JOIN, which is told so.

- **Tests** (in `hv_test`, 133 checks in all): an opener and two joiners over a network that loses 30 in a
  hundred, doubles 10 and delivers up to three steps late and out of order: both join, each with its own
  craft; every state a joiner takes in is the opener's game as it was at that step, to the unit; a game of
  beacons is played to a score by the joiners' own driving from what they see; the quiet one and the one
  that leaves; a game with no craft free; another release; 20000 datagrams of no sense or with bytes changed
  never put a craft of a joiner's view off the arena.
- **Not yet**: the sockets (GLOS's UDP, API 2.2: a broadcast to find, datagrams to play), the lobby that
  lists games and joins one, a joiner's own craft drawn from its own controls at once and put right gently
  (the messages carry what that needs: the controls' count), the others drawn between the states told.

### Over the network (the same day): the sockets, and two keys

`apps/hover/hvlink.c` puts GLOS's datagram ports (NET, API 2.2) under those messages, and the game has two
keys for them. **O** opens the game to the other GLOS PCs of the network (it begins again; one of the game's
six arenas, which every PC has) and closes it; **J** looks for an open game and joins the first with a craft
free, and leaves it. No addresses are typed: the one that looks asks everyone on port 18518 twice a second,
and every open game answers. The window's title says how things stand (open and how many have joined; looking;
in whose game). The game's name on the network is the PC's own (`[net] hostname`).

- **The ports:** an opener has the well-known port (shared, so several games may be open on one PC), where it
  is asked, and a port of its own, from which it answers and at which its joiners then speak to it. A joiner
  has one port of its own. Nothing waits: a step takes what has come in and sends what is due.
- **A joiner** sends its controls each step and takes one state a step (they come fifteen a second: none waits
  long, and a PC that draws slowly, hearing them in bursts, still sees each; if many wait, the newest). In a
  step without one every craft goes on as it was going, three steps at most and never into a wall
  (`hv_joiner_coast`). The sounds' events are made from how two states differ: a flag or the lamp taken, a
  flag dropped, a point, a beacon, a craft in the water (not the bumps). Its camera, map and tank are its own
  craft's.
- **Ends:** a joiner that leaves says so (twice); one not heard for three seconds is the computer's; a joiner
  that hears nothing for three seconds is alone again, and so is one that finds no game in ten.
- **What the game remembers:** `HVCRAFT.CFG` beside the program, lines of `key = value`: the game and the
  arena last played (written when the game ends), a `name` for the network (the PC's own if none), and
  `network = open` or `network = join`: the game opens to the network, or looks for a game, whenever it
  starts (for the PC that the others join; written by hand until the lobby sets it). The suite's games
  (`-demo`, `-until`) write nothing.
- **Tests:** `hv_test` (144 checks): the events from pairs of states, and coasting. Loop A, the desk suite:
  - the hover case: two copies of the game on one emulated PC, where a broadcast reaches the PC's own ports
    as it would another PC's (API 2.9): one opens, the other finds it and is given the second craft; both say
    the game at every 150th step as they have it, and for the same step the two lines are the same; the
    joiner leaves and the opener says its craft the computer's again; one that looks where nobody has a game
    says so;
  - `hoverpair` (`DESK_ONLY=hoverpair`): **two emulated PCs**, the near one with a network card, the far one
    with none, joined to it by a cable (COM2 to COM2) and a member of its network. The far PC's desktop
    starts the game (`[desktop] start`), which opens a game of beacons as its HVCRAFT.CFG says, under the
    PC's own name; the near PC's copy looks, its broadcast is copied down the cable, and it is given the
    second craft and told the game for 900 steps across the cable (some 2 KB a second each way at 115200).
- **Found on the way:** a batch file among `[desktop] start`'s programs cannot start an app (it runs under
  COMMAND.COM, and an app's parent must be GLOS): programs there take no arguments, which is why the game
  reads a file of its own.
- **Not yet:** the lobby (the games found as a list, the player's name and craft's colour, the computer's
  strength); an arena of the user's own sent to the joiners; a joiner's own craft moved at once by its own
  controls and put right gently; two real PCs across a switch and across a cable.

## The Devices page (2026-11-21)

The owner asked for a Device Manager in the Control Panel (2026-11-21): what devices are found, whether
GLOS's drivers support them and have them, and each one's details. The owner chose a page of the Control
Panel (the USB page's tree moved into it), turning GLOS's drivers off and on per device, and names from the
PCI ID Project's list under its BSD option.

- **The kernel's list** (`kernel/drv/devlist.c`; API 2.12's `GLOS_STAT_DEVICES`, the shell's `DEVICES`): the
  PCI functions as the scan found them (their revision, subsystem and the sizes of their ranges now kept, the
  ranges sized once as GLOS starts, a bridge's registers after its first two no longer taken for ranges),
  each with what its driver did (`dev_note`: the network card's, the USB controllers', the sound cards'; the
  Matrox engine's card; the card GLOS draws on through VBE), the ISA cards GLOS's drivers know, the USB
  devices under their controllers, the PC's own (COM and LPT ports as the BIOS counted them, the keyboard's
  controller, a PS/2 mouse, the floppy controller with its drives' kinds from the CMOS, the hard disks'
  count, a game port, the interrupt and DMA controllers, the timer and the RTC) and GLOS's virtual Sound
  Blaster. Details under each from its driver (`glos net`'s, `glos audio`'s, `glos display`'s lines).
- **`[devices] off`**: the devices GLOS's drivers leave to DOS, by PCI place or name (sb16, crystal,
  ne2000). The loader copies it into the boot information (version 10: the network card is looked for
  before the kernel has read GLOS.CFG), and the drivers ask it as they look (`dev_turned_off`): the network
  card's (any of the four PCI ones, and the ISA NE2000: the next card in the order is looked at), the USB
  controllers', the SB16's (where the mixer first finds it) and the Crystal chip's. `listed=1` says the
  setting names a device now, so the page can say "turned off when GLOS starts again".
- **The page** (`apps/control/devices.c`): a tree by kind, the chosen one's lines in a list under it, Turn
  off / Turn on, which edits the page's setting field (Apply writes it); the network card GLOS uses and a USB
  controller ask first. PCI names from `PCIIDS.DAT` beside the program (`tools/mkpciids.py`: 2526 vendors,
  21914 devices, 218 KB packed; `apps/shared/pciids.c` finds every name asked for in one pass through it).
- **Tests:** `pciids_test` (the names of the benches' devices, damaged copies); APITEST `sys-devices`; the
  ssh suite's `devices` case (`SSH_ONLY=devices`): the list against `glos pci`, the network card's, the SB16's
  and the USB controller's lines, the setting named, a restart, each off; the network card off under GLOS
  /RUN with DEVLIST printing the list; host GUI script `control-devices` (the page, the question, Turn off
  and on); the control suite's run checks every emulated PCI device is named.
- **Not yet:** the setting for devices with settings of their own (the display's engine, the virtual Sound
  Blaster: their own pages); names for USB devices (usb.ids); the PC's ACPI and Plug and Play BIOS's lists
  of ISA devices; a device's driver started or stopped without GLOS starting again.

## Help: the welcome tour and the tip of the day (2026-11-22)

The rest of Help's plan's first item (`docs/v1/apps/help.md`): the tour on GLOS's first start, and tips.

- **The tour** (`HELP /TOUR`, and Tour on Help's Help menu): the guide's page TOUR.MD, a step for each of its
  `## ` parts, by the toolkit's tour (`gui_tour`, toolkit 2's C8): a bubble with the part's words beside the
  part of the desktop it names in a line of its own (`*At the Start button.*`, the task bar, the
  notification area, the clock: SHELL's POINT), or in the middle. Its words are the guide's, so they are
  written and checked as the guide is. Help shows the tour's page under it, and its first page at the end.
- **The first start:** `[desktop] tour` (off by default; GLOS's installer, `tools/floppy/install.py`, writes
  `tour = on` and `tips = on` into a new GLOS.CFG). When the desktop comes on (`kcfg_desktop_started`, the
  same place as `[desktop] start`'s programs), GLOS starts `HELP.EXE /TOUR` beside GLOS.EXE and turns the
  setting off: shown once. Otherwise, with `[desktop] tips` on, `HELP.EXE /TIP`.
- **The tip of the day** (`HELP /TIP [N]`): TIPS.MD's list, a tip a day (the day's number, or the Nth), in a
  small window of its own: the tip, Next Tip, a box for `[desktop] tips` (written when Close finds it
  changed), Close.
- **Tests:** GUI scripts `help-tour` (the seven steps, four beside the shell's parts, the end; again from the
  menu, closed at the third) and `help-tip` (the first two tips, the box turned on and written), on the host
  and the emulated desktops; the desk suite's `tour` case (`DESK_ONLY=tour`): a GLOS.CFG as the installer
  writes it, the desktop on, Help's tour started and the setting turned off; on again, the tip instead.
- **Not yet:** a "show me" that opens the part (the Start menu itself); pictures in the tour's steps.

## The two games: pads, and the mouse on the slope (2026-11-22)

- **Fall Line** (`apps/ski/ski.c`): the first pad plugged in steers (its stick left and right; forwards or
  its first button tucks, back or its second ploughs); the mouse steers once it moves over the slope, the
  skier turning towards the pointer (the heading from where the skier is drawn to the pointer, in the
  skier's seventeen steps), the left button tucking, the right ploughing; a key or the pad takes over again.
  What is held is what a run records, so a ghost of a run by the mouse is skied again as it was.
- **Cushion** (`apps/hover/hvcraft.c`): the first pad drives (the left stick turns and drives, the right
  stick or the shoulder buttons the side fans, the first button boosts, the second sets down).
- **Scripts' pads:** the GUI scripts' new `pad LX LY BUTTONS` / `pad off` (`tests/host/glosshim.c`; on Loop A
  `glos joy pad=0,...`, a USB pad as if plugged in).
- **Tests:** `ski-pad` (host and the emulated desktops): mountain 7 held at three steps while a pad's stick
  goes left, then right with a tuck, then the mouse to the skier's left (said "mouse steers"); the hover
  case: a pad held forwards with the boost from the start, the game at step 90 the core's for those controls
  held (`hv_test hold 3 65 90`).
- **Cushion's mouse:** once it moves over the arena, the craft turns towards the pointer's side of the window
  (the middle sixth: straight on), the left button drives the fans forwards, the right one boosts; a key or
  the pad takes over. The hover case moves the pointer into the window and hears "HVCRAFT mouse drives".
- **Not yet:** the mouse taken (captured) so that it never leaves the window; a second pad for a second
  player on one PC (not in v1).

### Frames a second, measured (2026-11-22)

- **Cushion on bench-g400** (IBM 300PL, its G400 by DOS-GL's engine in a 480 by 360 window at 16 bits;
  `HVCRAFT -demo -kind 0 -until 900`, which now says `HVCRAFT frames=N ms=T fps=F` at its end): **25.5 a
  second** (768 frames in 30 seconds of the game). The game's thirty steps a second are kept either way.
- Owed: Fall Line on the 486 (bench C, the plan's floor: thirty a second on a 486DX2-66 at 8 bits), and both
  on the other benches' cards.

## Cushion's lobby (2026-11-22)

The plan's "toolkit window" before the game (`docs/v1/apps/hovercraft.md`), `apps/hover/lobby.c`.

- **What it has:** the kind of game, the computer's craft's strength (mild, normal, keen: hv_auto's 0 to 2,
  for the computer's craft of a game alone and of a game opened), the arena (the six and the user's own),
  the player's name, and the network's open games as a list (name, kind, arena, free craft) that keeps
  itself up to date: `hvlink_browse` sends FIND once a second and lists the OFFERs that come, each gone after
  four seconds unheard. Play Alone, Open to the Network (an arena of the user's own falls back to the first:
  the others have not got it), Join (`hvlink_join_offer`: that opener, not the first that answers), Quit.
- **One program, the toolkit's window and then DOS-GL's:** the lobby runs `gui_run` to its choice and closes;
  the game (`play`, the old main loop) makes DOS-GL's window and shuts it at Esc; the lobby comes back. DOS-GL
  takes a program that is an app already (GLOS_WHY_ALREADY). The suite's flags (-demo, -until, -open,
  -join) and HVCRAFT.CFG's `network` still play at once, with no lobby.
- **Remembered** in HVCRAFT.CFG as before (the game, the arena, the name).
- **Tests:** the hover case plays `tests/loopa/hover-lobby.txt` last on both desktops: the lobby with no
  game on the network, Play Alone, the game, Esc back to the lobby, Quit.
- **Not yet:** a game of the lobby's list played over two emulated PCs (the hoverpair case joins by J); the
  strength and the name remembered across starts (the name is); names over the craft.

## The Games Library: a game's profile (2026-11-22)

The plan's "a game's profile as a form" (`docs/v1/apps/games-library.md`): the Properties dialog's lower part
is the game's program's own settings, GLOS.CFG's `[program NAME.EXE]` as the Control Panel's Programs page
has them: the screen (a window or the whole screen), the memory (KB; empty: all), the Sound Blaster it is told
of (the `BLASTER=` line of its environment; the environment's other lines kept), direct mode. Read when the
dialog opens, written (`glos_setting_set`) only where they changed; a refusal is said in a message. For every
copy of a program of that name.

- **Tests:** `games-profile` (host and the emulated desktops): Doom's Properties set to the whole screen,
  8192 KB, a Sound Blaster at IRQ 7 and direct mode, and read back when the dialog opens again.
- **Not yet:** the controller's mapping for a game (`[joystick] map` is one for every program).

## MIDI Studio: recording (2026-11-22)

The plan's "a MIDI keyboard as input: an MPU-401 port where a card has one" and "step entry from the PC's
keys" (`docs/v1/apps/midi-studio.md`), as recording: **F7** (Transport, Record).

- **What is heard:** AUDIO's MIDI port in on the MPU-401 (`glos_midi_in`; its note messages through the
  toolkit's `gui_on_event`), and the PC's keys as two octaves of a piano (Z S X ... M from middle C, Q 2 W ...
  U above), looked at on the idle (key let go: the note ends) and caught by their key events (however short
  the press). Each note played is heard at once (the playing song's port, or the FM chip's own).
- **Where it goes:** the chosen track, at the time it was played (the song plays from its start meanwhile,
  if it has notes; else a clock of its own), its start on the nearest sixteenth, its length in sixteenths,
  one at least. The whole take is one step of the history ("Record").
- **The suite's `-demo`:** the take's time is the demo's (F9: a second), so a take is the same on every PC.
- **Tests:** `studio-record` (host and the emulated desktops): C, D and E a second apart from the PC's keys:
  at 0, 960 and 1920 ticks; undone and done again. The MPU-401's input waits for a bench with a keyboard.
- **Not yet:** step entry at a cursor (a note a key press, the cursor moving on); a count-in and a click; USB
  MIDI keyboards (the USB MIDI class on GLOS's stack); velocity from the PC's keys.
