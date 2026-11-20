#!/usr/bin/env python3
"""The INT 21h census's report (docs/v1/native-dos.md, N0): which DOS services the acceptance programs ask for.

Reads the GLOS-CENSUS lines CENSUS.EXE (tools/census/census.c) wrote into each survey job's serial.log
(out/survey/JOB/census*/serial.log: `tools/survey/survey.py run --variant census`) and writes
docs/v1/native-dos-census.md: every vector and function seen, which jobs and programs asked for it, how often, and
from where (P the program's own memory, D DOS's kernel, O elsewhere: a DPMI host reflecting a protected-mode call,
a TSR, DOS's own code outside its INT 21h segment).

    tools/census/report.py [--out FILE] [--print]

A call is known from its first use (a "new" line) even where the job ended before the program did (Loop A stops
the PC at HX-DONE); its count is known only when the program ended inside the run ("n=" lines). The harness's
own programs (SERSAY, VECCHK and the rest), CENSUS, FreeDOS's COMMAND.COM and the drivers are listed apart: GLOS
answers for them differently (its own shell; its own mouse and long names)."""
import argparse
import collections
import glob
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

LINE = re.compile(r"GLOS-CENSUS (new )?prog=(\S+) psp=([0-9A-F]{4}) int=([0-9A-F]{2}) ax=([0-9A-F]{4}) "
                  r"from=([PDO])(?: n=(\d+))?")
END = re.compile(r"GLOS-CENSUS end prog=(\S+) psp=([0-9A-F]{4}) entries=(\d+) calls=(\d+) dropped=(\d+) "
                 r"top21=([0-9A-F:]+)")

HARNESS = {"CENSUS", "SERSAY", "VECCHK", "UTEXIT", "VMODE", "HXTOOLS", "all"}
SHELL = {"COMMAND"}
DRIVERS = {"DOSLFN", "CTMOUSE", "CWSDPMI"}

