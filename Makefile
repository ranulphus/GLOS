# GLOS (Graphics Library Operating System, for DOS). See PRD.md.
#
#   make                build/ow/GLOS.EXE (16-bit loader) and build/kernel/GLOSK.BIN (the kernel)
#   make loopa [CARD=g450]   run GLOS.EXE in 86Box through MGA-Glide's Loop A: out/loopa-CARD/
#   make check-deps     MGA-Glide at or after deps.mk's pin
#   make sync-hal       third_party/mgahal from $(MGA_GLIDE) (the HAL; tools/sync-hal.sh)
#   make check-hal      the vendored HAL against its MANIFEST
#   make host-test      kernel code's host tests (in the dev container)
#   make ssh-test       the SSH layer on host sockets against OpenSSH's ssh (M3)
#   make loopa-m1       M1's exit matrix in Loop A (tests/loopa/jobs.py)
#   make loopa-gdb      gdb on the kernel's COM2 stub in Loop A (tools/gdb-loopa.sh)
#   make loopa-m2       M2's exit: HX tools with and without GLOS (JOBS=3 at once)
#   make loopa-hostile  M2's exit: hostile programs killed with the hotkey
#   make loopa-sched    the scheduler's self-test threads beside the VM (M3)
#   make loopa-mem      conventional memory under GLOS against plain DOS (PRD P7)
#   make loopa-shell    GLOS as the DOS shell: SHELL= boots, AUTOEXEC.BAT, the fallback
#   make loopa-net      the NE2000s: DHCP, TCP echo, refused under a packet driver
#   make loopa-ssh      ssh from the host into GLOS in Loop A (runs on the host)
#   make loopa-m3       M3's exit: the ssh suite (agent, capture, SFTP, glos shot, VECCHK)
#   make loopa-dpmi     the DPMI host: DPMIMINI, DPMICONF-32 against CWSDPMI and HDPMI32i,
#                       MGA-Glide's HELLOs (M4a's exit: make loopa-m4a)
#   make loopa-m4b      loopa-dpmi, djtst205 against CWSDPMI (loopa-djtst), MGA-Glide's DJGPP
#                       tools (loopa-dpmitools)
#   make loopa-m4c      loopa-m4b, HDPMI's regression tests against HDPMI32i (loopa-hdpmireg),
#                       lDebugX on ecm's dpmimini (loopa-ecm), and MGA-Glide's conform and replays,
#                       GTA, Screamer Rally and DOSBench under GLOS as the shell (loopa-m4c-games,
#                       tools/m4c-games.sh; M4C_MGA= an MGA-Glide tree at deps.mk's pin)
#   make loopa-m4d      loopa-m4c, DPMICONF-16 against HDPMI16/16i (loopa-dpmi16), and TPX on RTM
#                       compiling and running a program (loopa-tpx; BORLAND_DIR, make m4d-inputs)
#   make loopa-sess     exclusive sessions: what SESSTEST leaves behind, natively and under
#                       GLOS /RUN, as the shell and as agent jobs (runs on the host)
#   make loopa-display  the desktop's screen (M6): the 2D library's test card at 8, 16 and 32 bits on the S3
#                       and the G200, a screenshot of each against the host's own rendering
#   make loopa-desk     the desktop (M6): the demonstration's windows, task bar and pointer composed on the
#                       real screen at each depth, against the host's composition
#   make loopa-win      windows, input and the API (M6): apps with windows on the real screen, driven with
#                       glos key and glos mouse; the keyboard three ways; a busy app beside one that draws
#   make fonts          kernel/gfx/fonts again from TrueType fonts (tools/mkfont.py; on the host)
#   make loopa-apps     GLOS apps (M6): APPTESTs at once, beside a busy one, a session, a crash; kills;
#                       GLOS /RUN with an app (runs on the host)
#   make loopa-gpu      the GPU lock and apps' VRAM (M8): GPUTESTs drawing with the engine beside the desktop
#   make loopa-audio    GLOS's mixer (M8): AUDTESTs through the SB16, recorded
#   make loopa-glwin    DOS-GL's conformance EXEs (DOSGL_DIR) full-screen on plain DOS and windowed on GLOS
#   make loopa-sdlwin   DOS-GL's SDL examples (DOSGL_DIR) in GLOS windows, sound through the mixer
#   make loopa-games    Fifth Wheel (FIFTHWHEEL_DIR) and PrBoom-plus (DOSGL_DIR) on plain DOS and windowed
#   make loopa-m8       M8's exit: loopa-gpu, loopa-audio, loopa-glwin, loopa-sdlwin and loopa-games
#   make gui-goldens    the toolkit's and the apps' pictures, drawn on the host (M9; tests/gui/*.txt)
#   make loopa-gui      GUITEST's scripts on the desktop against them (M9)
#   make loopa-config   GLOS.CFG's settings over ssh, kept and written by the kernel (M9)
#   make loopa-console  programs started by apps, console jobs, the shell's events, stats, watches (M9)
#   make loopa-native   the native apps' scripts on the desktop against the host's goldens (M9)
#   make loopa-control  every setting changed from the Control Panel by keys (M9)
#   make loopa-m9       M9's exit: gui, config, console, native and control
#   make loopa-m10      M10's exit: time, netapp, https, sshc, pkg, vnc and gui (step G's scripts)
#   make loopa-time     the clock: SNTP, the zone, the RTC and DOS, names (M10)
#   make loopa-netapp   apps' TCP connections and sockets (NETTEST) against the host (M10)
#   make loopa-https    HTTPS: FETCH, certificates checked, GitHub and a FreeDOS mirror (M10)
#   make sshc-test      the SSH and SFTP client against OpenSSH's sshd on the host (M10)
#   make loopa-sshc     the terminal's ssh, sftp, ssh-keygen against OpenSSH in a container (M10)
#   make loopa-pkg      packages: a signed one installed, upgraded, removed; refusals; FreeDOS's (M10)
#   make loopa-vnc      VNC through an SSH tunnel (vncdotool), and glos rec's frames against glos shot (M10)
#   make gate           M4e's exit: the baseline matrix under GLOS and with direct mode forced, a kill and
#                       an ssh probe for every program (tools/gate/run.py; on the host; hours)
#   make survey-tools   build/dj/IFTEST.EXE (DJGPP) for tools/survey/survey.py
include config.mk
-include config.local.mk
include deps.mk

DEV      := $(MGA_GLIDE)/tools/dev
CFG_SRCS := sdk/config/cfg.c sdk/config/tz.c   # GLOS.CFG's schema and time zones, shared everywhere
CP_SRCS := sdk/config/cp.c sdk/config/cp_tables.c       # DOS code pages (M10): libglos's and the host's
codepages:
	$(Q)python3 tools/mkcp.py > sdk/config/cp_tables.c
BUILD_ID := $(shell git describe --always --dirty 2>/dev/null || echo unknown)
Q ?= @

OWBIN  := $(WATCOM)/binl64
OWENV  := env WATCOM=$(WATCOM) INCLUDE=$(WATCOM)/h PATH=$(OWBIN):$(PATH)
WCC16  := $(OWENV) $(OWBIN)/wcc
WLINK  := $(OWENV) $(OWBIN)/wlink

.PHONY: sshc-test apps gui-goldens gui-goldens-run loopa-gui loopa-native loopa-config loopa-console loopa-control loopa-m9 loopa-time loopa-netapp loopa-https loopa-sshc loopa-pkg loopa-vnc loopa-m10 loopa-apps loopa-display loopa-desk loopa-win loopa-gpu loopa-audio loopa-glwin loopa-sdlwin loopa-games loopa-m8 dosgl-inputs games-inputs fonts all dos-tests kernel host-test ssh-test loopa loopa-m1 loopa-m2 loopa-hostile loopa-sched loopa-mem loopa-shell loopa-net loopa-ssh loopa-m3 loopa-dpmi loopa-m4a loopa-djtst loopa-dpmitools loopa-m4b loopa-hdpmireg loopa-ecm loopa-m4c-games loopa-m4c loopa-dpmi16 loopa-tpx loopa-m4d loopa-sess gate loopa-gdb djtst glos-cache m4c-inputs m4d-inputs check-deps sync-hal check-hal clean help survey-tools
all: build/ow/GLOS.EXE build/kernel/GLOSK.BIN

help:
	@sed -n '3,/^[^#]/p' Makefile | grep '^#' | sed 's/^# //'

check-deps:
	@git -C "$(MGA_GLIDE)" merge-base --is-ancestor "$(MGA_GLIDE_PIN)" HEAD 2>/dev/null \
	  || { echo "$(MGA_GLIDE) is not at or after $(MGA_GLIDE_PIN) (deps.mk)"; exit 1; }

sync-hal:
	tools/sync-hal.sh $(MGA_GLIDE)
check-hal:
	tools/sync-hal.sh --check

