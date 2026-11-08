# GLOS v1, phase A: the design documents

Phase A of the route (`docs/v1-route.md`): documents only, reviewed by the owner together at the end (the owner's
choice, 2026-11-06), and approved before any code of phases B–F.

**Reviewed with the owner on 2026-11-06:** the documents' open questions answered and written in (the route's
"Phase A's review" lists them).

## The documents

| Document | What it is | Status |
|---|---|---|
| `apps/*.md` | A plan for each app: what it does in v1, how it looks, what it needs from toolkit 2 and API 2.0, its libraries, its CPU floor, its tests | Written |
| `shell.md` | The task bar, the notification area, notifications, the Start menu, screensavers, themes and wallpapers | Written |
| `ide.md` | The follow-up IDE's needs, as if it were one more app | Written |
| `api-2.md` | API 2.0 in full, from the plans | Written |
| `toolkit-2.md` | Toolkit 2's widgets and services, from the plans | Written |
| RetroGPU `gl/docs/backend-interface.md` | Step B0 of DOS-GL's software renderer plan (`gl/docs/backends.md`) | Written (RetroGPU branch b0-docs) |

## The API's areas

Each app plan lists what it needs from API 2.0 by these areas. `api-2.md` designs each area once, from every plan's
list. "Today" is API 1.5, GLOS main.

| Area | What it covers | Today |
|---|---|---|
| **WIN** | Windows: states (minimized, maximized, restored, full screen), resizing from every edge, title-bar buttons, icons, cursors, owned and modal windows, minimum and maximum sizes, a generic property call | Create, show, move, resize, title, focus; a grip to resize |
| **DRAW** | A window's pixels and presenting them; the screen's format; DOS-GL's windowed path (GPU, VRAM, the lock) and its software path | GLOS_FN_PIXELS, PRESENT, SCREEN, GPU, VRAM, WINVRAM |
| **INPUT** | Keys, text, the mouse and its wheel, double-click time and drag distances from the settings, game controllers | Keys, the mouse, the wheel (1.5) |
| **SHELL** | The task bar's window buttons, the notification area's icons, notifications, the Start menu's entries, file associations ("open with"), recent documents, the wallpaper | GLOS_FN_SHELL, GLOS_EV_LAUNCH, `[open]` |
| **CLIP** | The clipboard, in formats: text, an image, a list of files, private formats | Text, 64 KB |
| **DND** | Drag and drop between windows and apps | None |
| **THREAD** | An app's own threads; locks and events between them; events posted from any thread | None (one thread an app) |
| **JOB** | Running programs: output streamed (stdout, stderr), input, environment, working folder, cancel, exit code | GLOS_FN_RUN, JOB |
| **FS** | Long file names, file and disk information, watching folders, the drives | GLOS_FN_WATCH; 8.3 names through DOS |
| **AUDIO** | Streams, capture, volumes (master, each stream), the devices; MIDI out (the OPL3, an MPU-401) and in (an MPU-401, USB MIDI) | Streams out (GLOS_FN_AUDIO) |
| **NET** | Connections, DNS, the network's state (for the tray), listening sockets | GLOS_FN_NET (TCP) |
| **SETTINGS** | GLOS.CFG's settings, an event when one changes, themes and colours, the region's formats | GLOS_FN_CONFIG |
| **IDLE** | Idle time, the screensaver's start and end, the screen's power | None |
| **SYS** | What GLOS knows of the system: programs, memory, devices (USB, sound, network), the log | GLOS_FN_STAT |
| **DEBUG** | A debug channel to a program the caller started (gdb's remote protocol) | gdb over COM2 and `glos gdb` (SSH) |
| **CAPTURE** | A window's pixels, and keys and mouse into it, for a program the caller started | `glos shot`, `glos key` (SSH only) |
| **SECRETS** | A store for secrets (an API key), apart from GLOS.CFG | None |
| **MODULES** | Shared modules (DJGPP's DXE3): plug-ins, a toolkit shared between apps | None |

## Conventions in the app plans

- **CPU floor:** the slowest machine the app is tested on in Loop A (486dx2, or bf6, a Pentium II), and its slower
  features named.
- **Tests, in the route's three tiers:**
  - **The host first:** the app's GUI scripts on the host shim, with goldens at 8, 16 and 32 bits; its parsers and
    codecs.
  - **A short Loop A smoke:** its window, its jobs and devices under GLOS.
  - **The Pentium 4 bench:** real speed, and the CPU floor's timing.
  - **The bench by hand,** where the owner's hardware is needed.
- **Libraries:** permissive licences only (route, Principles), each re-checked at import.
