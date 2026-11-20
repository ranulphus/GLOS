#!/usr/bin/env python3
"""Native GLOS's DOS checklist (docs/v1/native-dos.md, N0 and N2): every DOS function in Ralf Brown's Interrupt
List with what GLOS's DOS is to do about it, and whether the census saw a program ask for it.

The owner's choice (2026-11-20): the scope is every INT 21h and INT 2Fh function in RBIL, those of features GLOS
doesn't have answered as MS-DOS 7.1 answers them when the feature isn't loaded; the census only sets the order.

    make rbil                                  (fetches RBIL release 61 into the cache, then runs this)
    tools/census/rbil.py --rbil DIR [--out FILE]

Read from RBIL (INTERRUP.A to .R): the entries of category D (DOS kernel) for INT 20h-2Fh; the disk BIOS's standard
functions (INT 13h), Microsoft's mouse driver's (INT 33h), LIM EMS's and VCPI's (INT 67h), which native GLOS
answers itself (its own IDE driver, mouse and EMS: the owner, 2026-11-20); and for INT 2Fh also the multiplex functions of DOS's own parts and of what GLOS provides in their place (Windows' and
DPMI's 16xxh, XMS, MSCDEX, the clipboard, KEYB, DOSKEY, ANSI.SYS, APPEND, the task switcher). An entry applies to
GLOS's DOS when RBIL gives it to MS-DOS ("DOS n+", "MS-DOS 7", "Windows95" for MS-DOS 7's long names and FAT32);
entries of other systems (DR DOS, OS/2, PC-MOS, European MS-DOS 4 ...) are listed apart.

Status, proposed (the owner reviews; OVERRIDES below holds the hand-made ones):
  implement  GLOS's DOS answers it as MS-DOS 7.1 does
  absent     answered as MS-DOS 7.1 answers it when the feature isn't loaded (no network server, no NLSFUNC ...)
  null       DOS's own null or reserved function: answered as DOS does
  callout    DOS (or Windows) calls it, programs answer it: GLOS's DOS makes DOS's calls (INT 23h, 24h, 28h, 2Ah), not
             Windows' (GLOS isn't Windows)
  internal   DOS's internal services for TSRs and redirectors (INT 2Fh 12xxh): after the core
  other      another system's: not in GLOS's DOS
RBIL itself is not copied: its titles are quoted, one line each (RBIL's licence; credit in the page)."""
import argparse
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import report  # noqa: E402  (the census's results)

SEP = re.compile(r"^--------(.)-([0-9A-F]{2})([0-9A-F]{2})?([0-9A-F]{2})?(\S*?)-*\s*$")
TITLE = re.compile(r"^INT ([0-9A-F]{2})\s*([A-Z]*)\s+-\s+(.*)$")

APPLIES = re.compile(r"\bDOS \d+(\.(\d+|x))?\+|MS-DOS 7|Windows95|\bDOS 7")
OTHER = re.compile(r"DR[ -]DOS|Novell|PC-MOS|PTS-DOS|ROM-DOS|OS/2|European|Concurrent|Multiuser|FlexOS|REAL/32|"
                   r"PalmDOS|Windows NT|Win32|DOS v2\.1|NewDOS|\b4DOS\b|\bNDOS\b|DOS 2\.25 only|DOS 4\.0 only|"
                   r"DOS 3\.3x only|DOS 5\.0 only|DOS 6\.0 only|Toshiba|Tandy|Wang|Compaq|Zenith|AT&T|DOS 2\.0 only|"
                   r"Arabic|Hebrew|ANSIPLUS|PCED")
