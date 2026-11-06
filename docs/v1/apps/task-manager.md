# Task Manager (Monitor, grown): plan (GLOS v1, phase A)

**Today: Monitor.** From GLOS_FN_STAT, every second:
- programs (apps, jobs, the session in front) with their time, memory and windows, and End;
- performance (the processor over a minute, memory, network frames);
- the system; the log.

It is also Ctrl+Alt+Del's task list.

**In v1:** a task manager after Windows 10's, which still runs on a 486: more detail, more history, and the
devices.

## What it does

- **Programs:**
  - **The list:** apps, jobs, sessions and GLOS's own threads (a setting), with processor and memory use, windows,
    state (running, waiting, not answering).
  - **Actions:** sorted by any column; End a program, switch to one, bring its window in front.
  - **A program that is not answering:** its window greyed and "(Not answering)" in its title (WIN's state), and
    offered an end.
- **Performance:** graphs over a minute and an hour of:
  - the processor;
  - memory (GLOS's, DOS's, the disk cache's);
  - the disk (DOS's reads and writes, the cache's hits);
  - the network (frames, bytes);
  - sound (the mixer's streams);
  - the GPU's use (the lock's holds, where a Matrox card is driven).
- **Startup:** the programs and apps that start with GLOS (`[desktop] start`), each to enable or disable.
- **Devices:**
  - the PCI functions and their keepers (D70: GLOS, quiet, DOS);
  - USB controllers and their devices (`glos usb`'s tree);
  - the sound chip;
  - the network card and its address;
  - the game port and its source.
- **The log:** GLOS's log, filtered by kind (WARN, a driver's lines), searchable, copied out.
- **Ctrl+Alt+Del:** the task list as today, and from it this window.

## From toolkit 2

- **Tabs.**
- **Lists:** the detail view with columns that sort.
- **A graph widget:** lines over time, a few series, scaled.
- **A tree:** the devices.
- **The context menu:** a program's End, Switch to, Details.

## From API 2.0

- **SYS:**
  - the programs with their figures;
  - the devices (PCI with keepers, USB, sound, network, the game port);
  - memory in its parts;
  - the disk's and the network's counters;
  - the GPU lock's figures;
  - the log, as a stream to follow rather than polled;
  - every figure a structure with a size (D14).
- **WIN:** another app's windows to bring forward; a window's "not answering" state.
- **JOB, and the kill call:** ending a program.
- **SETTINGS:** `[desktop] start` for the Startup page.

## Libraries

None.

## CPU floor

A 486DX2: it updates once a second, and its graphs draw only what changed.

## Tests

- **Host:** GUI scripts of each page against recorded GLOS_FN_STAT answers.
- **Loop A smoke:** a busy-looping app shows near 100% of the processor and ends from the list; the devices page
  lists 86Box's PCI functions and the bf6's UHCI.
- **The P4:** its real devices listed.
