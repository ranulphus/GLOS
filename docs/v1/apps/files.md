# Files: plan (GLOS v1, phase A)

**Today:**
- **The desktop's shell:** the desktop's icons, the launcher, Ctrl+Alt+Del's task list.
- **Folders:** each in a window of its own (icons), and a browser (`FILES -browse`: a tree and a list).
- **Opening a file:** by `[open]`'s kinds.

A folder is read whole before anything is shown. No toolbar, no archives, 8.3 names only.

**In v1:** the file manager people expect from Windows 98 to 10, still the desktop's shell, on a 486.

## What it does

- **One window kind, the browser:** a toolbar, an address bar, the tree of folders and drives on the left, the
  folder's contents on the right, a status bar. Spatial folder windows stay as an option (a setting), as D33 has
  them.
- **The toolbar:** Back, Forward, Up, Refresh; New folder; Cut, Copy, Paste, Delete, Rename; the view; Search. Each
  with a tooltip and a key.
- **The address bar:** the folder's path to type into, with completion, and a drop-down of recent folders. Clicking a
  part of the path goes there.
- **Views:**
  - large icons, small icons, a list, details;
  - details: name, size, type, date, attributes, with columns sorted by a click and resized by dragging;
  - thumbnails for pictures, made in the background (Paint's decoders), kept in a cache folder.
- **Folders that load as they go:** a folder of thousands of files shows its first screen at once. The rest arrive
  in the background, the scroll bar growing. Sizes and icons are filled in last.
- **Long file names** where DOS has an LFN driver (API 2.0's FS), 8.3 otherwise.
- **File operations:**
  - copy, move, delete (to a Recycle folder, a setting), rename;
  - in the background, with a progress window (time left, cancel) and questions for clashes (replace, skip, keep
    both, for all);
  - drag and drop within Files, between windows, to and from other apps (a file onto Notepad opens it).
- **The context menu** (right click, the Menu key): open, open with, cut, copy, paste, delete, rename, properties;
  for archives: extract here, extract to; "send to" a floppy or a ZIP. Apps register their own entries (SHELL).
- **Archives as folders:**
  - **ZIP:** read and written: browse, open a file (extracted to a temporary folder), drag files in and out, make
    one from a selection.
  - **Read only:** TAR, GZ (and TAR.GZ), LZH/LHA, ARJ, 7z.
  - **Big archives:** listed as they load, like folders.
- **Search:** by name (wildcards) and, optionally, by text inside files; results as a folder view, arriving as
  found, with the search stoppable.
- **Properties:** size (a folder's counted in the background), dates, attributes; a drive's label and free space; a
  picture's size; an archive's contents.
- **Drives:**
  - floppies, hard disks, CD-ROMs (MSCDEX), network drives where DOS has them;
  - free space as a bar;
  - a floppy's disk changed is noticed.
- **The shell's part** (`shell.md`): the desktop's icons and wallpaper, the Start menu (Files gives the Start menu
  its list of apps), "open with", the Recycle folder's icon on the desktop.

## How it looks

```
+-------------------------------------------------------------+
| File  Edit  View  Go  Tools  Help                           |
| [<][>][^][R] | [New][Cut][Copy][Paste][Del] | [View v][Find]|
| Address: [ C:\GAMES\DOOM                              v ]   |
+----------------+--------------------------------------------+
| > C:           | Name          Size   Type     Modified     |
|   > GAMES      | DOOM.EXE    709 KB  Program  1994-10-17   |
|     > DOOM     | DOOM.WAD   11.1 MB  File     1994-10-17   |
|   > GLOS       | ...                                        |
| > D: (CD)      |                                            |
+----------------+--------------------------------------------+
| 23 items, 12.4 MB          | C: 1.2 GB free                 |
+-------------------------------------------------------------+
```

## From toolkit 2

- **Panes:** the toolbar (icons, tooltips, overflow), the address bar (a field with completion and a drop-down), the
  splitter, the status bar's sections.
- **Lists:** the icon view and detail view (columns sorted and resized, rubber-band selection); lists and trees
  that grow as items arrive (the virtual list).
- **Menus and dialogs:** the context menu; the progress dialog; the clash question ("for all").
- **Interaction:** drag and drop (the source and the target, the drop's feedback); a background-work helper on the
  app's threads.

## From API 2.0

- **FS:**
  - long file names;
  - a folder read a part at a time;
  - file information (sizes, dates, attributes);
  - drives, their labels, free space, a changed disk;
  - watching folders (today's GLOS_FN_WATCH).
- **THREAD:** listing, thumbnails, copying and archive work off the window's thread.
- **DND:** files dragged between Files' windows and to other apps.
- **CLIP:** a list of files (copy and paste between windows and apps).
- **SHELL:** the desktop window, the Start menu's entries, file associations and "open with", the context menu's
  entries from other apps.
- **JOB:** opening a file with a program, running a DOS program or batch file.
- **SETTINGS:** the view's and the Recycle folder's settings, and their change event.

## Libraries

- **ZIP:** miniz (MIT), reading and writing.
- **7z:** the LZMA SDK's decoder (public domain), read only.
- **TAR and GZ:** TAR GLOS's own; GZ by miniz's inflate.
- **LZH/LHA and ARJ:** GLOS's own decoders, written from the formats' public descriptions.
- **Pictures:** stb_image (as Images today) for thumbnails.

## CPU floor

- **A 486DX2** for everything but thumbnails of big JPEGs and 7z archives, which are slow there but work.
- **A Pentium** for those to feel quick.

## Tests

- **Host:**
  - each archive format's reader on test archives (made on the host by the reference tools, kept in the repo);
  - the ZIP writer's output read back by `unzip -t`;
  - GUI scripts: the views, the toolbar, the address bar, the context menu, a big folder's first screen, a copy with
    a clash.
- **Loop A smoke:** a folder of 5,000 files shows its first screen within a second of the window opening; a ZIP made
  and extracted; a copy cancelled half-way leaves no partial file; drag and drop to Notepad.
- **The P4:** the same with real disks, thumbnails of a folder of photos, the 486-class timings measured on 86Box's
  486 for the floor.

## Open points

Settled at the owner's review (2026-11-06):
- **The Recycle folder:** on by default, capped at 10% of each drive (the oldest emptied first); Shift+Del deletes
  for real. DOS programs delete for real either way.
- **Long file names:** through DOS's LFN API, with GLOS's boot image loading FreeDOS's DOSLFN by default
  (`api-2.md`, FS).