ABSENT = re.compile(r"network|NETWORK|SERVER|[Ss]erver|REDIRECT|[Rr]edirect|PRINT\b|PRINT\.COM|printer|PRINTER|"
                    r"NLSFUNC|APPEND|ASSIGN|DOSKEY|DISPLAY\.SYS|GRAFTABL|DRIVER\.SYS|MACHINE NAME|machine name|"
                    r"SHARE|GRAPHICS\.COM|MODE\.COM|DBLSPACE|DRVSPACE|DoubleSpace|DriveSpace|Stacker|MSCDEX|"
                    r"CD-ROM|INTERLNK|EMM386|SMARTDRV|POWER\b|task switcher|TASK SWITCHER|WIN\.COM|WSWAP|BOOT LOG|"
                    r"DOSSHELL|SELECT|Mach 20|- \?\?\?\s*$")
NULL = re.compile(r"NULL FUNCTION|UNUSED|RESERVED|CP/M")

# INT 2Fh multiplex numbers read beyond category D: what GLOS's DOS answers in place of DOS's own parts and
# Windows (16h), XMS (43h), MSCDEX (15h), the clipboard (17h), ANSI.SYS (1Ah), DOSKEY (48h), the HMA (4Ah), the
# task switcher (4Bh), KEYB (4Fh, ADh), COMMAND.COM (55h, AEh), APPEND (B7h).
MUX = {0x15, 0x16, 0x17, 0x1A, 0x43, 0x48, 0x4A, 0x4B, 0x4F, 0x55, 0xAD, 0xAE, 0xB7}
MUX_TITLE = re.compile(r"MS Windows|Windows95|DPMI|EXTENDED MEMORY SPECIFICATION|HIMEM\.SYS|MSCDEX|CD-ROM|"
                       r"KEYB\.COM|DOSKEY|ANSI\.SYS|^APPEND|APPEND -|COMMAND\.COM|WINOLDAP|TASK SWITCHER|DOSSHELL|"
                       r"^DOS \d|^MS-DOS")
# Windows' 16xxh that GLOS answers as GLOS (not Windows): the others are Windows' own (absent or callouts).
WIN_ANSWERED = {(0x16, 0x00), (0x16, 0x0A), (0x16, 0x80), (0x16, 0x81), (0x16, 0x82), (0x16, 0x83), (0x16, 0x84),
                (0x16, 0x86), (0x16, 0x87), (0x16, 0x89), (0x16, 0x8F)}

# Vectors beyond DOS's that native GLOS answers itself (the owner, 2026-11-20: GLOS's own IDE driver, the mouse,
# EMS): the disk BIOS's standard functions, Microsoft's mouse driver's, LIM EMS's and VCPI's.
BIOSLIKE = {
    0x13: re.compile(r"(DISK|HARD DISK|FIXED DISK|FLOPPY DISK|IBM/MS INT 13 Extensions) - "),
    0x33: re.compile(r"MS MOUSE"),
    0x67: re.compile(r"LIM EMS|Virtual Control Program Interface"),
}

