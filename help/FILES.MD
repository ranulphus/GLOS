# Files

Files shows what is on your disks and lets you open, copy, move, rename and delete it. It comes in two shapes:

- **A folder's own window**, when you open a drive or a folder from the desktop. Each folder remembers where
  its window was and how it showed its files.
- **The browser** (Start, Files): a toolbar, an address bar, a tree of drives and folders at the left, and the
  folder's files at the right. Press a column's head (Name, Size, Modified) to sort by it.

## Opening things

Double-click, or press Enter.

- A program (EXE, COM) is run in its own folder. See [DOS programs and games](DOSPROGS.MD).
- A batch file (BAT) opens in the [Terminal](TERM.MD).
- Pictures open in [Images](IMAGES.MD), PDF files in the [PDF Viewer](PDF.MD), music and sounds in the
  [Player](PLAYER.MD), archives in the browser, and everything else in [Notepad](NOTEPAD.MD).
- **Open With...** (File menu) chooses another program, and can make that the rule for files of that kind. The
  rules are also on the Control Panel's File Types page.

## Copying, moving and deleting

| Keys | What they do |
|---|---|
| Ctrl+C, Ctrl+X, Ctrl+V | Copy, cut and paste files |
| Ctrl+A | Choose everything |
| F2 | Rename |
| Del | Move to the Recycle folder |
| Shift+Del | Delete for good |
| Ctrl+Shift+N | New folder |
| Alt+Enter | Properties |
| F5 | Read the folder again |
| Backspace | Up a folder |
| Ctrl+F | Find files |
| Alt+Left, Alt+Right, Ctrl+L | The browser's Back, Forward and address bar |

- Long jobs run in a small window of their own with the time left and a Cancel button, while you go on working.
- If a file of the same name is there already you are asked: Replace, Skip, or Keep Both.
- Files are cut and copied within Files; they are not dragged from one window to another.

## The Recycle folder

Del moves files to the Recycle folder of their drive, from which they can be had back: Go, Recycle Folder,
then Restore. It keeps at most a tenth of the drive, the oldest going first. Files on floppies and inside
archives, and anything deleted with Shift+Del or by a DOS program, are gone at once.

## Archives

ZIP, TAR, GZ, TGZ, LZH, ARJ and 7z archives are entered like folders in the browser. **Extract Here** and
**Extract To...** unpack them. **Add to ZIP** packs what is chosen into a new ZIP; inside a ZIP you can also
paste, rename, delete and make folders. Archives of the other kinds are read, not written. Files locked with a
password are listed but cannot be unpacked.

## Views, finding and properties

- **View:** large icons, small icons, thumbnails (small pictures of pictures), details.
- **Find Files** (Ctrl+F) looks from the folder shown downward, for a piece of a name (with * and ?) or for
  words inside files.
- **Properties** (Alt+Enter) shows a file's size, date and attributes, a folder's contents, and the drive's
  free space.

## Another computer's folders

Type `sftp://user@computer/folder` in the address bar, or choose Go, Connect to a Computer. You can enter
folders, open files, copy to this PC and paste from it, make folders, rename and delete (for good). See
[The network](NETWORK.MD).
