#!/usr/bin/env python3
"""J0: print each report from a hidraw device that differs from the one before, with the time.
    hid.py /dev/hidrawN SECONDS"""
import os, select, sys, time
fd = os.open(sys.argv[1], os.O_RDONLY | os.O_NONBLOCK)
t0, last = time.time(), None
while time.time() - t0 < float(sys.argv[2]):
    if select.select([fd], [], [], 0.2)[0]:
        r = os.read(fd, 64)
        if r != last:
            print("%7.3f in  %s" % (time.time() - t0, " ".join("%02x" % x for x in r)), flush=True)
            last = r