# Hand-made statuses: (int, ah, al or None) -> (status, note).
OVERRIDES = {
    (0x2F, 0x10, 0x00): ("implement", "sharing is built in: SHARE is there"),
    (0x2F, 0x11, 0x00): ("absent", "no redirector (GLOS's network drives are its own file system's)"),
    (0x2F, 0x43, 0x00): ("implement", "GLOS's XMS server (there today)"),
    (0x2F, 0x43, 0x10): ("implement", "GLOS's XMS server (there today)"),
    (0x2F, 0x16, 0x87): ("implement", "GLOS's DPMI host (there today)"),
    (0x2F, 0x16, 0x80): ("implement", "the machine gives up its turn (idle)"),
    (0x2F, 0x16, 0x00): ("implement", "answered: not Windows"),
    (0x2F, 0x16, 0x0A): ("implement", "answered: not Windows"),
    (0x2F, 0x4A, 0x01): ("implement", "the HMA, GLOS's in each machine"),
    (0x2F, 0x4A, 0x02): ("implement", "the HMA, GLOS's in each machine"),
    (0x2F, 0x17, 0x00): ("implement", "the clipboard (Beyond DOS: WinOldAp's calls)"),
    (0x21, 0x5D, 0x06): ("implement", "the swappable data area, real (RTM, Half-Life)"),
    (0x21, 0x5D, 0x0A): ("implement", None),
    (0x21, 0x5D, 0x0B): ("absent", "DOS 4.x only: MS-DOS 5+ answers it with an error (5D06h is the SDA)"),
    (0x21, 0x72, None): ("absent", "Windows 95's beta only"),
    (0x23, None, None): ("callout", "GLOS's DOS calls the program's handler at Ctrl-C, as DOS does"),
    (0x24, None, None): ("callout", "GLOS's DOS calls the program's handler at a critical error, as DOS does"),
    (0x28, None, None): ("callout", "GLOS's DOS calls it while it waits for a key, as DOS does (TSRs pop up from it)"),
    (0x29, None, None): ("implement", "CON's fast output: programs call it too"),
    (0x2A, 0x80, None): ("callout", "DOS calls it around its critical sections, for network software: GLOS's DOS too"),
    (0x2A, 0x81, None): ("callout", "as 80h"),
    (0x2A, 0x82, None): ("callout", "as 80h"),
    (0x21, 0x63, 0x01): ("absent", "not a Korean DOS"),
    (0x21, 0x63, 0x02): ("absent", "not a Korean DOS"),
    (0x21, 0x5F, 0x02): ("absent", "no redirections: error 12h (no more files)"),
    (0x21, 0x5F, 0x07): ("implement", None),
    (0x21, 0x5F, 0x08): ("implement", None),
    (0x21, 0x61, None): ("null", None),
    (0x21, 0x05, None): ("implement", "to PRN: the BIOS's LPT1 (GLOS's spooler later: Beyond DOS)"),
    (0x2F, 0x16, 0x84): ("implement", "answered as GLOS: no VxD (ES:DI 0), whichever BX"),
    (0x2F, 0x13, None): ("absent", "GLOS's DOS doesn't read disks through the machine's INT 13h"),
    (0x2F, 0x1A, 0x00): ("later", "ANSI.SYS: CON interprets ANSI's escapes, with the console"),
    (0x2F, 0x43, 0x09): ("implement", "GLOS's XMS server's handle table"),
    (0x2F, 0x4A, 0x33): ("implement", "answered: MS-DOS 7"),
    (0x2F, 0x55, 0x00): ("absent", "GLOS's COMMAND.COM needs none"),
    (0x2F, 0x55, 0x01): ("absent", "GLOS's COMMAND.COM needs none"),
    (0x2F, 0xAD, 0x40): ("absent", None),
    (0x2F, 0x43, 0x08): ("absent", "HIMEM.SYS's own"),
    (0x21, 0x6D, None): ("absent", "ROM DOS's"),
    (0x21, 0x6E, None): ("absent", "ROM DOS's"),
    (0x2F, 0x4A, 0x32): ("absent", None),
}
for al in range(0x00, 0x04):
    OVERRIDES[(0x21, 0x6F, al)] = ("absent", "ROM DOS's")
# INT 13h on GLOS's IDE disks: what an AT BIOS of the 1990s answers; the XT's, PS/2's and ESDI's functions are
# refused as such BIOSes refuse them (AH 01h, invalid function); the floppy's own go to the BIOS.
for ah in (0x05, 0x06, 0x07, 0x0A, 0x0B, 0x0E, 0x0F, 0x12, 0x13, 0x14, 0x19, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25):
    OVERRIDES[(0x13, ah, None)] = ("absent", "IDE disks: AH 01h (invalid function), as AT BIOSes answer")
OVERRIDES[(0x13, 0x09, None)] = ("implement", "IDE disks: nothing to do, success")
for ah in (0x16, 0x17, 0x18):
    OVERRIDES[(0x13, ah, None)] = ("implement", "floppies: the BIOS")
