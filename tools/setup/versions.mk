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

# Mozilla's root certificates as curl ships them (cacert.pem, MPL-2.0; curl.se/docs/caextract.html, the dated file):
# made into C:\GLOS\CERTS.DAT, HTTPS's trust anchors (tools/certs/mkcerts.c, M10 step C; PRD D59).
CACERT_URL    := https://curl.se/ca/cacert-2026-09-25.pem
CACERT_SHA256 := a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505

# DOSLFN 0.41f (Henrik Haftmann, Jason Hood; freeware with sources; FreeDOS 1.3's package): the long-name driver
# Loop A's long-name tests load before GLOS (jobs.py api_lfn_case, phase B step 7). A DOS program beside GLOS, as
# a user's own would be; none of it is in GLOS.
DOSLFN_URL    := https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.3/drivers/doslfn.zip
DOSLFN_SHA256 := b96e93bb21c1cb4d9b6e37e76f96174345c027ce354aae9c09b38713e7114377

# FreeDOS 1.4's floppy edition (freedos.org; its programs under their own licences, the GPL for most): its 1.44 MB
# boot floppy is what GLOS's setup floppy starts from (tools/floppy/mkfloppy.sh; docs/floppy.md), FreeDOS's
# kernel, shell, FDISK, FORMAT and SYS on it unchanged. None of it is in GLOS. (RetroGPU's Loop A boots the same
# image: tools/loopa/mkgolden.sh.)
FREEDOS_FLOPPY_URL    := https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/FD14-FloppyEdition.zip
FREEDOS_FLOPPY_SHA256 := 45b1fa7c52dd996c3bfa5e352ffcd410781b952a6ad629f15a4c9ec4bbaefc5a

# GLOS's icons (docs/gui.md 1.11; tools/mkicons.py, `make icons`): the sets they are taken from. What is made
# from them is kept in the repository (sdk/gui/fonts/ICONS.DAT, sdk/gui/glyphs.c), so the build fetches
# neither. Farm-Fresh v3.92 (FatCow; CC BY 3.0 US; the project closed in 2014, and this is a copy of its final
# release at a fixed commit) and Font Awesome Free 6.7.2 (icons CC BY 4.0, fonts SIL OFL 1.1, code MIT).
FARMFRESH_URL      := https://github.com/gammasoft/fatcow/archive/e6ee23359517a30cd388488a8f5fc7afcdcfdbc5.zip
FARMFRESH_SHA256   := 45045ed8e51d6e25ddaa9b7354d107a498213abcca8a5b70307c9f238d78693d
FONTAWESOME_URL    := https://github.com/FortAwesome/Font-Awesome/releases/download/6.7.2/fontawesome-free-6.7.2-web.zip
FONTAWESOME_SHA256 := ecdaaa6d347cd7da82c66054770995e97f3d066a57e8d58ac9c517f0f77561fb

# Ralf Brown's Interrupt List, release 61 (2000; "permission is granted to use and redistribute the files ... in
# any way", credit kept): parts A-C hold INT 00h-2Fh, part D INT 30h on (EMS's INT 67h). Native GLOS's DOS checklist is made from it
# (tools/census/rbil.py, docs/v1/native-dos-rbil.md); none of it is in GLOS.
RBIL_A_URL    := https://www.cs.cmu.edu/~ralf/interrupt-list/inter61a.zip
RBIL_A_SHA256 := 8077536135c0290ff18a4a44cf63c56339f6127f053ee673f9895837248cc947
RBIL_B_URL    := https://www.cs.cmu.edu/~ralf/interrupt-list/inter61b.zip
RBIL_B_SHA256 := 713e9e218eda694ed2bac8d5f02378f935f3e3132b79c92c44c14101e76b88a7
RBIL_C_URL    := https://www.cs.cmu.edu/~ralf/interrupt-list/inter61c.zip
RBIL_C_SHA256 := d7a059a1700c765adb9d4bf9dbe6289621aca6ff09c0583192764ee47e2b690a
RBIL_D_URL    := https://www.cs.cmu.edu/~ralf/interrupt-list/inter61d.zip
RBIL_D_SHA256 := 0d0a04e84dd6a1db312157f3365fc7ee0b539af677944ace593df5236a563241