# ---- Host tests: kernel code built 32-bit for Linux (in the dev container,
# which has the 32-bit C library) --------------------------------------------
# TinySSH's crypto (third_party/tinyssh; THIRD_PARTY.md): the SSH algorithms only.
TINYSSH_SRCS := $(filter-out %_lib25519.c %_lib1305.c,$(wildcard third_party/tinyssh/*.c)) \
                $(foreach t,int8 int16 int32 int64 uint8 uint16 uint32 uint64,third_party/tinyssh/cryptoint/$(t)_optblocker.c)
# The SSH and SFTP client's sources (sdk/sshc; M10 step D; built below for DJGPP and the host).
SSHC_SRCS := sdk/sshc/sshc.c sdk/sshc/keys.c sdk/sshc/sftpc.c kernel/ssh/sshbuf.c kernel/ssh/sshkeys.c
SSHC_FLAGS := -Iinclude -Ikernel/ssh -Ithird_party/tinyssh -Ithird_party/tinyssh/cryptoint
HOST_TESTS := pmm_test:kernel/mm/pmm.c heap_test:kernel/mm/heap.c v86dec_test:kernel/vm/v86dec.c \
              vpic_test:kernel/vm/vpic.c png_test:kernel/lib/png.c
host-test:
	$(Q)$(DEV) $(MAKE) -s host-test-run
host-test-run:
	@mkdir -p build/host
	@set -e; for t in $(HOST_TESTS); do n=$${t%%:*}; src=$${t#*:}; \
	  $(HOST_CC) -m32 -O1 -g -Wall -Wextra -Werror -Ikernel/include -Iinclude -o build/host/$$n tests/host/$$n.c $$src; \
	  build/host/$$n; done
	@$(HOST_CC) -m32 -O2 -g -Wall -Ithird_party/tinyssh -Ithird_party/tinyssh/cryptoint -o build/host/crypto_kat \
	  tests/host/crypto_kat.c $(TINYSSH_SRCS) && build/host/crypto_kat
	@$(HOST_CC) -m32 -O1 -g -Wall -Wextra -Werror -Ikernel/include -Iinclude -o build/host/gfx_test \
	  tests/host/gfx_test.c tests/host/qdev.c $(GFX_SRCS) kernel/lib/png.c && build/host/gfx_test
	@$(HOST_CC) -m32 -O1 -g -Wall -Wextra -Werror -Ikernel/include -Iinclude -o build/host/wm_test \
	  tests/host/wm_test.c tests/host/qdev.c $(WM_SRCS) $(GFX_SRCS) kernel/lib/png.c && build/host/wm_test
	@$(HOST_CC) -m32 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-format-truncation -Ikernel/include -Iinclude \
	  -DGLOSSHIM_NO_MAIN \
	  -o build/host/gui_test tests/host/gui_test.c tests/host/glosshim.c $(filter-out sdk/gui/field.c,$(GUI_SRCS)) \
	  $(GFX_SRCS) kernel/lib/png.c $(CFG_SRCS) $(CP_SRCS) && build/host/gui_test
	@$(HOST_CC) -m32 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -Itests/host/kstub -Ikernel/include -Iinclude \
	  -o build/host/cfg_test tests/host/cfg_test.c $(CFG_SRCS) && build/host/cfg_test
	@$(HOST_CC) -m32 -O1 -g -Wall -Wextra -Werror -Iinclude -o build/host/tz_test tests/host/tz_test.c sdk/config/tz.c \
	  && build/host/tz_test
	@$(HOST_CC) -m32 -O1 -g -Wall -Wextra -Werror -Iinclude -o build/host/cp_test tests/host/cp_test.c $(CP_SRCS) \
	  && build/host/cp_test
	@$(HOST_CC) -m32 -O1 -g -Wall -Wextra -Werror -Iapps/term -o build/host/vt_test tests/host/vt_test.c apps/term/vt.c \
	  && build/host/vt_test

# The toolkit (sdk/gui, M9) and the apps on the host, against tests/host/glosshim.c: each test script
# (tests/gui/NAME.txt) run at 8, 16 and 32 bits, its shots written to build/host/gui/ (the goldens Loop A's
# gui suites compare with). `make gui-goldens` (in the dev container).
GUI_SRCS := $(wildcard sdk/gui/*.c)
GUI_HOST := $(HOST_CC) -m32 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-format-truncation -Ikernel/include -Iinclude
build/host/guitest: tests/dos/guitest.c $(GUI_SRCS) sdk/gui/guiint.h include/glos/gui.h include/glos/api.h \
                    tests/host/glosshim.c kernel/include/keymap.h $(CFG_SRCS) $(CP_SRCS) include/glos/config.h include/glos/tz.h
	@mkdir -p build/host
	$(Q)$(GUI_HOST) -Dmain=app_main -c -o build/host/guitest-app.o tests/dos/guitest.c
	$(Q)$(GUI_HOST) -o $@ build/host/guitest-app.o tests/host/glosshim.c $(GUI_SRCS) $(LIBGLOS_GFX) kernel/lib/png.c \
	  $(CFG_SRCS) $(CP_SRCS)
# The native apps (apps/NAME/NAME.c, M9): on the host for their goldens, as GUITEST.
APPS := notepad calc images term files monitor control
APP_LIBS_term := build/sdk/libsshc.a           # (TERM's ssh, sftp and ssh-keygen: M10)
APP_HOST_FLAGS := -Ithird_party/stb
define app_host
build/host/$(1): $$(wildcard apps/$(1)/*.c apps/$(1)/*.h) $$(GUI_SRCS) sdk/gui/guiint.h include/glos/gui.h include/glos/api.h tests/host/glosshim.c \
                 kernel/include/keymap.h $$(CFG_SRCS) $$(CP_SRCS) include/glos/config.h include/glos/tz.h $$(APP_HOST_LIBS_$(1))
	@mkdir -p build/host
	$$(Q)$$(GUI_HOST) $$(APP_HOST_FLAGS) -Dmain=app_main -c -o build/host/$(1)-app.o apps/$(1)/$(1).c
	$$(Q)$$(GUI_HOST) -o $$@ build/host/$(1)-app.o $$(filter-out apps/$(1)/$(1).c,$$(wildcard apps/$(1)/*.c)) \
	  tests/host/glosshim.c $$(GUI_SRCS) $$(LIBGLOS_GFX) kernel/lib/png.c $$(CFG_SRCS) $$(CP_SRCS) $$(APP_HOST_LIBS_$(1)) -lm
endef
# The terminal's ssh (M10): the client for the host too, 32-bit as the rest, its crypto without -Werror.
APP_HOST_LIBS_term := build/host/libsshc32.a
build/host/libsshc32.a: $(SSHC_SRCS) sdk/sshc/random.c $(TINYSSH_SRCS) include/glos/sshc.h include/glos/sftpc.h
	@mkdir -p build/host/sshc32
	$(Q)echo "  CC      $@"
	$(Q)rm -f $@ build/host/sshc32/*.o
	$(Q)for f in $(SSHC_SRCS) sdk/sshc/random.c $(TINYSSH_SRCS); do \
	    $(HOST_CC) -m32 -O1 -g $(SSHC_FLAGS) -c -o build/host/sshc32/$$(basename $$f .c).o $$f || exit 1; \
	  done
	$(Q)ar rcs $@ build/host/sshc32/*.o
$(foreach a,$(APPS),$(eval $(call app_host,$(a))))
GUI_SCRIPTS := $(wildcard tests/gui/*.txt)
# (A:, C:\HX, C:\OUT and C:\TEST are the Loop A machine's own, which the file manager's tree shows.)
gui-goldens:
	$(Q)$(DEV) $(MAKE) -s gui-goldens-run
gui-goldens-run: build/host/guitest $(addprefix build/host/,$(APPS))
	@rm -rf build/host/gui build/host/gui-drives && mkdir -p build/host/gui
	@set -e; for t in $(GUI_SCRIPTS); do n=$$(basename $$t .txt); prog=$$(awk '$$1=="run"{print tolower($$2); exit}' $$t); \
	  prog=$${prog%.exe}; mkdir -p build/host/gui/$$n; for b in 8 16 32; do rm -rf build/host/gui-drives; \
	    cp -r tests/gui/drives build/host/gui-drives; cp sdk/gui/fonts/SMOOTH.DAT build/host/gui-drives/C/GLOS/; \
	    mkdir -p build/host/gui-drives/A build/host/gui-drives/C/HX build/host/gui-drives/C/OUT build/host/gui-drives/C/TEST; \
	    GLOS_HOST_DRIVES=build/host/gui-drives build/host/$$prog $$t build/host/gui/$$n $$b > build/host/gui/$$n-$$b.log \
	      || { echo "gui-goldens: $$n at $$b bits:"; cat build/host/gui/$$n-$$b.log; exit 1; }; done; \
	  grep -q '^#loopa-only' $$t || sed -n 's/^expect //p' $$t | while IFS= read -r e; do \
	    grep -qxF -- "$$e" build/host/gui/$$n-16.log || { echo "gui-goldens: $$n: no line '$$e'"; exit 1; }; done; \
	  echo "gui-goldens: $$n $$(grep -c ^SHOT build/host/gui/$$n-8.log) shots"; done

# (`make fonts` makes kernel/gfx/fonts again from FONT_DIR's TrueType files: on the host, with python3-pil.)
FONT_DIR ?= /usr/share/fonts/truetype/dejavu
# (M10 step G: Latin Extended-A, Greek, Cyrillic and the punctuation they need; the monospaced font has what code
# page 437 draws too, box drawing and blocks for the terminal and DOS programs' output.)
FONT_TEXT := 0x192,0x2C6-0x2C7,0x2D8-0x2DD,0x370-0x3FF,0x400-0x45F,0x2010-0x2027,0x2030-0x203A
FONT_SIGNS := 0x20AC,0x2116,0x2122,0x2190-0x2193
FONT_437 := 0x207F,0x20A7,0x20AC,0x2116,0x2122,0x2190-0x2193,0x2219-0x221A,0x221E,0x2229,0x2248,0x2261,0x2264-0x2265
FONT_437 := $(FONT_437),0x2310,0x2320-0x2321,0x23BA-0x23BD,0x2500-0x257F,0x2580-0x259F,0x25A0
# (And the toolkit's font smoothing, M10: the same glyphs anti-aliased, sdk/gui/fonts/SMOOTH.DAT, C:\GLOS's.)
fonts:
	@mkdir -p build/fonts sdk/gui/fonts
	$(Q)python3 tools/mkfont.py $(FONT_DIR)/DejaVuSans.ttf 11 ui --last 0x17F --ranges $(FONT_TEXT),$(FONT_SIGNS) \
	  --smooth build/fonts/ui.aa > kernel/gfx/fonts/ui.c
	$(Q)python3 tools/mkfont.py $(FONT_DIR)/DejaVuSans-Bold.ttf 11 bold --last 0x17F --ranges $(FONT_TEXT),$(FONT_SIGNS) \
	  --smooth build/fonts/bold.aa > kernel/gfx/fonts/bold.c
	$(Q)python3 tools/mkfont.py $(FONT_DIR)/DejaVuSansMono.ttf 11 mono --last 0x17F --ranges $(FONT_TEXT),$(FONT_437) \
	  --smooth build/fonts/mono.aa > kernel/gfx/fonts/mono.c
	$(Q){ printf 'GLOSAA1\n'; cat build/fonts/ui.aa build/fonts/bold.aa build/fonts/mono.aa; } > sdk/gui/fonts/SMOOTH.DAT

# The SSH layer (kernel/ssh) on host sockets against OpenSSH's client
# (tests/host/sshd_test.sh): native, on the host, which has ssh.
SSH_SRCS := kernel/ssh/ssh.c kernel/ssh/sshbuf.c kernel/ssh/sshkeys.c
build/host/sshd: tests/host/sshd.c $(SSH_SRCS) $(wildcard kernel/ssh/*.h) $(TINYSSH_SRCS)
	@mkdir -p build/host
	$(Q)echo "  CC      $@"
	$(Q)gcc -O2 -g -Wall -Wextra -Ikernel/ssh -Ithird_party/tinyssh -Ithird_party/tinyssh/cryptoint -o $@ \
	  tests/host/sshd.c $(SSH_SRCS) $(TINYSSH_SRCS)
ssh-test: build/host/sshd
	$(Q)tests/host/sshd_test.sh build/host/sshd

# ---- GLOS.EXE: the 16-bit loader (Open Watcom, small model) ----------------
LOADER_C   := loader/main.c
LOADER_ASM := loader/lowlevel.asm loader/stub.asm
build/ow/obj16/%.obj: loader/%.c include/glos/bootinfo.h include/glos/config.h build/build_id
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC16   $<"
	$(Q)$(WCC16) -bt=dos -ms -3 -os -zq -we -iinclude -dGLOS_BUILD="\"$(BUILD_ID)\"" -fo=$@ $<
build/ow/obj16/%.obj: loader/%.asm
	@mkdir -p $(dir $@)
	$(Q)echo "  WASM    $<"
	$(Q)$(OWENV) $(OWBIN)/wasm -q -fo=$@ $<
build/ow/obj16/cfg.obj build/ow/obj16/tz.obj: build/ow/obj16/%.obj: sdk/config/%.c include/glos/config.h include/glos/tz.h
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC16   $<"
	$(Q)$(WCC16) -bt=dos -ms -3 -os -zq -we -iinclude -fo=$@ $<
build/ow/GLOS.EXE: build/ow/obj16/main.obj build/ow/obj16/lowlevel.obj build/ow/obj16/stub.obj build/ow/obj16/cfg.obj \
                   build/ow/obj16/tz.obj
	$(Q)echo "  WLINK   $@"
	$(Q)$(WLINK) system dos option quiet option stack=4k option map=build/ow/GLOS.map name $@ \
	  file build/ow/obj16/main.obj,build/ow/obj16/lowlevel.obj,build/ow/obj16/stub.obj,build/ow/obj16/cfg.obj \
	  file build/ow/obj16/tz.obj

# ---- DOS test programs (Open Watcom, small model): tests/dos/name.c -> build/ow/dos/NAME.EXE
define dos_test
build/ow/dos/$(1).EXE: tests/dos/$(2).c
	@mkdir -p build/ow/dos/obj
	$$(Q)echo "  WCC16   $$<"
	$$(Q)$$(WCC16) -bt=dos -ms -3 -os -zq -we -fo=build/ow/dos/obj/$(2).obj $$<
	$$(Q)$$(WLINK) system dos option quiet option stack=4k name $$@ file build/ow/dos/obj/$(2).obj
endef
$(eval $(call dos_test,HOSTILE,hostile))
$(eval $(call dos_test,XMSTEST,xmstest))
$(eval $(call dos_test,ECHOARGS,echoargs))
$(eval $(call dos_test,CLOCKCHK,clockchk))
$(eval $(call dos_test,CONIN,conin))
$(eval $(call dos_test,RUNOUT,runout))
$(eval $(call dos_test,SESSTEST,sesstest))
$(eval $(call dos_test,TIMECHK,timechk))
$(eval $(call dos_test,SHOTTEST,shottest))
$(eval $(call dos_test,TYPEIN,typein))
# DPMIMINI.COM: the smallest DPMI client, in assembly (M4a).
build/ow/dos/DPMIMINI.COM: tests/dos/dpmimini.asm
	@mkdir -p build/ow/dos/obj
	$(Q)echo "  WASM    $<"
	$(Q)$(OWENV) $(OWBIN)/wasm -q -fo=build/ow/dos/obj/dpmimini.obj $<
	$(Q)$(WLINK) format dos com option quiet name $@ file build/ow/dos/obj/dpmimini.obj
# DPMICONF-16 (tests/dos/dpmi16/): the 16-bit client's checks, C and WASM (M4d).
build/ow/dos/DPMI16.EXE: tests/dos/dpmi16/dpmi16.c tests/dos/dpmi16/dpmi16a.asm
	@mkdir -p build/ow/dos/obj
	$(Q)echo "  WCC16   tests/dos/dpmi16/dpmi16.c"
	$(Q)$(WCC16) -bt=dos -ms -3 -os -s -zq -we -fo=build/ow/dos/obj/dpmi16.obj tests/dos/dpmi16/dpmi16.c
	$(Q)echo "  WASM    tests/dos/dpmi16/dpmi16a.asm"
	$(Q)$(OWENV) $(OWBIN)/wasm -q -fpi87 -fo=build/ow/dos/obj/dpmi16a.obj tests/dos/dpmi16/dpmi16a.asm
	$(Q)$(WLINK) system dos option quiet option stack=4k name $@ \
	  file build/ow/dos/obj/dpmi16.obj,build/ow/dos/obj/dpmi16a.obj
DOS_TESTS := build/ow/dos/HOSTILE.EXE build/ow/dos/XMSTEST.EXE build/ow/dos/ECHOARGS.EXE build/ow/dos/DPMIMINI.COM \
             build/ow/dos/RUNOUT.EXE build/ow/dos/DPMI16.EXE build/ow/dos/SESSTEST.EXE build/ow/dos/TIMECHK.EXE \
             build/ow/dos/SHOTTEST.EXE build/ow/dos/TYPEIN.EXE build/ow/dos/CONIN.EXE build/ow/dos/CLOCKCHK.EXE
dos-tests: $(DOS_TESTS)

# The 2D library (kernel/gfx; M6 step C) and its fonts: 1-bit bitmaps made from TrueType fonts by
# tools/mkfont.py, on the host (python3-pil), and kept in the repository so that the kernel's build needs
# neither. `make fonts` makes them again from FONT_DIR's files.
WM_SRCS := kernel/wm/wm.c kernel/wm/demo.c
GFX_SRCS := kernel/gfx/gfx.c kernel/gfx/pal.c kernel/gfx/font.c kernel/gfx/region.c kernel/gfx/testcard.c \
            kernel/gfx/fonts/ui.c kernel/gfx/fonts/bold.c kernel/gfx/fonts/mono.c

# ---- GLOSK.BIN: the kernel (host gcc -m32, linked at C0100000h) -------------
KCFLAGS := -m32 -march=i486 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
           -fno-asynchronous-unwind-tables -fno-delete-null-pointer-checks -mgeneral-regs-only \
           -O2 -g -Wall -Wextra -Werror -nostdinc -Iinclude -Ikernel/include \
           -isystem $(shell $(HOST_CC) -m32 -print-file-name=include) -Ikernel/include/libc -Ikernel/net/port \
           -Ithird_party/lwip/src/include -Ithird_party/tinyssh -Ithird_party/tinyssh/cryptoint -Ikernel/ssh \
           -Ithird_party/mgahal/hal/include
KSRCS := kernel/entry.S kernel/arch/stubs.S kernel/arch/cpu.c kernel/core/main.c kernel/core/timer.c \
         kernel/core/sched.c kernel/drv/pci.c kernel/drv/nic.c kernel/drv/ne2k.c kernel/drv/rtl8139.c kernel/drv/e100.c \
         kernel/net/net.c kernel/net/sock.c kernel/lib/libc.c \
         kernel/core/random.c kernel/core/kat.c kernel/ssh/ssh.c kernel/ssh/sshbuf.c kernel/ssh/sshkeys.c \
         kernel/ssh/sshd.c \
         kernel/drv/serial.c kernel/lib/kprintf.c kernel/mm/pmm.c kernel/mm/heap.c kernel/mm/vmm.c \
         kernel/dbg/gdbstub.c kernel/dbg/gdbprog.c kernel/vm/v86.c kernel/vm/session.c kernel/vm/v86dec.c kernel/vm/vpic.c kernel/vm/vdev.c \
         kernel/vm/vkbc.c kernel/vm/int15.c kernel/vm/xms.c kernel/dos/agent.c kernel/dos/watch.c \
         kernel/dos/shot.c kernel/lib/png.c kernel/dos/dos.c kernel/ssh/sftp.c \
         kernel/dpmi/host.c kernel/dpmi/ldt.c kernel/dpmi/mem.c kernel/dpmi/rmcall.c kernel/dpmi/int31.c \
         kernel/dpmi/deliver.c kernel/dpmi/level.c kernel/dpmi/dosx.c kernel/dbg/crash.c \
         kernel/app/app.c kernel/app/api.c kernel/app/dev.c kernel/app/clip.c kernel/app/stat.c kernel/core/config.c kernel/core/clock.c $(GFX_SRCS) kernel/gfx/display.c kernel/gfx/mga.c kernel/gfx/gpu.c kernel/drv/sb16.c kernel/drv/mixer.c $(WM_SRCS) kernel/wm/desk.c kernel/wm/deskin.c kernel/wm/deskapp.c kernel/wm/vnc.c
KOBJS := $(patsubst kernel/%,build/kernel/%.o,$(KSRCS)) build/kernel/sdk_cfg.o build/kernel/sdk_tz.o
# GLOS.CFG's schema (sdk/config/cfg.c, include/glos/config.h; M9) and time zones (sdk/config/tz.c; M10): shared
# with the loader and the Control Panel.
build/kernel/sdk_cfg.o build/kernel/sdk_tz.o: build/kernel/sdk_%.o: sdk/config/%.c include/glos/config.h include/glos/tz.h
	@mkdir -p $(dir $@)
	$(Q)echo "  KCC     $<"
	$(Q)$(HOST_CC) $(KCFLAGS) -MMD -MP -c -o $@ $<
# The Matrox HAL (third_party/mgahal: MGA-Glide's, make sync-hal), the part
# without FPU code, with its port for this kernel (docs/milestones-m7.md step C).
MGAHAL := third_party/mgahal/hal
HAL_SRCS := $(addprefix $(MGAHAL)/src/,pci.c chip.c fifo.c engine.c blit.c cursor.c vram.c) $(MGAHAL)/port/glos.c
HAL_OBJS := $(patsubst $(MGAHAL)/%.c,build/mgahal/%.o,$(HAL_SRCS))
build/mgahal/%.o: $(MGAHAL)/%.c $(wildcard $(MGAHAL)/include/mga/*.h kernel/include/*.h)
	@mkdir -p $(dir $@)
	$(Q)echo "  HAL     $<"
	$(Q)$(HOST_CC) $(KCFLAGS) -DMGA_GLOS=1 -MMD -MP -c -o $@ $<
# lwIP 2.2.0 (third_party/lwip, BSD-3; THIRD_PARTY.md): its own code, built
# with the kernel's flags but without -Werror.
LWIP_SRCS := $(wildcard third_party/lwip/src/core/*.c third_party/lwip/src/core/ipv4/*.c \
               third_party/lwip/src/apps/mdns/*.c third_party/lwip/src/apps/sntp/*.c) \
             third_party/lwip/src/netif/ethernet.c
LWIP_OBJS := $(patsubst third_party/lwip/src/%.c,build/lwip/%.o,$(LWIP_SRCS))
build/lwip/%.o: third_party/lwip/src/%.c $(wildcard kernel/net/port/*.h kernel/net/port/arch/*.h)
	@mkdir -p $(dir $@)
	$(Q)echo "  LWIP    $<"
	$(Q)$(HOST_CC) $(filter-out -Werror -Wextra,$(KCFLAGS)) -c -o $@ $<
build/kernel/%.o: kernel/% $(wildcard kernel/include/*.h include/glos/*.h)
	@mkdir -p $(dir $@)
	$(Q)echo "  KCC     $<"
	$(Q)$(HOST_CC) $(KCFLAGS) -MMD -MP -c -o $@ $<
# (each object's headers, local ones too: kernel/app/appint.h, kernel/wm/deskint.h, the HAL's)
-include $(KOBJS:.o=.d) $(HAL_OBJS:.o=.d)
TINYSSH_OBJS := $(patsubst third_party/tinyssh/%.c,build/tinyssh/%.o,$(TINYSSH_SRCS))
build/tinyssh/%.o: third_party/tinyssh/%.c
	@mkdir -p $(dir $@)
	$(Q)echo "  TINYSSH $<"
	$(Q)$(HOST_CC) $(filter-out -Werror -Wextra,$(KCFLAGS)) -c -o $@ $<
build/kernel/glosk.elf: $(KOBJS) $(LWIP_OBJS) $(TINYSSH_OBJS) $(HAL_OBJS) kernel/kernel.ld
	$(Q)echo "  KLD     $@"
	$(Q)$(HOST_CC) $(KCFLAGS) -nostdlib -no-pie -Wl,-T,kernel/kernel.ld -Wl,--build-id=none \
	  -Wl,--no-warn-rwx-segments -o $@ $(KOBJS) $(LWIP_OBJS) $(TINYSSH_OBJS) $(HAL_OBJS)
build/kernel/GLOSK.BIN: build/kernel/glosk.elf
	$(Q)objcopy -O binary $< $@
kernel: build/kernel/GLOSK.BIN

# Touched only when the build id (git describe) changes, so the loader is
# rebuilt with the new id and otherwise left alone.
build/build_id: FORCE
	@mkdir -p build
	@echo '$(BUILD_ID)' | cmp -s - $@ || echo '$(BUILD_ID)' > $@
FORCE:
.PHONY: FORCE

loopa: all check-deps
	$(Q)$(DEV) python3 $(MGA_GLIDE)/tools/loopa/run.py --name glos --card $(CARD) \
	  --exe build/ow/GLOS.EXE --file build/kernel/GLOSK.BIN --args /ROUNDTRIP \
	  --out $(CURDIR)/out/loopa-$(CARD) --timeout $(LOOPA_TIMEOUT); cat out/loopa-$(CARD)/status

# M1's exit matrix: three machine profiles x raw and HIMEMX boots (tests/loopa/jobs.py).
loopa-m1: all check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py m1

# M2's exit: the HX tools' lines with and without GLOS, and the hostile
# programs killed (tests/loopa/jobs.py; JOBS runs at once).
JOBS ?= 3
loopa-m2: all dos-tests check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py m2 -j $(JOBS)
loopa-hostile: all dos-tests check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py hostile -j $(JOBS)
loopa-sched: all dos-tests check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py sched -j $(JOBS)
loopa-mem: all check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py mem -j $(JOBS)
loopa-shell: all check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py shell -j $(JOBS)
loopa-net: all check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py net -j $(JOBS)
# On the host: ssh is not in the dev container; jobs.py starts Loop A through it.
loopa-ssh: all dos-tests check-deps
	$(Q)python3 tests/loopa/jobs.py ssh -j $(JOBS)
# M3's exit: the ssh suite (its 486DX2 + NE2000 and bf6 + RTL8029 cases).
loopa-m3: loopa-ssh
# glos shot in every video mode (M5 A2): SHOTTEST's pictures on the Matrox and S3 VBE BIOSes and a 486.
loopa-shot: all dos-tests build/dj/SHOTDJ.EXE check-deps
	$(Q)python3 tests/loopa/jobs.py shot -j $(JOBS)
# M5's exit: a job driven entirely over SSH against its --keys/--shots twin.
loopa-m5exit: all build/dj/SHOTDJ.EXE check-deps
	$(Q)python3 tests/loopa/jobs.py m5exit -j $(JOBS)
# glos gdb (M5 A7): the host's gdb through ssh -W on a DJGPP program.
loopa-gdbprog: all build/dj/GDBME.EXE check-deps
	$(Q)python3 tests/loopa/jobs.py gdb -j $(JOBS)
# glos key, type and mouse (M5 A4) against the same input from Loop A's --keys.
# GLOS apps (M6 step A): APPTESTs at once, beside a spinning one, a session and a crash.
loopa-apps: all dos-tests djtst build/dj/APPTEST.EXE check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py apps -j $(JOBS)
# The desktop's screen and the 2D library (M6 step C): the test card on the S3 and the G200 against the host's.
loopa-display: all dos-tests build/dj/APPTEST.EXE host-test check-deps
	$(Q)python3 tests/loopa/jobs.py display -j $(JOBS)
# The desktop (M6 step D): the demonstration's windows on the S3 and the G200 against the host's composition.
loopa-desk: all dos-tests host-test check-deps
	$(Q)python3 tests/loopa/jobs.py desk -j $(JOBS)
# Windows, input and the API (M6 steps E and F): WINTESTs on the desktop, driven with glos key and glos mouse.
loopa-win: all dos-tests build/dj/WINTEST.EXE check-deps
	$(Q)python3 tests/loopa/jobs.py win -j $(JOBS)
# The GPU lock and apps' VRAM (M8 steps B and C): GPUTESTs drawing with the engine beside the desktop.
loopa-gpu: all dos-tests build/dj/GPUTEST.EXE check-deps
	$(Q)python3 tests/loopa/jobs.py gpu -j $(JOBS)
# GLOS's mixer (M8 step E): AUDTESTs through the SB16, recorded.
loopa-audio: all dos-tests build/dj/AUDTEST.EXE check-deps
	$(Q)python3 tests/loopa/jobs.py audio -j $(JOBS)
# Windowed DOS-GL (M8 step D): DOS-GL's conformance EXEs (from DOSGL_DIR's build) full-screen on plain DOS and
# windowed on GLOS desktops, their frames identical.
dosgl-inputs:
	@test -f $(DOSGL_DIR)/build/exe/conform/T01.EXE || { echo "dosgl-inputs: no DOS-GL build in $(DOSGL_DIR) (make conform-dos there)" >&2; exit 1; }
	@mkdir -p build/dosgl/conform
	$(Q)cp $(DOSGL_DIR)/build/exe/conform/T*.EXE build/dosgl/conform/
	@mkdir -p build/dosgl/sdl
	$(Q)for e in SDLINFO SDLKEYS SDLBEEP SDLGL SDLCRASH TEXCUBE; do \
	    test -f $(DOSGL_DIR)/build/exe/$$e.EXE && cp $(DOSGL_DIR)/build/exe/$$e.EXE build/dosgl/sdl/ || true; done
	$(Q)git -C $(DOSGL_DIR) log -1 --format="dosgl-inputs: DOS-GL %h %s" 2>/dev/null || true
loopa-glwin: all dos-tests dosgl-inputs check-deps
	$(Q)python3 tests/loopa/jobs.py glwin -j $(JOBS)
# The games windowed (M8 step G): Fifth Wheel (FIFTHWHEEL_DIR, built against DOS-GL unchanged) and PrBoom-plus
# (DOSGL_DIR's tools/doom/build.sh), on plain DOS and in windows on GLOS desktops.
games-inputs:
	@test -f $(FIFTHWHEEL_DIR)/build/dos/FWHEEL.EXE || { echo "games-inputs: no FWHEEL.EXE in $(FIFTHWHEEL_DIR)" >&2; exit 1; }
	@test -f $(DOSGL_DIR)/build/doom/PRBOOMP.EXE || { echo "games-inputs: no PRBOOMP.EXE in $(DOSGL_DIR) (make doom)" >&2; exit 1; }
	@mkdir -p build/games
	$(Q)cp $(FIFTHWHEEL_DIR)/build/dos/FWHEEL.EXE $(FIFTHWHEEL_DIR)/build/data/WORLD.PAK build/games/
	$(Q)cp $(DOSGL_DIR)/build/doom/PRBOOMP.EXE $(DOSGL_DIR)/build/doom/PRBOOM.WAD $(DOSGL_DIR)/tools/doom/games.json build/games/
loopa-games: all dos-tests games-inputs check-deps
	$(Q)python3 tests/loopa/jobs.py games -j $(JOBS)
# SDL3 windowed (M8 step F): DOS-GL's SDL examples (SDL patches 0008-0009) on GLOS desktops.
loopa-sdlwin: all dos-tests dosgl-inputs check-deps
	$(Q)python3 tests/loopa/jobs.py sdlwin -j $(JOBS)
# M8's exit (docs/milestones-m8.md step H): the four checks are in glwin (windowed = full-screen, the same EXEs
# on plain DOS, the hang), games (no underrun) and gpu (the watchdog).
loopa-m8: loopa-gpu loopa-audio loopa-glwin loopa-sdlwin loopa-games
# The toolkit (M9 step A): GUITEST's scripts (tests/gui) on the desktop against the host's goldens (gui-goldens).
loopa-gui: all dos-tests build/dj/GUITEST.EXE gui-goldens check-deps
	$(Q)python3 tests/loopa/jobs.py gui -j $(JOBS)
# The native apps (M9 steps D-G): their scripts (tests/gui) on the desktop against the host's goldens.
loopa-native: all dos-tests apps gui-goldens check-deps
	$(Q)python3 tests/loopa/jobs.py native -j $(JOBS)
# The settings (M9 step B): every setting set over ssh, the file kept and written, a new start reading it.
loopa-config: all check-deps
	$(Q)python3 tests/loopa/jobs.py config -j $(JOBS)
# Programs started by apps and console jobs (M9 step C): RUNTEST, CONIN, PMVGA.
loopa-console: all dos-tests build/dj/RUNTEST.EXE build/dj/PMVGA.EXE check-deps
	$(Q)python3 tests/loopa/jobs.py console -j $(JOBS)
# The Control Panel (M9 step G): every setting changed by keys alone, a screen mode kept and one let go back.
loopa-control: all apps check-deps
	$(Q)python3 tests/loopa/jobs.py control -j $(JOBS)
# The clock (M10 step A): SNTP from an NTP server on the host, the zone, the RTC and DOS, names.
loopa-time: all dos-tests check-deps
	$(Q)python3 tests/loopa/jobs.py time -j $(JOBS)
# M10 step B: an app's TCP connections (NETTEST) against an echo server on the host; an app killed with some open.
loopa-netapp: all build/dj/NETTEST.EXE check-deps
	$(Q)python3 tests/loopa/jobs.py netapp -j $(JOBS)
# M10 step C: HTTPS (FETCH) against servers on the host with a test CA, refusals, GitHub and a FreeDOS mirror.
loopa-https: all build/apps/FETCH.EXE build/host/mkcerts build/glos/CERTS.DAT check-deps
	$(Q)python3 tests/loopa/jobs.py https -j $(JOBS)
# M10 step F: VNC through an SSH tunnel with a stock client (vncdotool: tests/vnc), and glos rec.
loopa-vnc: all apps check-deps
	$(Q)python3 tests/loopa/jobs.py vnc -j $(JOBS)
# M10 step E: packages from a signed source on the host (tools/pkg), and FreeDOS's.
loopa-pkg: all build/apps/PKG.EXE build/host/mkcerts build/glos/CERTS.DAT build/glos/KEYS/PKG/GLOS.PUB dos-tests \
           check-deps
	$(Q)python3 tests/loopa/jobs.py pkg -j $(JOBS)
# M10 step D: the terminal's ssh, sftp and ssh-keygen against OpenSSH in a container (tests/sshd).
loopa-sshc: all build/apps/TERM.EXE check-deps
	$(Q)python3 tests/loopa/jobs.py sshc -j $(JOBS)
# M9's exit: the toolkit, the settings, console jobs, the apps, the Control Panel.
loopa-m9: loopa-gui loopa-config loopa-console loopa-native loopa-control
loopa-m10: loopa-time loopa-netapp loopa-https loopa-sshc loopa-pkg loopa-vnc loopa-gui
loopa-input: all dos-tests build/dj/DIRTEST.EXE check-deps
	$(Q)python3 tests/loopa/jobs.py input -j $(JOBS)
# M4a: the DPMI host's checks (DPMIMINI, DPMICONF-32 against CWSDPMI and HDPMI32i).
loopa-dpmi: all dos-tests build/dj/DPMICONF.EXE build/dj/LAZYCHK.EXE check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py dpmi -j $(JOBS)
loopa-m4a: loopa-dpmi
# M4b: DJGPP 2.05's tests and CRASHME against CWSDPMI; MGA-Glide's STACKPG, MOUSETST, JOYTEST, SBBEEP.
loopa-djtst: all dos-tests djtst check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py djtst -j $(JOBS)
loopa-dpmitools: all check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py dpmitools -j $(JOBS)
loopa-m4b: loopa-dpmi loopa-djtst loopa-dpmitools
# M4c: HDPMI's suite (bf6, 486DX2) and lDebugX (all three profiles), raw boots, each against HDPMI32i;
# then the programs under GLOS as the shell.
loopa-hdpmireg: all dos-tests m4c-inputs check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py hdpmireg --profile bf6 --profile 486dx2 --boot default -j $(JOBS)
loopa-ecm: all dos-tests m4c-inputs check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py ecm --boot default -j $(JOBS)
loopa-m4c-games: glos-cache check-deps
	$(Q)tools/m4c-games.sh
loopa-m4c: loopa-m4b loopa-hdpmireg loopa-ecm loopa-m4c-games
# M4d: DPMICONF-16 against HDPMI16 and HDPMI16i (all six profile and boot combinations), and TPX (Turbo
# Pascal 7's IDE on Borland's RTM) compiling and running a program, without GLOS and under it.
loopa-dpmi16: all dos-tests m4d-inputs check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py dpmi16 -j $(JOBS)
loopa-tpx: all dos-tests m4d-inputs check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py tpx --profile bf6 --profile 486dx2 --boot default -j $(JOBS)
loopa-m4d: loopa-m4c loopa-dpmi16 loopa-tpx
# M4e: exclusive sessions (on the host, as loopa-ssh: its agent cases use ssh).
loopa-sess: all dos-tests build/dj/PROFCHK.EXE build/dj/DIRTEST.EXE build/dj/djtst/ENABLE.EXE check-deps
	$(Q)python3 tests/loopa/jobs.py sess -j $(JOBS)
# M4e's exit: the gate (tools/gate/run.py). Its baselines are cached in $(MGA_CACHE)/glos/gate.
gate: all dos-tests djtst build/dj/DPMICONF.EXE build/dj/PROFCHK.EXE build/dj/DIRTEST.EXE build/dj/CRASHME.EXE \
      build/dj/LAZYCHK.EXE \
      m4c-inputs m4d-inputs glos-cache check-deps
	$(Q)python3 tools/gate/run.py -j $(JOBS)

# gdb attached to the kernel over COM2 (tools/gdb-loopa.sh).
loopa-gdb: all check-deps
	$(Q)$(DEV) tools/gdb-loopa.sh

# The M0 survey's own probe (tests/dos/iftest.c): DJGPP, run under CWSDPMI and HDPMI.
DJENV := env LD_LIBRARY_PATH=$(DJGPP_PREFIX)/hostlib
DJCC  := $(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-gcc
build/dj/IFTEST.EXE: tests/dos/iftest.c
	@mkdir -p $(dir $@)
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O1 -Wall -Werror -o $@ $<
survey-tools: build/dj/IFTEST.EXE
# DPMICONF-32 (tests/dos/dpmiconf.c, its handlers in dpmiconf_h.S): the DPMI host's conformance checks, DJGPP (M4a, M4b).
build/dj/DPMICONF.EXE: tests/dos/dpmiconf.c tests/dos/dpmiconf_h.S
	@mkdir -p $(dir $@)
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O1 -Wall -Werror -o $@ $^
# PROFCHK (tests/dos/profchk.c): what a profile gives a program, its environment and memory (M4e).
build/dj/PROFCHK.EXE: tests/dos/profchk.c
	@mkdir -p $(dir $@)
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O1 -Wall -Werror -o $@ $<
# LAZYCHK (tests/dos/lazychk.c): DPMI memory backed when first touched (M4e).
build/dj/LAZYCHK.EXE: tests/dos/lazychk.c
	@mkdir -p $(dir $@)
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O1 -Wall -Werror -o $@ $<
# SHOTDJ (tests/dos/shottest.c as a DPMI client): a known picture in each video mode, for glos shot (M5).
build/dj/SHOTDJ.EXE: tests/dos/shottest.c
	@mkdir -p $(dir $@)
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O1 -Wall -Werror -o $@ $<
# GDBME (tests/dos/gdbme.c): a program for glos gdb, and its symbols' addresses for the host's gdb, which
# reads no DJGPP executable (M5).
build/dj/GDBME.EXE: tests/dos/gdbme.c
	@mkdir -p $(dir $@)
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O0 -g -Wall -Werror -o $@ $<
	$(Q)$(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-nm $@ > build/dj/GDBME.sym
# DIRTEST (tests/dos/dirtest.c): a program at IOPL 3 and what GLOS takes back from it (M4e).
# libglos (sdk/libglos, include/glos/api.h): the GLOS API for DJGPP programs (M6), and APPTEST, the apps suite's app.
# The 2D library in it is the kernel's own code (kernel/gfx), built for the program.
LIBGLOS_GFX := kernel/gfx/gfx.c kernel/gfx/pal.c kernel/gfx/font.c kernel/gfx/region.c \
               kernel/gfx/fonts/ui.c kernel/gfx/fonts/bold.c kernel/gfx/fonts/mono.c
build/sdk/libglos.a: sdk/libglos/glos.c sdk/libglos/socket.c include/glos/api.h include/glos/gfx.h include/glos/net.h \
                     $(LIBGLOS_GFX) $(CFG_SRCS) $(CP_SRCS) include/glos/config.h include/glos/tz.h include/glos/cp.h
	@mkdir -p build/sdk
	$(Q)echo "  DJCC    $<"
	$(Q)rm -f $@ build/sdk/*.o
	$(Q)$(DJCC) -O2 -Wall -Werror -Iinclude -c -o build/sdk/glos.o $<
	$(Q)$(DJCC) -O2 -Wall -Werror -Iinclude -c -o build/sdk/socket.o sdk/libglos/socket.c
	$(Q)$(DJCC) -O2 -Wall -Werror -Iinclude -c -o build/sdk/cfg.o sdk/config/cfg.c
	$(Q)$(DJCC) -O2 -Wall -Werror -Iinclude -c -o build/sdk/tz.o sdk/config/tz.c
	$(Q)$(DJCC) -O2 -Wall -Werror -Iinclude -c -o build/sdk/cp.o sdk/config/cp.c
	$(Q)$(DJCC) -O2 -Wall -Werror -Iinclude -c -o build/sdk/cp_tables.o sdk/config/cp_tables.c
	$(Q)for f in $(LIBGLOS_GFX); do \
	    $(DJCC) -O2 -Wall -Werror -ffreestanding -Iinclude -Ikernel/include -c -o build/sdk/gfx_$$(basename $$f .c).o $$f || exit 1; \
	  done
	$(Q)$(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-ar rcs $@ build/sdk/*.o
# BearSSL 0.6 (third_party/bearssl, MIT; THIRD_PARTY.md; M10 step C): build/sdk/libbearssl.a for DJGPP. Its own
# guesses at the platform are set aside: no SSE2, AES-NI or RDRAND (a 486 has none, and a Pentium no SSE2), no
# /dev/urandom or Unix time (the TLS layer seeds it from GLOS_FN_RANDOM and gives it GLOS_FN_TIME's time).
BEARSSL_SRCS := $(sort $(wildcard third_party/bearssl/src/*/*.c))
BEARSSL_OBJS := $(patsubst third_party/bearssl/src/%.c,build/sdk/bearssl/%.o,$(BEARSSL_SRCS))
BEARSSL_FLAGS := -O2 -Ithird_party/bearssl/inc -Ithird_party/bearssl/src -DBR_SSE2=0 -DBR_AES_X86NI=0 -DBR_RDRAND=0 \
                 -DBR_USE_URANDOM=0 -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_RAND=0 -DBR_USE_WIN32_TIME=0 -DBR_POWER8=0
