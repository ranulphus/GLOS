# Disk Tools

Disk Tools works on whole disks: floppies and hard disks as the PC's BIOS has them, and images of them in
files.

The list at the top has the floppy drives and the hard disks; under it are the chosen hard disk's partitions.
A floppy is not read until you ask: **F5** looks at the disk in the drive.

**The disk GLOS runs from is never written,** whatever you choose.

## A disk's image

- **Disk, Image of the Disk** (Ctrl+S) reads every sector of the disk into a file. A sector that cannot be
  read is tried again; if it stays bad it is zeros in the image, and you are told which sectors they were.
- **Disk, Write an Image to the Disk** (Ctrl+W) writes an image file over the disk and reads it back to see
  that it is there. The image must be exactly the disk's size. **Everything on the disk is replaced:** you are
  asked first, and twice for a hard disk. After writing a hard disk, restart the PC before DOS uses it.
- **Disk, Check the Disk** (Ctrl+K) reads every sector and tells you which could not be read.

A floppy takes about a minute to read and two to write and read back. Cancel stops a job; an image that was
not finished is removed.

## Floppies

- **Disk, Format** makes an empty DOS disk of the floppy, with a label if you give one, at any size its drive
  makes (a 1.44 MB drive: 1.44 MB or 720 KB). A quick format only makes the file system anew; **Full format**
  makes every track anew and reads it back, and keeps bad sectors out of use. A brand new disk, a damaged one
  or one of another size needs a full format (Disk Tools chooses it then). A full format of a 1.44 MB floppy
  takes two or three minutes.
- **Disk, Look Inside** (Enter) shows the floppy's folders and files without DOS.

## Inside an image

**Image, Open an Image** (Ctrl+O) shows what is in an image file of a DOS disk. Enter opens a folder,
Backspace goes up. **Copy Out** copies the chosen files and folders to a folder on your disk; **Copy
Everything Out** copies it all.

**Image, New Image from a Folder** (Ctrl+N) makes a floppy image (360 KB to 2.88 MB) holding a folder's
files, ready to write to a floppy or to give to an emulator.

## Backups on another computer

Disk Tools can keep copies of a folder on another computer, over SSH (SFTP), and bring them back.

- **Backup, Back Up a Folder** (Ctrl+B): the **Folder** here; the **Computer**, by the name of a session
  saved in the Terminal's Sessions (its address, user and key) or as `user@computer:port`; the **Folder
  there** that keeps this folder's backups; and **What**: every file, or what changed since the last backup
  (by size and date). Each backup is a folder there named by its date and time. **Keep** thins out the older
  backups after each new one, keeping the newest 5, 10 or 30 (files a newer backup still needs stay where they
  are, so every backup kept can still be restored whole). **Again** makes GLOS do the
  same backup by itself each time it starts, every day, or every week, at the **Time** (and **On day**) given:
  **Backup, Scheduled Backups** lists those, and **Remove** stops one.
- **Backup, Restore a Backup** (Ctrl+R): the computer and the folder there; then choose one of the backups
  (the newest first) and the folder to put it in. Files of the same names there are written over.

Every backup can be restored whole on its own, even one of what changed: it knows where the other files
are. A backup that was stopped or failed is not listed.

The computer must have been met before (Disk Tools asks the first time, as SSH does). From a batch file or a
schedule, without a window:

    DISKS -backup C:\DATA Box glos-backups/DATA -changes -keep 10
    DISKS -restore Box glos-backups/DATA newest C:\DATA

## Not yet

Disks larger than 2 GB cannot be read to an image yet. Backups are files, not ZIPs. A floppy of another size than its drive's (a 720 KB
disk in a 1.44 MB drive) is not read or written yet.
