#!/usr/bin/env python3
"""Checks a program's link map against libglos's list of wrapped C library functions
(sdk/libglos/wrap.list): every one is reached through the lock (gloswrap.c), from the C library's
own files too.

  wrapcheck.py WRAP.LIST PROGRAM.map      (the map made with -Wl,-Map,... -Wl,--cref)

For each function F in the list, in the map's cross reference table:
  - __wrap_F is defined by libglos.a(gloswrap.o): the lock's wrapper is in the program;
  - F itself, if the program has it, is referenced by nobody but gloswrap.o: every other caller's
    reference went to the wrapper (the linker's --wrap). A caller listed there bypasses the lock.
A function nothing in the program calls is in neither place, which is fine.

And the C library's functions that use its transfer buffer (DOS's data passes through it) but are not wrapped
(UNLOCKED below) must not be called by the program's own files at all, only by the library itself from inside
a wrapped call: a thread calling one would put its data in the buffer while another's wrapped call is between
its copy into the buffer and DOS's read of it. (2026-11-26: printf("\n") made putchar by GCC, unwrapped, let a
thread's findfirst write over a line of the Games Library's output.)"""
import re
import sys

# The C library's entry points that go to DOS through the transfer buffer (each refers to __go32_info_block, or
# reaches a function that does, without passing a wrapped one) and that a program might call: the stdio buffers'
# own fill and flush, the low-level file calls under open/read/write/close, the path ones, DOS's own finds and
# file calls. Wrap one (wrap.list, gloswrap.c) rather than take it off this list.
UNLOCKED = ["_flsbuf", "_filbuf", "_open", "_creat", "_creatnew", "_read", "_write", "_rename",
            "_chmod", "_truename", "_fixpath", "lstat", "statfs", "_dos_findfirst", "_dos_findnext",
            "_dos_open", "_dos_creat", "_dos_creatnew", "_dos_close", "_dos_read", "_dos_write",
            "_dos_getftime", "_dos_setftime", "_dos_getfileattr", "_dos_setfileattr", "_dos_getdiskfree",
            "_get_volume_info", "__file_exists", "_is_executable", "_lfn_gen_short_fname", "realpath"]


def libc(f):
    """a reference from the C library's (or the compiler's) own files"""
    return bool(re.search(r"lib(c|m|gcc)\.a\(", f))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    names = [l.strip() for l in open(sys.argv[1]) if l.strip() and not l.startswith("#")]
    text = open(sys.argv[2], errors="replace").read()
    at = text.find("Cross Reference Table")
    if at < 0:
        sys.exit("wrapcheck: %s has no cross reference table (link with -Wl,--cref)" % sys.argv[2])
    refs, cur = {}, None                    # symbol -> [defining file, referencing files...]
    for line in text[at:].splitlines()[2:]:
        if not line.strip():
            continue
        if line[0] not in " \t":
            m = re.match(r"(\S+)\s+(\S.*)?$", line)
            cur = m.group(1)
            refs[cur] = [m.group(2).strip()] if m.group(2) else []
        elif cur:
            refs[cur].append(line.strip())
    bad, wrapped = [], 0
    for n in names:
        w, f = refs.get("__wrap_" + n), refs.get(n)      # (the table has C's names)
        if w is None and f is None:
            continue                        # (not in this program)
        if w is None or "gloswrap.o" not in w[0]:
            bad.append("%s: no wrapper from gloswrap.o (%s)" % (n, w[0] if w else "__wrap_%s is not in the program" % n))
            continue
        wrapped += 1
        for user in (f or [])[1:]:
            if "gloswrap.o" not in user:
                bad.append("%s: called past the lock by %s" % (n, user))
    for n in UNLOCKED:
        if n in names:
            continue
        for user in (refs.get(n) or [])[1:]:
            if not libc(user):
                bad.append("%s: uses the transfer buffer outside the lock, called by %s (wrap it: wrap.list)" % (n, user))
    for b in bad:
        print("wrapcheck: " + b)
    if bad or not wrapped:
        sys.exit("wrapcheck: %s FAILED (%d wrapped, %d problems)" % (sys.argv[2], wrapped, len(bad)))
    print("  WRAP    %s: %d of %d listed functions in the program, all through the lock" % (sys.argv[2], wrapped, len(names)))


main()