OVERRIDES[(0x67, 0x49, None)] = ("absent", "reserved in EMS 4.0")
OVERRIDES[(0x67, 0x4A, None)] = ("absent", "reserved in EMS 4.0")
OVERRIDES[(0x33, 0x01, 0x2E)] = ("absent", None)
OVERRIDES[(0x33, 0x02, 0x2E)] = ("absent", None)
for al in range(0xA2, 0xA6):
    OVERRIDES[(0x21, 0x71, al)] = ("absent", "Windows 95's internal")
for al in range(0x80, 0x86):
    OVERRIDES[(0x2F, 0xAD, al)] = ("implement", "KEYB's calls, with the keyboard's layout (N4)")
for al in range(0x00, 0x100):
    OVERRIDES.setdefault((0x2F, 0x15, al), ("implement", "MSCDEX's calls, with GLOS's CD-ROM (N4)"))


def parse(rbil):
    """[(int, ah, al, qual, cat, flags, title)] for the entries in scope."""
    out = []
    files = sorted(f for f in glob.glob(os.path.join(rbil, "INTERRUP.?")) if f[-1] in "ABCDEFGHIJKLMNOPQR")
    if not files:
        sys.exit("no RBIL in %s (make rbil fetches it)" % rbil)
    for fn in files:
        lines = open(fn, "rb").read().decode("latin-1").split("\n")
        for i, l in enumerate(lines[:-1]):
            m = SEP.match(l)
            if not m:
                continue
            cat, vec, ah, al, qual = m.group(1), int(m.group(2), 16), m.group(3), m.group(4), m.group(5)
            t = TITLE.match(lines[i + 1].rstrip("\r"))
            if not t or (not ah and vec in (0x13, 0x21, 0x2F, 0x33, 0x67)):
                continue
            ah = int(ah, 16) if ah else None
            al = int(al, 16) if al else None
            title = t.group(3).strip()
            if vec in BIOSLIKE:
                if not BIOSLIKE[vec].match(title):
                    continue
            elif not 0x20 <= vec <= 0x2F:
                continue
            elif cat != "D" and not (vec == 0x2F and ah in MUX and MUX_TITLE.search(title)):
                continue
            if vec == 0x2F and ah == 0x16 and m.group(5):
                continue                    # one VxD's API (1607h, 1684h BX=id): answered as their function's

            out.append((vec, ah, al, qual, cat, t.group(2), title))
    return out


def status(vec, ah, al, flags, title):
    o = OVERRIDES.get((vec, ah, al))
    if o:
        return o
    if vec == 0x13:
        return ("implement", "IDE disks: GLOS's own driver answers it; floppies and other disks: the BIOS")
    if vec == 0x33:
        return ("implement", "GLOS's mouse (N4)")
    if vec == 0x67:
        if title.startswith("Virtual Control Program Interface"):
            return ("absent", "no VCPI: GLOS is the DPMI host (PRD D25)")
        return ("implement", "GLOS's EMS (N4)")
    if not APPLIES.search(title) or OTHER.search(title):
        if vec == 0x2F and ah == 0x16 and "Windows" in title:
            pass                                    # Windows' own: judged below
        elif vec == 0x2F and ah in MUX and MUX_TITLE.search(title) and not OTHER.search(title):
            pass                                    # DOS's parts named by their product (KEYB, DOSKEY ...)
        else:
            return ("other", None)
    if "C" in flags:
        return ("callout", None)
    if vec == 0x2F and ah == 0x16:
        return ("implement", "answered as GLOS, not Windows") if (ah, al) in WIN_ANSWERED else \
            ("absent", "Windows' own: no Windows")
    if NULL.search(title):
        return ("null", None)
    if vec == 0x2F and ah == 0x12:
        return ("internal", None)
    if ABSENT.search(title):
        return ("absent", None)
    return ("implement", None)


