# GLOS on a real PC (the bench)

These scripts run Loop A's checks against a real PC running GLOS in agent mode, using jobs.py's own code where
it can: its GUI driver, its test PKI and repositories, its containers. They talk to the PC over SSH, so the PC
needs only a network. A serial cable from COM1 is useful for boots that fail before the network is up, and
without one `GLOS.EXE /VERBOSE` shows the kernel's log on the text screen.

| Script | What it checks | Loop A's version |
|---|---|---|
| `bench_gui.py SCRIPT...` | GUI test scripts (tests/gui) on the real desktop, each shot byte for byte against the host's goldens (`make gui-goldens`) | the gui suite |
| `bench_vnc.py` | vncdotool through `ssh -L`: its screen equal to `glos shot`'s, keys into Notepad, a press on its close box; `glos rec`'s frames | the vnc suite |
| `bench_sshc.py` | The terminal's ssh (a command, its status, vi and top over a pty) and sftp (1 MB each way) against OpenSSH in a container on this host, key logins only | the sshc suite |
| `bench_glwin.py [--hang] [TEST...]` | DOS-GL's conformance programs (`DOSGL_DIR`'s build) full-screen and then each in a window on the desktop (`GLOS_BENCH_DESK`, default 800x600x16): every frame the same bytes, each an app with turns at the engine; `--hang`: TEXCUBE's forced GL hang ended by the watchdog, then drawing right | the glwin suite |
| `bench_pkg.py` | A signed test source on this host over HTTPS: install, upgrade, remove (GLOS.CFG byte for byte as before), a changed zip and a changed index refused. PKG runs from its own base, `C:\PKGTEST`, so the PC's own trust and keys are untouched | the pkg suite |

The PC, set in the environment:

- `GLOS_BENCH_HOST`: its address (default 192.168.1.33).
- `GLOS_BENCH_KEY`: the private key its `KEYS\AUTHKEYS` lets in (default `~/.ssh/glos_bench`; not the repository's
  test key, which anyone can read).
- `GLOS_BENCH_HOST_LAN`: this host's address as the PC reaches it (default: found from the route to it).
- `GLOS_BENCH_BITS`: the desktop's depth for the GUI and VNC checks (default 16).

Its host key must be in `known_hosts` under the name `bench-g400` (`HostKeyAlias`). The scripts leave nothing on
the PC but what they found there: they remove what they made (`C:\GUI` excepted: `bench_gui.py`'s names scripts
need it made first, `md C:\GUI`, and removed after).

The first bench PC, its setup and results: docs/milestones-m10.md, "On the bench".