# INT 21h by AH (and AX where AL picks a subfunction: CENSUS keys those by AX).
I21 = {
    0x00: "terminate (CP/M style)", 0x01: "read key with echo", 0x02: "write character", 0x06: "direct console I/O",
    0x07: "read key, no echo, no ^C", 0x08: "read key, no echo", 0x09: "write $-string", 0x0A: "buffered input",
    0x0B: "key waiting?", 0x0D: "disk reset", 0x0E: "select drive", 0x0F: "FCB open", 0x10: "FCB close",
    0x11: "FCB find first", 0x12: "FCB find next", 0x13: "FCB delete", 0x14: "FCB sequential read",
    0x15: "FCB sequential write", 0x16: "FCB create", 0x17: "FCB rename", 0x19: "current drive", 0x1A: "set DTA",
    0x1B: "current drive's FAT info", 0x1C: "drive's FAT info", 0x1F: "current drive's DPB (internal)",
    0x21: "FCB random read", 0x22: "FCB random write", 0x23: "FCB file size", 0x24: "FCB set record",
    0x25: "set vector", 0x26: "make PSP", 0x27: "FCB random block read", 0x28: "FCB random block write",
    0x29: "parse name into FCB", 0x2A: "get date", 0x2B: "set date", 0x2C: "get time", 0x2D: "set time",
    0x2E: "set verify", 0x2F: "get DTA", 0x30: "DOS version", 0x31: "stay resident", 0x32: "drive's DPB (internal)",
    0x34: "InDOS flag's address (internal)", 0x35: "get vector", 0x36: "free disk space", 0x37: "switch character",
    0x38: "country information", 0x39: "make directory", 0x3A: "remove directory", 0x3B: "change directory",
    0x3C: "create file", 0x3D: "open file", 0x3E: "close handle", 0x3F: "read", 0x40: "write", 0x41: "delete file",
    0x42: "seek", 0x45: "duplicate handle", 0x46: "force duplicate handle", 0x47: "current directory",
    0x48: "allocate memory", 0x49: "free memory", 0x4A: "resize memory", 0x4C: "terminate", 0x4D: "child's exit code",
    0x4E: "find first", 0x4F: "find next", 0x50: "set PSP (internal)", 0x51: "get PSP (internal)",
    0x52: "List of Lists (internal)", 0x53: "BPB to DPB (internal)", 0x54: "get verify", 0x55: "make child PSP",
    0x56: "rename", 0x59: "extended error", 0x5A: "create temporary file", 0x5B: "create new file",
    0x60: "canonical name (TRUENAME)", 0x62: "get PSP", 0x67: "set handle count", 0x68: "commit file",
    0x69: "volume serial (get/set)", 0x6A: "commit file", 0x6C: "extended open/create",
}
I21AX = {
    0x0C00: "flush input, then nothing", 0x0C01: "flush input, read key", 0x0C06: "flush input, console I/O",
    0x0C07: "flush input, raw key", 0x0C08: "flush input, read key no echo", 0x0C0A: "flush input, buffered input",
    0x3300: "get Ctrl-Break checking", 0x3301: "set Ctrl-Break checking", 0x3305: "boot drive",
    0x3306: "true DOS version", 0x4300: "get attributes", 0x4301: "set attributes", 0x4400: "IOCTL: device info",
    0x4401: "IOCTL: set device info", 0x4406: "IOCTL: input ready?", 0x4407: "IOCTL: output ready?",
    0x4408: "IOCTL: removable?", 0x4409: "IOCTL: remote drive?", 0x440A: "IOCTL: remote handle?",
    0x440D: "IOCTL: generic block request", 0x440E: "IOCTL: logical drive map",
    0x4411: "IOCTL: generic request supported? (handle)", 0x4B00: "EXEC: load and run",
    0x4B01: "EXEC: load, don't run", 0x4B03: "EXEC: load overlay", 0x5700: "get file date/time",
    0x5701: "set file date/time", 0x5800: "get allocation strategy", 0x5801: "set allocation strategy",
    0x5802: "get UMB link", 0x5803: "set UMB link", 0x5D06: "swappable data area (internal)",
    0x5D0A: "set extended error", 0x5D0B: "SDA list (internal)", 0x5E00: "machine name", 0x5F02: "redirection list",
    0x6300: "DBCS lead bytes", 0x6501: "extended country info", 0x6502: "uppercase table", 0x6503: "lowercase table",
    0x6504: "filename uppercase table", 0x6505: "filename character table", 0x6506: "collating table",
    0x6507: "DBCS table", 0x6520: "uppercase character", 0x6521: "uppercase string", 0x6522: "uppercase ASCIIZ",
    0x6601: "get code page", 0x6602: "set code page", 0x7139: "LFN make directory", 0x713A: "LFN remove directory", 0x713B: "LFN change directory",
    0x7141: "LFN delete", 0x7143: "LFN attributes", 0x7147: "LFN current directory", 0x714E: "LFN find first",
    0x714F: "LFN find next", 0x7156: "LFN rename", 0x7160: "LFN canonical/short/long name",
    0x716C: "LFN extended open/create", 0x71A0: "LFN volume information (is LFN there?)",
    0x71A1: "LFN find close", 0x71A6: "LFN file information by handle", 0x71A7: "LFN time conversion",
    0x71A8: "LFN short name for a long one", 0x7300: "FAT32 drive locking", 0x7302: "FAT32 extended DPB",
    0x7303: "FAT32 free space", 0x7304: "FAT32 set DPB", 0x7305: "FAT32 absolute sectors",
}
I2F = {
    0x1000: "SHARE installed? (DOS asks on opens)", 0x1100: "network redirector installed?",
    0x1123: "redirector: qualify name (DOS's own, on opens)", 0x1122: "redirector: process termination",
    0x111D: "redirector: close a process's files (DOS's own)", 0x1120: "redirector: flush buffers (DOS's own)",
    0x1180: "redirector", 0x1200: "DOS internal: installed?", 0x1500: "MSCDEX installed?",
    0x150B: "MSCDEX: CD drive check", 0x150C: "MSCDEX version", 0x150D: "MSCDEX drive letters",
    0x1510: "MSCDEX device request (CD audio)", 0x1600: "Windows enhanced mode?", 0x160A: "Windows version",
    0x1680: "release time slice (idle)", 0x1681: "begin critical section", 0x1682: "end critical section",
    0x1683: "VM id", 0x1684: "VxD entry point (Windows)", 0x1686: "in protected mode?", 0x1687: "DPMI host entry", 0x1689: "kernel idle",
    0x168F: "close-aware", 0x1700: "WinOldAp clipboard version", 0x1A00: "ANSI.SYS installed?",
    0x4300: "XMS installed?", 0x4310: "XMS entry point", 0x4A01: "HMA free space", 0x4A02: "HMA allocate",
    0x4A10: "SMARTDRV", 0x4F00: "keyboard (KEYB) installed?", 0x5500: "COMMAND.COM interface",
    0xAE00: "installable command check (COMMAND.COM)", 0xAE01: "installable command run",
    0xB700: "APPEND installed?", 0xFB42: "Borland RTM's own interface", 0xC7F0: "CENSUS dump", 0xC7F1: "CENSUS installed?",
}
I33 = {
    0x0000: "reset/detect", 0x0001: "show cursor", 0x0002: "hide cursor", 0x0003: "position and buttons",
    0x0004: "set position", 0x0005: "button presses", 0x0006: "button releases", 0x0007: "horizontal limits",
    0x0008: "vertical limits", 0x0009: "graphics cursor", 0x000A: "text cursor", 0x000B: "motion counters",
    0x000C: "event handler", 0x000F: "mickey ratio", 0x0011: "wheel/buttons (CuteMouse)", 0x0014: "swap handler",
    0x0015: "state size", 0x001A: "sensitivity", 0x001B: "get sensitivity", 0x0021: "software reset",
    0x0024: "driver version",
}
I67 = {0x4000: "EMS status", 0x4100: "page frame", 0x4200: "page counts", 0x4600: "EMS version",
       0xDE00: "VCPI installed?"}


