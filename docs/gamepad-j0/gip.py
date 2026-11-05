#!/usr/bin/env python3
"""J0 (GLOS docs/gamepad.md): talk to an Xbox One-style (GIP) pad from Linux userspace through usbfs, with no
kernel driver bound. Claims interface 0, optionally sends the GIP power-on packet, then prints every input
report that differs from the one before, with the time, for SECONDS.
    gip.py BUS DEV SECONDS [start|nostart]      e.g. gip.py 001 003 5 start
"""
import ctypes, fcntl, os, struct, sys, time

bus, dev, secs = sys.argv[1], sys.argv[2], float(sys.argv[3])
start = len(sys.argv) < 5 or sys.argv[4] == "start"


def ioc(d, nr, size):
    return d << 30 | size << 16 | ord("U") << 8 | nr


class Bulk(ctypes.Structure):
    _fields_ = [("ep", ctypes.c_uint), ("len", ctypes.c_uint), ("timeout", ctypes.c_uint),
                ("data", ctypes.c_void_p)]


BULK = ioc(3, 2, ctypes.sizeof(Bulk))
CLAIM = ioc(2, 15, 4)
RELEASE = ioc(2, 16, 4)

fd = os.open("/dev/bus/usb/%s/%s" % (bus, dev), os.O_RDWR)
fcntl.ioctl(fd, CLAIM, struct.pack("I", 0))
t0 = time.time()


def xfer(ep, data, timeout_ms):
    buf = ctypes.create_string_buffer(bytes(data), 64) if data else ctypes.create_string_buffer(64)
    b = Bulk(ep, len(data) if data else 64, timeout_ms, ctypes.addressof(buf))
    try:
        n = fcntl.ioctl(fd, BULK, b)
    except OSError as e:
        return None, e.errno
    return buf.raw[:n], 0


def show(tag, data):
    print("%7.3f %s %s" % (time.time() - t0, tag, " ".join("%02x" % x for x in data)), flush=True)


seq = 0
if start:
    pkt = [0x05, 0x20, seq, 0x01, 0x00]     # GIP power on: command 05h, flags 20h, sequence, length 1, payload 00h
    show("out", pkt)
    r, err = xfer(0x01, pkt, 500)
    print("        out result", "ok" if r is not None else "errno %d" % err, flush=True)
last = None
while time.time() - t0 < secs:
    r, err = xfer(0x81, None, 200)
    if r is None:
        continue                            # (timeout: nothing reported)
    if r != last:
        show("in ", r)
        last = r
fcntl.ioctl(fd, RELEASE, struct.pack("I", 0))
os.close(fd)
