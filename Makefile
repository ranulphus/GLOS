# GLOS (Graphics Library Operating System, for DOS). See PRD.md.
#
#   make                build/ow/GLOS.EXE (16-bit loader) and build/kernel/GLOSK.BIN (the kernel)
#   make loopa [CARD=g450]   run GLOS.EXE in 86Box through MGA-Glide's Loop A: out/loopa-CARD/
#   make check-deps     MGA-Glide at or after deps.mk's pin
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
#   make gate           M4e's exit: the baseline matrix under GLOS and with direct mode forced, a kill and
#                       an ssh probe for every program (tools/gate/run.py; on the host; hours)
#   make survey-tools   build/dj/IFTEST.EXE (DJGPP) for tools/survey/survey.py
include config.mk
-include config.local.mk
include deps.mk

DEV      := $(MGA_GLIDE)/tools/dev
BUILD_ID := $(shell git describe --always --dirty 2>/dev/null || echo unknown)
Q ?= @

OWBIN  := $(WATCOM)/binl64
OWENV  := env WATCOM=$(WATCOM) INCLUDE=$(WATCOM)/h PATH=$(OWBIN):$(PATH)
WCC16  := $(OWENV) $(OWBIN)/wcc
WLINK  := $(OWENV) $(OWBIN)/wlink

.PHONY: loopa-apps loopa-display loopa-desk loopa-win fonts all dos-tests kernel host-test ssh-test loopa loopa-m1 loopa-m2 loopa-hostile loopa-sched loopa-mem loopa-shell loopa-net loopa-ssh loopa-m3 loopa-dpmi loopa-m4a loopa-djtst loopa-dpmitools loopa-m4b loopa-hdpmireg loopa-ecm loopa-m4c-games loopa-m4c loopa-dpmi16 loopa-tpx loopa-m4d loopa-sess gate loopa-gdb djtst glos-cache m4c-inputs m4d-inputs check-deps clean help survey-tools
all: build/ow/GLOS.EXE build/kernel/GLOSK.BIN

help:
	@sed -n '3,/^[^#]/p' Makefile | grep '^#' | sed 's/^# //'

check-deps:
	@git -C "$(MGA_GLIDE)" merge-base --is-ancestor "$(MGA_GLIDE_PIN)" HEAD 2>/dev/null \
	  || { echo "$(MGA_GLIDE) is not at or after $(MGA_GLIDE_PIN) (deps.mk)"; exit 1; }