def code(vec, ah, al, qual):
    if ah is None:
        return "%02Xh" % vec + (" " + qual if qual else "")
    s = "%02Xh %02X%sh" % (vec, ah, "%02X" % al if al is not None else "")
    return s + (" " + qual if qual else "")


def census_seen():
    """{(vec, ax): jobs} that programs asked for (the census, on plain DOS or under GLOS)."""
    seen, _, _ = report.gather()
    return {k: len(s["pjobs"]) for k, s in seen.items() if s["pjobs"]}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rbil", required=True)
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "v1", "native-dos-rbil.md"))
    a = ap.parse_args()
    entries = parse(a.rbil)
    seen = census_seen()
    groups = {}
    for vec, ah, al, qual, cat, flags, title in entries:
        groups.setdefault((vec, ah, al, qual), []).append((cat, flags, title))
    rows, counts = [], {}
    for (vec, ah, al, qual), es in sorted(groups.items(), key=lambda kv: (kv[0][0], -1 if kv[0][1] is None else
                                                                            kv[0][1], -1 if kv[0][2] is None
                                                                            else kv[0][2], kv[0][3])):
        # The entry that applies to MS-DOS first, else the first.
        best = None
        for cat, flags, title in es:
            st = status(vec, ah, al, flags, title)
            if best is None or (best[0][0] == "other" and st[0] != "other"):
                best = (st, flags, title)
        (st, note), flags, title = best
        n = seen.get((vec, 0)) if ah is None else \
            seen.get((vec, (ah << 8) | (al or 0))) or (seen.get((vec, ah << 8)) if al is None else None)
        counts[st] = counts.get(st, 0) + 1
        rows.append((code(vec, ah, al, qual), title.replace("|", "/"), "U" in flags, n, st, note or ""))
    lines = ["# Native GLOS's DOS: the checklist from RBIL", "",
             "Generated by `tools/census/rbil.py` (`make rbil`) from Ralf Brown's Interrupt List, release 61 "
             "(Ralf Brown and its contributors; www.cs.cmu.edu/~ralf/files.html), with the census's results "
             "([native-dos-census.md](native-dos-census.md)). See [native-dos.md](native-dos.md), N0 and N2.", "",
             "**Scope (the owner, 2026-11-20):** every DOS function in RBIL; those of features GLOS doesn't have "
             "answered as MS-DOS 7.1 answers them when the feature isn't loaded. The census sets the order: "
             "functions programs asked for come first in each area.", "",
             "**Proposed status** (for the owner's review; `OVERRIDES` in the script holds the hand-made ones): "
             "**implement**; **absent** (answered as without the feature); **null** (DOS's own null or reserved "
             "function); **callout** (DOS or Windows calls it and programs answer: GLOS's DOS makes DOS's, not Windows'); **internal** (INT 2Fh 12xxh, for "
             "TSRs and redirectors: after the core); **later** (ANSI.SYS, with the console); **other** (another "
             "system's).", "",
             "**Counts:** " + ", ".join("%s %d" % (k, counts[k]) for k in sorted(counts)) +
             " (%d entries; a function with entries for several systems is counted once)." % len(rows), "",
             "U: undocumented (RBIL's mark). Census: how many jobs' programs asked for it.", ""]
    cur = None
    for c, title, und, n, st, note in rows:
        vec = c[:3]
        if vec != cur:
            cur = vec
            lines += ["", "## INT %s" % vec, "", "| Function | RBIL | U | Census | Status | Note |",
                      "|---|---|---|---|---|---|"]
        lines.append("| %s | %s | %s | %s | %s | %s |" % (c, title, "U" if und else "", n or "", st, note))
    open(a.out, "w").write("\n".join(lines) + "\n")
    print("%s: %d entries; %s" % (os.path.relpath(a.out, ROOT), len(rows),
                                  ", ".join("%s %d" % (k, counts[k]) for k in sorted(counts))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