def name(vec, ax):
    if vec == 0x21:
        return I21AX.get(ax) or I21.get(ax >> 8) or "?"
    if vec == 0x2F:
        return I2F.get(ax) or ("CENSUS" if ax >> 8 == 0xC7 else "?")
    if vec == 0x33:
        return I33.get(ax, "?")
    if vec == 0x67:
        return I67.get(ax) or I67.get(ax & 0xFF00, "?")
    return {0x20: "terminate (INT 20h)", 0x25: "absolute disk read", 0x26: "absolute disk write",
            0x27: "stay resident (INT 27h)"}.get(vec, "?")


def kind(prog):
    if prog == "GLOS":                  # GLOS.EXE's stub (its PSP): GLOS's own calls, under GLOS
        return "glos"
    if prog in HARNESS:
        return "harness"
    if prog in SHELL:
        return "shell"
    if prog in DRIVERS:
        return "driver"
    return "program"


def logs():
    """[(job, serial.log)]: the survey's census variants, and jobs.py's TPX, djtst and api runs made with CENSUS
    in LOOPA_EXTRA_ARGS (their GLOS runs have CENSUS under GLOS: job names with "glos", and api's)."""
    out = [(l.split(os.sep)[-3], l) for l in glob.glob(os.path.join(ROOT, "out", "survey", "*", "census*", "serial.log"))]
    for pat in ("tpx-*", "djtst-*", "api*"):
        for l in glob.glob(os.path.join(ROOT, "out", pat, "serial.log")):
            if "GLOS-CENSUS install" in open(l, "rb").read().decode("latin-1"):
                out.append((l.split(os.sep)[-2], l))
    return sorted(out)


def under_glos(job):
    return "-glos-" in job or job.startswith("api")          # jobs.py's api suite: GLOS apps, all under GLOS


