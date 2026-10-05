# Pinned external inputs (downloaded once by tools/setup/fetch.sh into the
# shared cache, ~/.cache/mga-glide/dl, which the dev container mounts).

# HX DOS extender runtime 2.23 (Japheth; freeware, "may be used for any
# purpose"): HDPMI32/HDPMI16 and the IOPL-0 HDPMI32i/HDPMI16i, used as
# behavioural baselines only (PRD D26), never as code.
HXRT_URL      := https://github.com/Baron-von-Riedesel/HX/releases/download/v2.23/HXRT223.zip
HXRT_SHA256   := 20510cf66d6c8704f66f908f8fbf9783198195c1e043a6ca9fb6adeadf8785c9

# DJGPP 2.05's test suite (DJ Delorie; DJGPP's licence, COPYING.DJ): its
# sources are compiled by GLOS's build and run as M4b's DJGPP checks
# (tests/loopa/jobs.py djtst); none of it is in GLOS.
DJTST_URL     := https://www.delorie.com/pub/djgpp/current/v2/djtst205.zip
DJTST_SHA256  := 6f99cff9339be41f5b919945070842ad6f5db7089cbf8fdc0db3dfe351e32c8f

# HDPMI's regression tests (Japheth's HX, Src/HDPMI/Regression/Regression.zip
# at commit f2276db9; binaries only, freeware like the runtime): run by
# jobs.py hdpmireg under HDPMI32i and GLOS (M4c); none of it is in GLOS.
HDPMIREG_URL    := https://raw.githubusercontent.com/Baron-von-Riedesel/HX/f2276db9accfc57facf2588bc016a27130597bb1/Src/HDPMI/Regression/Regression.zip
HDPMIREG_SHA256 := 1a9c1983f6daa1e86f539f6fe7c016d25d925ea59d898b3ff4c5aabe56b67e25

# ecm's DPMI debugger tests (C. Masloch; lDebugX and dpmimini.com, from
# pushbx.org/ecm/test): lDebugX steps a DPMI client under HDPMI32i and GLOS
# (jobs.py ecm, M4c); none of it is in GLOS.
ECMTEST_URL     := https://pushbx.org/ecm/test/20210127.zip
ECMTEST_SHA256  := 7a2423b608efc9652e2a6642a29a4759ea5976c147bb6df82b9ecd717bf182b8
