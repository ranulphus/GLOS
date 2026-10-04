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
