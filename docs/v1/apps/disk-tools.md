# Disk Tools: plan (GLOS v1, phase A)

**New in v1.** The disk jobs retro PCs still need: floppies to images and back, formatting, a disk's image to a file,
and backups over the network to an SFTP server.

## What it does

- **Floppies:**
  - **To an image:** read a floppy (360 KB to 2.88 MB) to an image file (IMG/IMA), with bad sectors retried and
    reported.
  - **From an image:** write an image to a floppy, verified after.
  - **Format:** with a label, quick or full, and the system copied where DOS's SYS can do it (by running it).
  - **Check:** read every sector and report bad ones.
- **Images:**
  - open an image in Files as a folder (FAT12/16 read, through GLOS's own FAT reader: a small one, read only in v1);
  - make an image from a folder's files (FAT12 written into a new image).
- **Hard disks and CF cards:**
  - **A disk's image to a file:** read through INT 13h, written in pieces (to another disk or over SFTP, since the
    file may be bigger than DOS allows: split into 2 GB parts).
  - **An image back to a disk:** with two confirmations and the disk named by size and model, never the disk GLOS
    is on unless the user insists.
  - **Partitions:** listed (MBR) with their types and sizes. Changing partitions isn't planned.
- **Backups over SFTP:**
  - folders backed up to an SFTP server: full, or only what changed since the last (by size and date), as ZIPs or
    as files;
  - scheduled (daily, weekly, at GLOS's start), with the server's address, user and key (the key in SECRETS);
  - restore: browse a backup and copy files back.
- **Long jobs:** run in the background with progress, cancelled cleanly, with a notification when done.

## From toolkit 2

- **Choosing a job:** a wizard or step dialog.
- **Progress:** the progress dialog, with a log pane for a job's details (bad sectors, files skipped).
- **Lists:** the drives and partitions (the detail view).

## From API 2.0

- **FS:**
  - drives and their types (floppy, hard disk, CD, network) and sizes;
  - raw sector access through INT 13h for disks (a call that runs INT 13h for the app, as the DOS server does,
    with the disk cache told);
  - long file names.
- **THREAD:** the jobs off the window's thread.
- **NET:** SFTP to a server. GLOS's SSH client (Term's `ssh.c`) becomes a library apps can use.
- **SECRETS:** the server's key and password.
- **JOB:** DOS's FORMAT and SYS run as jobs where GLOS doesn't do the work itself.
- **SHELL:** notifications for a finished job; a scheduled backup's start (GLOS runs it at its time: a small
  scheduler in the shell, or a `[desktop] start` entry with a schedule, settled in `api-2.md`).

## Libraries

- **ZIP:** miniz.
- **SSH and SFTP:** GLOS's own SSH layer and TinySSH's crypto (as Term's).
- **FAT:** GLOS's own reader (the PRD's "GLOS's own FAT driver" is post-v1; this is a small reader for images only).

## CPU floor

A 486DX2: floppy and disk jobs are slow for the drives' sake. SFTP backups on a 486 go at about 0.6 MB/s
(ChaCha20-Poly1305, PRD §1).

## Tests

- **Host:** the FAT reader and writer on generated images (checked with mtools); the backup's change detection on a
  fake tree; GUI scripts.
- **Loop A smoke:** a floppy image written to 86Box's floppy and read back identical; a folder backed up to an SFTP
  server on the host and restored.
- **The P4:** a real floppy if it has a drive; a CF card's image over SFTP.

## Open points

- **Raw disk writes:** writing a whole disk is the most dangerous thing any GLOS app can do. Should Disk Tools offer
  it at all in v1, or only reading disks to images?
