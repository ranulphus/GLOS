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
- **Next**: a PDF's fonts (their encodings and widths, embedded TrueType; then CFF and Type 1) and its text
  operators, then the program.

