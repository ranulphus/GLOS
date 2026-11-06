# The GLOS IDE's needs (GLOS v1, phase A)

**Not built in v1.** The owner's follow-up project, in its own repository: an IDE for GLOS in the spirit of Visual
C++ and Delphi, with AI agents like Claude Code built in, for writing GLOS apps on GLOS itself. This page lists what
it will need from GLOS, so that API 2.0 and toolkit 2 carry it from the start (the owner's answer: all of it in 2.0).

## What the IDE does (as far as GLOS needs to know)

- **Projects:** C (and later Pascal) sources, resources (windows and dialogs described in files), the app's icon and
  its package's details.
- **The editor:** source files with highlighting, completion, errors marked.
- **The form designer, as Delphi's:** windows and dialogs laid out by dragging the toolkit's widgets, their
  properties set in an inspector, saved as resource files the app loads at run time.
- **Building:** DJGPP's GCC or Open Watcom run as jobs, their messages streamed into an output pane and turned into
  error marks; the build cancelled at will.
- **Running and debugging:** the app run beside the IDE; breakpoints, stepping, watches, the call stack, memory;
  the exception that stopped it.
- **Agents:**
  - an AI model (a service's API over HTTPS; no model runs on these PCs) given tools: read and write the project's
    files, build, run, see the app's window, press its keys and click it, read the debugger's state;
  - the user's permission asked for what the IDE's own rules say needs it.
- **Packaging:** the app as a GLOS package, installed on this PC to try, or signed for others.

## From API 2.0

| Need | Area | What it means |
|---|---|---|
| Builds with live output | **JOB** | A program run with its stdout and stderr streamed to the caller as they come, its stdin given, its own environment and working folder, cancelled at will, its exit code. Long command lines (response files) |
| Running the app being built | **JOB**, **WIN** | Started as a job and as an app (it calls GLOS_FN_INIT itself), its windows known to the IDE as its child's |
| Debugging it | **DEBUG** | A debug channel to a program the caller started: gdb's remote protocol (GLOS's stub, `kernel/dbg`), opened as a local connection: breakpoints, stepping, registers, memory, the stop's reason, the program's modules and their addresses |
| Seeing and driving it | **CAPTURE** | The child's windows' pixels (in a format the agent can send, PNG), and keys and mouse into them, for programs the caller started only |
| Modern projects | **FS** | Long file names; watching the project's folders for changes made elsewhere |
| The model's API | **NET**, **SECRETS** | HTTPS (the app's own TLS on GLOS's connections), a service's API key kept in the secrets store, never in a project file |
| Trying the app | **PKG** (a policy, not a call) | Installing a package built here, trusted as "local" (not the owner's signing key): PKG's trust levels |
| Plug-ins | **MODULES** | The IDE's own plug-ins (tools, languages) as DJGPP DXE3 modules; whether apps share the toolkit as a module too |
| Background work | **THREAD** | The agent's loop, builds and the language tools on their own threads |
| Copy and paste | **CLIP** | Text, and the toolkit's own formats (a dialog's widgets copied between designer windows) |

## From toolkit 2

- **Docking:** dockable panes (project, outline, output, debugger panes, the agent's conversation), saved layouts.
- **The code editor component:** Notepad's editor with completion and signature pop-ups, error and breakpoint marks
  in the margin, folding, go to definition (the language tools' business; the editor offers the hooks).
- **A property grid:** names and values, with editors by type (text, number, choice, colour, font).
- **Resource files:** windows and dialogs described in a file (the widgets, their properties, their layout), loaded
  by any app at run time; and every widget class listing its properties (introspection) for the designer.
- **The output pane:** Term's text grid as a widget, with clickable lines (a file and line).
- **A conversation view:** messages with Markdown, code blocks, the agent's tool calls shown and expandable (the
  rich-text view).

## The SDK on GLOS

D14's SDK as GLOS packages, so the IDE builds apps without a modern PC:
- the headers (`include/glos/`);
- libglos and the toolkit;
- the toolchains (DJGPP and Open Watcom, under their own licences, as packages of their own);
- the API reference as Help pages.

## Left to the IDE's own plan

- Which models and services, and whether through a relay on a modern PC.
- The agents' permissions (what they may run, write and send without asking).
- Its languages: C first; Pascal (Free Pascal's DOS target) later?
- Its repository's name.