def gather():
    """{(vec, ax): {"jobs": set, "progs": set, "from": set, "n": int, "unknown": bool, "kind": set}}, ends."""
    seen = collections.defaultdict(lambda: {"jobs": set(), "pjobs": set(), "progs": set(), "from": set(), "n": 0,
                                            "counted": set(), "kind": set()})
    ends, jobs = [], set()
    for job, log in logs():
        jobs.add(job)
        text = open(log, "rb").read().decode("latin-1")
        counted = {}
        for m in LINE.finditer(text):
            new, prog, psp, vec, ax, frm, n = m.groups()
            k = (int(vec, 16), int(ax, 16))
            s = seen[k]
            s["jobs"].add(job)
            s["progs"].add(prog)
            s["from"].add(frm)
            s["kind"].add("glos" if under_glos(job) else kind(prog))
            if kind(prog) == "program":
                s["pjobs"].add(job)
            if n is not None:
                counted[(prog, psp) + k + (frm,)] = int(n)
        for key, n in counted.items():
            s = seen[key[2:4]]
            s["n"] += n
            s["counted"].add(job)
        for m in END.finditer(text):
            ends.append((job,) + m.groups())
    return seen, ends, jobs


def report(out):
    seen, ends, jobs = gather()
    lines = ["# The INT 21h census (N0)", "",
             "Generated by `tools/census/report.py` from the survey's census runs (`tools/survey/survey.py run "
             "--variant census`; Half-Life's with DOSLFN under CENSUS, `census-lfn`). Each job ran on plain "
             "FreeDOS 1.4 in Loop A with CENSUS.EXE (`tools/census/census.c`) resident first. See "
             "[native-dos.md](native-dos.md).", "",
             "**Jobs (%d):** %s." % (len(jobs), ", ".join(sorted(jobs))), "",
             "Columns: the function; how many jobs' programs asked for it (any caller where no program did); the programs that did (on plain DOS or under GLOS); "
             "the shell, drivers and harness tools that did too (Also); from where (P the "
             "program's own memory, O elsewhere: a DPMI host reflecting a protected-mode call, a TSR, DOS's "
             "own code); calls counted (only for programs that ended inside their run: Loop A stops the PC "
             "when a program says HX-DONE, before it ends; `-` when none did).", ""]

    def table(title, keys):
        if not keys:
            return
        lines.extend(["## " + title, "", "| Vector | AX | Function | Jobs | Programs | Also | From | Calls |",
                      "|---|---|---|---|---|---|---|---|"])
        for k in sorted(keys):
            s = seen[k]
            progs = sorted(p for p in s["progs"] if kind(p) == "program")
            also = sorted(p for p in s["progs"] if kind(p) != "program")
            lines.append("| %02Xh | %04Xh | %s | %d | %s | %s | %s | %s |" % (
                k[0], k[1], name(*k), len(s["pjobs"] or s["jobs"]), ", ".join(progs[:8]) + (" ..." if len(progs) > 8 else ""),
                ", ".join(also), "".join(sorted(s["from"])), s["n"] if s["counted"] else "-"))
        lines.append("")

    progs = [k for k, s in seen.items() if "program" in s["kind"]]
    table("INT 21h: asked for by the programs on plain DOS", [k for k in progs if k[0] == 0x21])
    table("Other vectors the programs used on plain DOS", [k for k in progs if k[0] != 0x21])
    table("Only the shell, the drivers and the harness",
          [k for k, s in seen.items() if s["kind"] & {"shell", "driver", "harness"} and not s["kind"] & {"program"}
           and not (k[0] == 0x2F and k[1] >> 8 == 0xC7)])
    table("Only under GLOS (CENSUS below GLOS: what GLOS's kernel, stub and agent ask of DOS today, with the "
          "programs' calls GLOS passed down)", [k for k, s in seen.items() if s["kind"] == {"glos"}])
    if ends:
        lines.extend(["## Programs that ended inside their runs", "",
                      "| Job | Program | Entries | Calls | Dropped | INT 21h at the end |", "|---|---|---|---|---|---|"])
        for job, prog, psp, entries, calls, dropped, top in ends:
            if prog != "CENSUS":
                lines.append("| %s | %s | %s | %s | %s | %s |" % (job, prog, entries, calls, dropped, top))
        lines.append("")
    open(out, "w").write("\n".join(lines))
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "v1", "native-dos-census.md"))
    ap.add_argument("--print", action="store_true")
    a = ap.parse_args()
    text = report(a.out)
    if a.print:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
