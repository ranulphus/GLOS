# Pinned external inputs (downloaded once by tools/setup/fetch.sh into the
# shared cache, ~/.cache/mga-glide/dl, which the dev container mounts).

# HX DOS extender runtime 2.23 (Japheth; freeware, "may be used for any
# purpose"): HDPMI32/HDPMI16 and the IOPL-0 HDPMI32i/HDPMI16i, used as
# behavioural baselines only (PRD D26), never as code.
HXRT_URL      := https://github.com/Baron-von-Riedesel/HX/releases/download/v2.23/HXRT223.zip
HXRT_SHA256   := 20510cf66d6c8704f66f908f8fbf9783198195c1e043a6ca9fb6adeadf8785c9