# ---- Host tests: kernel code built 32-bit for Linux (in the dev container,
# which has the 32-bit C library) --------------------------------------------
# TinySSH's crypto (third_party/tinyssh; THIRD_PARTY.md): the SSH algorithms only.
TINYSSH_SRCS := $(filter-out %_lib25519.c %_lib1305.c,$(wildcard third_party/tinyssh/*.c)) \
                $(foreach t,int8 int16 int32 int64 uint8 uint16 uint32 uint64,third_party/tinyssh/cryptoint/$(t)_optblocker.c)
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
	  tests/host/gfx_test.c $(GFX_SRCS) kernel/lib/png.c && build/host/gfx_test
	@$(HOST_CC) -m32 -O1 -g -Wall -Wextra -Werror -Ikernel/include -Iinclude -o build/host/wm_test \
	  tests/host/wm_test.c $(WM_SRCS) $(GFX_SRCS) kernel/lib/png.c && build/host/wm_test

# (`make fonts` makes kernel/gfx/fonts again from FONT_DIR's TrueType files: on the host, with python3-pil.)
FONT_DIR ?= /usr/share/fonts/truetype/dejavu
fonts:
	$(Q)python3 tools/mkfont.py $(FONT_DIR)/DejaVuSans.ttf 11 ui > kernel/gfx/fonts/ui.c
	$(Q)python3 tools/mkfont.py $(FONT_DIR)/DejaVuSans-Bold.ttf 11 bold > kernel/gfx/fonts/bold.c
	$(Q)python3 tools/mkfont.py $(FONT_DIR)/DejaVuSansMono.ttf 11 mono > kernel/gfx/fonts/mono.c

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
build/ow/obj16/%.obj: loader/%.c include/glos/bootinfo.h build/build_id
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC16   $<"
	$(Q)$(WCC16) -bt=dos -ms -3 -os -zq -we -iinclude -dGLOS_BUILD="\"$(BUILD_ID)\"" -fo=$@ $<
build/ow/obj16/%.obj: loader/%.asm
	@mkdir -p $(dir $@)
	$(Q)echo "  WASM    $<"
	$(Q)$(OWENV) $(OWBIN)/wasm -q -fo=$@ $<
build/ow/GLOS.EXE: build/ow/obj16/main.obj build/ow/obj16/lowlevel.obj build/ow/obj16/stub.obj
	$(Q)echo "  WLINK   $@"
	$(Q)$(WLINK) system dos option quiet option stack=4k option map=build/ow/GLOS.map name $@ \
	  file build/ow/obj16/main.obj,build/ow/obj16/lowlevel.obj,build/ow/obj16/stub.obj

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
             build/ow/dos/SHOTTEST.EXE build/ow/dos/TYPEIN.EXE
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
           -Ithird_party/lwip/src/include -Ithird_party/tinyssh -Ithird_party/tinyssh/cryptoint -Ikernel/ssh
KSRCS := kernel/entry.S kernel/arch/stubs.S kernel/arch/cpu.c kernel/core/main.c kernel/core/timer.c \
         kernel/core/sched.c kernel/drv/pci.c kernel/drv/nic.c kernel/drv/ne2k.c kernel/drv/rtl8139.c kernel/drv/e100.c \
         kernel/net/net.c kernel/lib/libc.c \
         kernel/core/random.c kernel/core/kat.c kernel/ssh/ssh.c kernel/ssh/sshbuf.c kernel/ssh/sshkeys.c \
         kernel/ssh/sshd.c \
         kernel/drv/serial.c kernel/lib/kprintf.c kernel/mm/pmm.c kernel/mm/heap.c kernel/mm/vmm.c \
         kernel/dbg/gdbstub.c kernel/dbg/gdbprog.c kernel/vm/v86.c kernel/vm/session.c kernel/vm/v86dec.c kernel/vm/vpic.c kernel/vm/vdev.c \
         kernel/vm/vkbc.c kernel/vm/int15.c kernel/vm/xms.c kernel/dos/agent.c \
         kernel/dos/shot.c kernel/lib/png.c kernel/dos/dos.c kernel/ssh/sftp.c \
         kernel/dpmi/host.c kernel/dpmi/ldt.c kernel/dpmi/mem.c kernel/dpmi/rmcall.c kernel/dpmi/int31.c \
         kernel/dpmi/deliver.c kernel/dpmi/level.c kernel/dpmi/dosx.c kernel/dbg/crash.c \
         kernel/app/app.c kernel/app/api.c kernel/app/dev.c $(GFX_SRCS) kernel/gfx/display.c $(WM_SRCS) kernel/wm/desk.c kernel/wm/deskin.c kernel/wm/deskapp.c
KOBJS := $(patsubst kernel/%,build/kernel/%.o,$(KSRCS))
# lwIP 2.2.0 (third_party/lwip, BSD-3; THIRD_PARTY.md): its own code, built
# with the kernel's flags but without -Werror.
LWIP_SRCS := $(wildcard third_party/lwip/src/core/*.c third_party/lwip/src/core/ipv4/*.c \
               third_party/lwip/src/apps/mdns/*.c) \
             third_party/lwip/src/netif/ethernet.c
LWIP_OBJS := $(patsubst third_party/lwip/src/%.c,build/lwip/%.o,$(LWIP_SRCS))
build/lwip/%.o: third_party/lwip/src/%.c $(wildcard kernel/net/port/*.h kernel/net/port/arch/*.h)
	@mkdir -p $(dir $@)
	$(Q)echo "  LWIP    $<"
	$(Q)$(HOST_CC) $(filter-out -Werror -Wextra,$(KCFLAGS)) -c -o $@ $<
build/kernel/%.o: kernel/% $(wildcard kernel/include/*.h include/glos/*.h)
	@mkdir -p $(dir $@)
	$(Q)echo "  KCC     $<"
	$(Q)$(HOST_CC) $(KCFLAGS) -c -o $@ $<
TINYSSH_OBJS := $(patsubst third_party/tinyssh/%.c,build/tinyssh/%.o,$(TINYSSH_SRCS))
build/tinyssh/%.o: third_party/tinyssh/%.c
	@mkdir -p $(dir $@)
	$(Q)echo "  TINYSSH $<"
	$(Q)$(HOST_CC) $(filter-out -Werror -Wextra,$(KCFLAGS)) -c -o $@ $<
build/kernel/glosk.elf: $(KOBJS) $(LWIP_OBJS) $(TINYSSH_OBJS) kernel/kernel.ld
	$(Q)echo "  KLD     $@"
	$(Q)$(HOST_CC) $(KCFLAGS) -nostdlib -no-pie -Wl,-T,kernel/kernel.ld -Wl,--build-id=none \
	  -Wl,--no-warn-rwx-segments -o $@ $(KOBJS) $(LWIP_OBJS) $(TINYSSH_OBJS)
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
build/sdk/libglos.a: sdk/libglos/glos.c include/glos/api.h include/glos/gfx.h $(LIBGLOS_GFX)
	@mkdir -p build/sdk
	$(Q)echo "  DJCC    $<"
	$(Q)rm -f $@ build/sdk/*.o
	$(Q)$(DJCC) -O2 -Wall -Werror -Iinclude -c -o build/sdk/glos.o $<
	$(Q)for f in $(LIBGLOS_GFX); do \
	    $(DJCC) -O2 -Wall -Werror -ffreestanding -Iinclude -Ikernel/include -c -o build/sdk/gfx_$$(basename $$f .c).o $$f || exit 1; \
	  done
	$(Q)$(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-ar rcs $@ build/sdk/*.o
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
