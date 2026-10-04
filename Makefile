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

.PHONY: all dos-tests kernel host-test ssh-test loopa loopa-m1 loopa-m2 loopa-hostile loopa-sched loopa-mem loopa-shell loopa-net loopa-ssh loopa-m3 loopa-dpmi loopa-m4a loopa-djtst loopa-dpmitools loopa-m4b loopa-gdb djtst check-deps clean help survey-tools
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
# DPMIMINI.COM: the smallest DPMI client, in assembly (M4a).
build/ow/dos/DPMIMINI.COM: tests/dos/dpmimini.asm
	@mkdir -p build/ow/dos/obj
	$(Q)echo "  WASM    $<"
	$(Q)$(OWENV) $(OWBIN)/wasm -q -fo=build/ow/dos/obj/dpmimini.obj $<
	$(Q)$(WLINK) format dos com option quiet name $@ file build/ow/dos/obj/dpmimini.obj
DOS_TESTS := build/ow/dos/HOSTILE.EXE build/ow/dos/XMSTEST.EXE build/ow/dos/ECHOARGS.EXE build/ow/dos/DPMIMINI.COM \
             build/ow/dos/RUNOUT.EXE
dos-tests: $(DOS_TESTS)

# ---- GLOSK.BIN: the kernel (host gcc -m32, linked at C0100000h) -------------
KCFLAGS := -m32 -march=i486 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
           -fno-asynchronous-unwind-tables -fno-delete-null-pointer-checks -mgeneral-regs-only \
           -O2 -g -Wall -Wextra -Werror -nostdinc -Iinclude -Ikernel/include \
           -isystem $(shell $(HOST_CC) -m32 -print-file-name=include) -Ikernel/include/libc -Ikernel/net/port \
           -Ithird_party/lwip/src/include -Ithird_party/tinyssh -Ithird_party/tinyssh/cryptoint -Ikernel/ssh
KSRCS := kernel/entry.S kernel/arch/stubs.S kernel/arch/cpu.c kernel/core/main.c kernel/core/timer.c \
         kernel/core/sched.c kernel/drv/pci.c kernel/drv/ne2k.c kernel/net/net.c kernel/lib/libc.c \
         kernel/core/random.c kernel/core/kat.c kernel/ssh/ssh.c kernel/ssh/sshbuf.c kernel/ssh/sshkeys.c \
         kernel/ssh/sshd.c \
         kernel/drv/serial.c kernel/lib/kprintf.c kernel/mm/pmm.c kernel/mm/heap.c kernel/mm/vmm.c \
         kernel/dbg/gdbstub.c kernel/vm/v86.c kernel/vm/v86dec.c kernel/vm/vpic.c kernel/vm/vdev.c \
         kernel/vm/vkbc.c kernel/vm/int15.c kernel/vm/xms.c kernel/dos/agent.c \
         kernel/dos/shot.c kernel/lib/png.c kernel/dos/dos.c kernel/ssh/sftp.c \
         kernel/dpmi/host.c kernel/dpmi/ldt.c kernel/dpmi/mem.c kernel/dpmi/rmcall.c kernel/dpmi/int31.c \
         kernel/dpmi/deliver.c kernel/dbg/crash.c
KOBJS := $(patsubst kernel/%,build/kernel/%.o,$(KSRCS))
# lwIP 2.2.0 (third_party/lwip, BSD-3; THIRD_PARTY.md): its own code, built
# with the kernel's flags but without -Werror.
LWIP_SRCS := $(wildcard third_party/lwip/src/core/*.c third_party/lwip/src/core/ipv4/*.c) \
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
# M4a: the DPMI host's checks (DPMIMINI, DPMICONF-32 against CWSDPMI and HDPMI32i).
loopa-dpmi: all dos-tests build/dj/DPMICONF.EXE check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py dpmi -j $(JOBS)
loopa-m4a: loopa-dpmi
# M4b: DJGPP 2.05's tests and CRASHME against CWSDPMI; MGA-Glide's STACKPG, MOUSETST, JOYTEST, SBBEEP.
loopa-djtst: all dos-tests djtst check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py djtst -j $(JOBS)
loopa-dpmitools: all check-deps
	$(Q)$(DEV) python3 tests/loopa/jobs.py dpmitools -j $(JOBS)
loopa-m4b: loopa-dpmi loopa-djtst loopa-dpmitools

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

clean:
	rm -rf build out