build/sdk/bearssl/%.o: third_party/bearssl/src/%.c $(wildcard third_party/bearssl/src/*.h third_party/bearssl/inc/*.h)
	@mkdir -p $(dir $@)
	$(Q)$(DJCC) $(BEARSSL_FLAGS) -c -o $@ $<
build/sdk/libbearssl.a: $(BEARSSL_OBJS)
	$(Q)echo "  AR      $@"
	$(Q)rm -f $@
	$(Q)$(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-ar rcs $@ $(BEARSSL_OBJS)
# ... and for the host (tools/certs/mkcerts, tests/host/tls_test), with its own guesses.
BEARSSL_HOST_OBJS := $(patsubst third_party/bearssl/src/%.c,build/host/bearssl/%.o,$(BEARSSL_SRCS))
build/host/bearssl/%.o: third_party/bearssl/src/%.c $(wildcard third_party/bearssl/src/*.h third_party/bearssl/inc/*.h)
	@mkdir -p $(dir $@)
	$(Q)$(HOST_CC) -O2 -Ithird_party/bearssl/inc -Ithird_party/bearssl/src -c -o $@ $<
build/host/libbearssl.a: $(BEARSSL_HOST_OBJS)
	$(Q)echo "  AR      $@"
	$(Q)rm -f $@
	$(Q)ar rcs $@ $(BEARSSL_HOST_OBJS)
# TLS and HTTP for apps (sdk/tls, sdk/http; include/glos/tls.h, http.h; M10 step C).
build/sdk/libtls.a: sdk/tls/tls.c sdk/tls/anchors.c include/glos/tls.h include/glos/api.h
	@mkdir -p build/sdk/tls
	$(Q)echo "  DJCC    sdk/tls"
	$(Q)rm -f $@
	$(Q)$(DJCC) -O2 -Wall -Werror -Iinclude -Ithird_party/bearssl/inc -c -o build/sdk/tls/tls.o sdk/tls/tls.c
	$(Q)$(DJCC) -O2 -Wall -Werror -Iinclude -Ithird_party/bearssl/inc -c -o build/sdk/tls/anchors.o sdk/tls/anchors.c
	$(Q)$(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-ar rcs $@ build/sdk/tls/tls.o build/sdk/tls/anchors.o
build/sdk/libhttp.a: sdk/http/http.c include/glos/http.h include/glos/tls.h include/glos/net.h include/glos/api.h
	@mkdir -p build/sdk/http
	$(Q)echo "  DJCC    sdk/http"
	$(Q)rm -f $@
	$(Q)$(DJCC) -O2 -Wall -Werror -Wno-format-truncation -Iinclude -c -o build/sdk/http/http.o sdk/http/http.c
	$(Q)$(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-ar rcs $@ build/sdk/http/http.o
# The SSH and SFTP client (sdk/sshc, glos/sshc.h, glos/sftpc.h; M10 step D): its own code, the kernel's wire
# encoding and key parser (kernel/ssh/sshbuf.c, sshkeys.c), TinySSH's crypto, randombytes() from GLOS_FN_RANDOM.
build/sdk/libsshc.a: $(SSHC_SRCS) sdk/sshc/random.c $(TINYSSH_SRCS) include/glos/sshc.h include/glos/sftpc.h \
                     kernel/ssh/sshbuf.h include/glos/api.h
	@mkdir -p build/sdk/sshc
	$(Q)echo "  DJCC    sdk/sshc"
	$(Q)rm -f $@ build/sdk/sshc/*.o
	$(Q)for f in $(SSHC_SRCS) sdk/sshc/random.c; do \
	    $(DJCC) -O2 -Wall -Werror -Wno-format-truncation $(SSHC_FLAGS) -c -o build/sdk/sshc/$$(basename $$f .c).o $$f || exit 1; \
	  done
	$(Q)for f in $(TINYSSH_SRCS); do \
	    $(DJCC) -O2 $(SSHC_FLAGS) -c -o build/sdk/sshc/ts_$$(basename $$f .c).o $$f || exit 1; \
	  done
	$(Q)$(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-ar rcs $@ build/sdk/sshc/*.o
# ... on the host, against OpenSSH's sshd (one of the user's own, on a free port): make sshc-test.
build/host/sshc_test: tests/host/sshc_test.c $(SSHC_SRCS) $(TINYSSH_SRCS) include/glos/sshc.h include/glos/sftpc.h
	@mkdir -p build/host
	$(Q)echo "  CC      $@"
	$(Q)gcc -O2 -g -Wall -Wextra -Wno-format-truncation $(SSHC_FLAGS) -o $@ tests/host/sshc_test.c $(SSHC_SRCS) \
	  $(TINYSSH_SRCS)
sshc-test: build/host/sshc_test
	$(Q)tests/host/sshc_test.sh build/host/sshc_test
NET_LIBS := build/sdk/libhttp.a build/sdk/libtls.a build/sdk/libbearssl.a build/sdk/libglos.a
# FETCH (apps/fetch): a file over HTTPS or HTTP, for the terminal and SSH.
build/apps/FETCH.EXE: apps/fetch/fetch.c $(NET_LIBS) tools/glosmark.py
	@mkdir -p build/apps
	$(Q)echo "  DJCC    apps/fetch"
	$(Q)$(DJCC) -O2 -Wall -Werror -Wno-format-truncation -Iinclude -Ithird_party/bearssl/inc -o $@ $< $(NET_LIBS)
	$(Q)python3 tools/glosmark.py $@
# PKG (apps/pkg; M10 step E): packages from signed sources. Its zips through stb_image's inflater, signatures with
# TinySSH's Ed25519 (in libsshc), downloads with sdk/http.
build/apps/PKG.EXE: $(wildcard apps/pkg/*.c apps/pkg/*.h) $(NET_LIBS) build/sdk/libsshc.a build/sdk/libgui.a tools/glosmark.py
	@mkdir -p build/apps
	$(Q)echo "  DJCC    apps/pkg"
	$(Q)$(DJCC) -O2 -Wall -Werror -Wno-format-truncation -Iinclude -Ithird_party/bearssl/inc -Ithird_party/stb \
	  -Ikernel/ssh -Ithird_party/tinyssh -Ithird_party/tinyssh/cryptoint -o $@ $(wildcard apps/pkg/*.c) \
	  build/sdk/libhttp.a build/sdk/libtls.a build/sdk/libbearssl.a build/sdk/libsshc.a build/sdk/libgui.a \
	  build/sdk/libglos.a -lm
	$(Q)python3 tools/glosmark.py $@
# C:\GLOS\CERTS.DAT: the trust anchors, from curl's cacert.pem (pinned in tools/setup/versions.mk).
build/host/mkcerts: tools/certs/mkcerts.c sdk/tls/anchors.c include/glos/tls.h build/host/libbearssl.a
	@mkdir -p build/host
	$(Q)echo "  CC      $@"
	$(Q)$(HOST_CC) -O2 -Wall -Werror -Iinclude -Ithird_party/bearssl/inc -o $@ tools/certs/mkcerts.c sdk/tls/anchors.c \
	  build/host/libbearssl.a
build/glos/CERTS.DAT: build/host/mkcerts tools/setup/versions.mk
	@mkdir -p build/glos
	$(Q)tools/setup/fetch.sh $(CACERT_URL) $(CACERT_SHA256) $(MGA_CACHE)/dl/$(notdir $(CACERT_URL))
	$(Q)build/host/mkcerts $@ $(MGA_CACHE)/dl/$(notdir $(CACERT_URL))
# C:\GLOS\KEYS\PKG\GLOS.PUB: the owner's package key (PRD D58; made offline with tools/pkg/pkg.py keygen, the
# private half never on a build host), which the GLOS source's INDEX.SIG must be signed by.
build/glos/KEYS/PKG/GLOS.PUB: tools/pkg/GLOS.PUB
	@mkdir -p build/glos/KEYS/PKG
	$(Q)python3 -c "import base64,sys; w=open(sys.argv[1]).read().split(); \
	  sys.exit(0 if w[0] == 'glos-pkg-ed25519' and len(base64.b64decode(w[1], validate=True)) == 32 else 'bad key')" $<
	$(Q)cp $< $@
# The toolkit (sdk/gui, include/glos/gui.h; M9): build/sdk/libgui.a, linked before libglos.a.
build/sdk/libgui.a: $(GUI_SRCS) sdk/gui/guiint.h include/glos/gui.h include/glos/api.h include/glos/gfx.h
	@mkdir -p build/sdk/gui
	$(Q)echo "  DJCC    sdk/gui"
	$(Q)rm -f $@ build/sdk/gui/*.o
	$(Q)for f in $(GUI_SRCS); do \
	    $(DJCC) -O2 -Wall -Werror -Iinclude -c -o build/sdk/gui/$$(basename $$f .c).o $$f || exit 1; \
	  done
	$(Q)$(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-ar rcs $@ build/sdk/gui/*.o
build/dj/PMVGA.EXE: tests/dos/pmvga.c
	@mkdir -p build/dj
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O1 -Wall -Werror -o $@ $<
# NETTEST (tests/dos/nettest.c; M10 step B): an app's connections, and the sockets on them.
build/dj/NETTEST.EXE: tests/dos/nettest.c build/sdk/libglos.a tools/glosmark.py
	@mkdir -p build/dj
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O1 -Wall -Werror -Iinclude -o $@ $< build/sdk/libglos.a
	$(Q)python3 tools/glosmark.py $@
build/dj/RUNTEST.EXE: tests/dos/runtest.c build/sdk/libglos.a tools/glosmark.py
	@mkdir -p build/dj
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O1 -Wall -Werror -Iinclude -o $@ $< build/sdk/libglos.a
	$(Q)python3 tools/glosmark.py $@
build/dj/GUITEST.EXE: tests/dos/guitest.c build/sdk/libgui.a build/sdk/libglos.a tools/glosmark.py
	@mkdir -p build/dj
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O2 -Wall -Werror -Iinclude -o $@ $< build/sdk/libgui.a build/sdk/libglos.a
	$(Q)python3 tools/glosmark.py $@
# The native apps for DOS: build/apps/NAME.EXE.
APP_EXES := $(foreach a,$(APPS),build/apps/$(shell echo $(a) | tr a-z A-Z).EXE)
define app_exe
build/apps/$(1).EXE: $$(wildcard apps/$(2)/*.c apps/$(2)/*.h) build/sdk/libgui.a build/sdk/libglos.a tools/glosmark.py $$(APP_LIBS_$(2))
	@mkdir -p build/apps
	$$(Q)echo "  DJCC    apps/$(2)"
	$$(Q)$$(DJCC) -O2 -Wall -Werror -Wno-format-truncation -Iinclude -Ithird_party/stb -o $$@ $$(wildcard apps/$(2)/*.c) build/sdk/libgui.a \
	  $$(APP_LIBS_$(2)) build/sdk/libglos.a -lm
	$$(Q)python3 tools/glosmark.py $$@
endef
$(foreach a,$(APPS),$(eval $(call app_exe,$(shell echo $(a) | tr a-z A-Z),$(a))))
apps: $(APP_EXES)
build/dj/APPTEST.EXE: tests/dos/apptest.c build/sdk/libglos.a tools/glosmark.py
	@mkdir -p build/dj
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O1 -Wall -Werror -Iinclude -o $@ $< build/sdk/libglos.a
	$(Q)python3 tools/glosmark.py $@
# WINTEST (tests/dos/wintest.c): an app with a window, for the win suite (M6 steps E and F).
build/dj/WINTEST.EXE: tests/dos/wintest.c build/sdk/libglos.a tools/glosmark.py
	@mkdir -p build/dj
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O1 -Wall -Werror -Iinclude -o $@ $< build/sdk/libglos.a
	$(Q)python3 tools/glosmark.py $@
# GPUTEST (tests/dos/gputest.c): an app that draws with the Matrox engine from its own copy of the HAL (M8).
GPUTEST_HAL := $(addprefix $(MGAHAL)/,src/pci.c src/chip.c src/fifo.c src/engine.c src/blit.c src/debug/serial.c \
               port/djgpp.c)
build/dj/GPUTEST.EXE: tests/dos/gputest.c build/sdk/libglos.a $(GPUTEST_HAL) tools/glosmark.py
	@mkdir -p build/dj
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O1 -Wall -Werror -DMGA_DJGPP=1 -Iinclude -I$(MGAHAL)/include -o $@ $< $(GPUTEST_HAL) build/sdk/libglos.a
	$(Q)python3 tools/glosmark.py $@
# AUDTEST (tests/dos/audtest.c): a tone through GLOS's mixer (M8 step E).
build/dj/AUDTEST.EXE: tests/dos/audtest.c build/sdk/libglos.a tools/glosmark.py
	@mkdir -p build/dj
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O1 -Wall -Werror -Iinclude -o $@ $< build/sdk/libglos.a -lm
	$(Q)python3 tools/glosmark.py $@
build/dj/DIRTEST.EXE: tests/dos/dirtest.c
	@mkdir -p $(dir $@)
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O1 -Wall -Werror -o $@ $<
# CRASHME (tests/dos/crashme.c): a fault with no handler, for the crash report (M4b).
build/dj/CRASHME.EXE: tests/dos/crashme.c
	@mkdir -p $(dir $@)
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O1 -g -Wall -Werror -o $@ $<

# DJGPP 2.05's own tests (djtst205.zip, pinned in tools/setup/versions.mk), built
# from the cached archive: NAME:path under tests/libc (M4b's exit, jobs.py djtst).
include tools/setup/versions.mk
MGA_CACHE ?= $(HOME)/.cache/mga-glide
DJTST := FAULT:go32/fault NULL:crt0/null FPU:go32/fpu RAISE:go32/raise INFOBLK:go32/infoblk BRK:crt0/brk \
         MULTISPN:crt0/multispn NEAR:pc_hw/nearptr/near NEAR2:pc_hw/nearptr/near2 NEAR3:pc_hw/nearptr/near3 \
         ENABLE:pc_hw/hwint/enable GETOCW:pc_hw/fpu/getocw STAT:pc_hw/fpu/stat TIMER:go32/timer HANG:go32/hang \
         CTRLC:go32/ctrlc SIGNALS:go32/signals UCLOCK:pc_hw/timer/uclock
DJTST_EXES := $(foreach t,$(DJTST),build/dj/djtst/$(word 1,$(subst :, ,$(t))).EXE)
build/dj/djtst/.unpacked: tools/setup/versions.mk
	@mkdir -p build/dj/djtst/src
	$(Q)tools/setup/fetch.sh $(DJTST_URL) $(DJTST_SHA256) $(MGA_CACHE)/dl/djtst205.zip
	$(Q)unzip -qo $(MGA_CACHE)/dl/djtst205.zip -d build/dj/djtst/src
	$(Q)touch $@
define djtst_rule
build/dj/djtst/$(1).EXE: build/dj/djtst/.unpacked
	$$(Q)echo "  DJCC    djtst $(2)"
	$$(Q)$$(DJCC) -O2 -w -o $$@ build/dj/djtst/src/tests/libc/$(2).c -lm
endef
$(foreach t,$(DJTST),$(eval $(call djtst_rule,$(word 1,$(subst :, ,$(t))),$(word 2,$(subst :, ,$(t))))))
djtst: $(DJTST_EXES) build/dj/CRASHME.EXE build/ow/dos/RUNOUT.EXE

# HDPMI's regression tests and ecm's lDebugX tests (M4c; pinned in versions.mk).
build/hdpmireg/.unpacked: tools/setup/versions.mk
	@mkdir -p build/hdpmireg
	$(Q)tools/setup/fetch.sh $(HDPMIREG_URL) $(HDPMIREG_SHA256) $(MGA_CACHE)/dl/hdpmi-regression-f2276db9accf.zip
	$(Q)unzip -qoL $(MGA_CACHE)/dl/hdpmi-regression-f2276db9accf.zip -d build/hdpmireg
	$(Q)touch $@
build/ecm/.unpacked: tools/setup/versions.mk
	@mkdir -p build/ecm
	$(Q)tools/setup/fetch.sh $(ECMTEST_URL) $(ECMTEST_SHA256) $(MGA_CACHE)/dl/ecm-test-20210127.zip
	$(Q)unzip -qoL $(MGA_CACHE)/dl/ecm-test-20210127.zip -d build/ecm
	$(Q)touch $@
m4c-inputs: build/hdpmireg/.unpacked build/ecm/.unpacked build/ow/dos/RUNOUT.EXE
# HDPMI16 and HDPMI16i, DPMICONF-16's baselines, from the pinned HX runtime (M4d).
GLOS_HX := $(MGA_CACHE)/glos/hx
$(GLOS_HX)/.hdpmi16: tools/setup/versions.mk
	@mkdir -p $(GLOS_HX)
	$(Q)tools/setup/fetch.sh $(HXRT_URL) $(HXRT_SHA256) $(MGA_CACHE)/dl/HXRT223.zip
	$(Q)unzip -p $(MGA_CACHE)/dl/HXRT223.zip BIN/HDPMI16.EXE > $(GLOS_HX)/HDPMI16.EXE
	$(Q)unzip -p $(MGA_CACHE)/dl/HXRT223.zip BIN/HDPMI16i.EXE > $(GLOS_HX)/HDPMI16I.EXE
	$(Q)touch $@
# TPX (Turbo Pascal 7's protected-mode IDE) and its RTM, from $(BORLAND_DIR) (private: copied into the
# cache, which the dev container mounts, and never into the repository).
GLOS_BORLAND := $(MGA_CACHE)/glos/borland
TPX_FILES := TPX.EXE RTM.EXE DPMI16BI.OVL TURBO.TPL
$(GLOS_BORLAND)/.tpx: $(addprefix $(BORLAND_DIR)/TP7/,$(TPX_FILES))
	@mkdir -p $(GLOS_BORLAND)
	$(Q)cp $^ $(GLOS_BORLAND)/
	$(Q)touch $@
m4d-inputs: $(GLOS_HX)/.hdpmi16 build/ow/dos/DPMI16.EXE build/dj/DPMICONF.EXE $(GLOS_BORLAND)/.tpx

# GLOS for other repos' Loop A jobs (M4c): in the cache the dev container
# mounts, for LOOPA_EXTRA_ARGS="--boot-cfg glosshell --file $(GLOS_CACHE)/GLOS.EXE=/TEST/GLOS.EXE ..."
GLOS_CACHE := $(MGA_CACHE)/glos/bin
glos-cache: all
	@mkdir -p $(GLOS_CACHE)/direct $(GLOS_CACHE)/keys
	$(Q)cp build/ow/GLOS.EXE build/kernel/GLOSK.BIN $(GLOS_CACHE)/
	$(Q)cp tests/keys/hostkey $(GLOS_CACHE)/keys/HOSTKEY
	$(Q)cp tests/keys/AUTHKEYS $(GLOS_CACHE)/keys/AUTHKEYS
	$(Q)printf '; every program in direct mode (M4C_DIRECT=1, the survey variant glos-direct)\r\n[shell]\r\noptions = /DIRECT\r\n' \
	    > $(GLOS_CACHE)/direct/GLOS.CFG

clean:
	rm -rf build out
