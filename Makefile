# GLOS (Graphics Library Operating System, for DOS). See PRD.md.
#
#   make                build/ow/GLOS.EXE (16-bit loader) and build/kernel/GLOSK.BIN (the kernel)
#   make loopa [CARD=g450]   run GLOS.EXE in 86Box through MGA-Glide's Loop A: out/loopa-CARD/
#   make check-deps     MGA-Glide at or after deps.mk's pin
#   make loopa-m1       M1's exit matrix in Loop A (tests/loopa/jobs.py)
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

.PHONY: all kernel loopa loopa-m1 check-deps clean help survey-tools
all: build/ow/GLOS.EXE build/kernel/GLOSK.BIN

help:
	@sed -n '3,7p' Makefile | sed 's/^# //'

check-deps:
	@git -C "$(MGA_GLIDE)" merge-base --is-ancestor "$(MGA_GLIDE_PIN)" HEAD 2>/dev/null \
	  || { echo "$(MGA_GLIDE) is not at or after $(MGA_GLIDE_PIN) (deps.mk)"; exit 1; }

# ---- GLOS.EXE: the 16-bit loader (Open Watcom, small model) ----------------
LOADER_C   := loader/main.c
LOADER_ASM := loader/lowlevel.asm
build/ow/obj16/%.obj: loader/%.c include/glos/bootinfo.h build/build_id
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC16   $<"
	$(Q)$(WCC16) -bt=dos -ms -3 -os -zq -we -iinclude -dGLOS_BUILD="\"$(BUILD_ID)\"" -fo=$@ $<
build/ow/obj16/lowlevel.obj: $(LOADER_ASM)
	@mkdir -p $(dir $@)
	$(Q)echo "  WASM    $<"
	$(Q)$(OWENV) $(OWBIN)/wasm -q -fo=$@ $<
build/ow/GLOS.EXE: build/ow/obj16/main.obj build/ow/obj16/lowlevel.obj
	$(Q)echo "  WLINK   $@"
	$(Q)$(WLINK) system dos option quiet option stack=4k name $@ file build/ow/obj16/main.obj,build/ow/obj16/lowlevel.obj

# ---- GLOSK.BIN: the kernel (host gcc -m32, linked at C0100000h) -------------
KCFLAGS := -m32 -march=i486 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
           -fno-asynchronous-unwind-tables -fno-delete-null-pointer-checks -mgeneral-regs-only \
           -O2 -g -Wall -Wextra -Werror -nostdinc -Iinclude -Ikernel/include
KSRCS := kernel/entry.S kernel/arch/stubs.S kernel/arch/cpu.c kernel/core/main.c kernel/core/timer.c \
         kernel/drv/serial.c kernel/lib/kprintf.c kernel/mm/pmm.c kernel/mm/heap.c kernel/mm/vmm.c \
         kernel/dbg/gdbstub.c
KOBJS := $(patsubst kernel/%,build/kernel/%.o,$(KSRCS))
build/kernel/%.o: kernel/% $(wildcard kernel/include/*.h include/glos/*.h)
	@mkdir -p $(dir $@)
	$(Q)echo "  KCC     $<"
	$(Q)$(HOST_CC) $(KCFLAGS) -c -o $@ $<
build/kernel/glosk.elf: $(KOBJS) kernel/kernel.ld
	$(Q)echo "  KLD     $@"
	$(Q)$(HOST_CC) $(KCFLAGS) -nostdlib -no-pie -Wl,-T,kernel/kernel.ld -Wl,--build-id=none \
	  -Wl,--no-warn-rwx-segments -o $@ $(KOBJS)
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

# The M0 survey's own probe (tests/dos/iftest.c): DJGPP, run under CWSDPMI and HDPMI.
DJENV := env LD_LIBRARY_PATH=$(DJGPP_PREFIX)/hostlib
DJCC  := $(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-gcc
build/dj/IFTEST.EXE: tests/dos/iftest.c
	@mkdir -p $(dir $@)
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) -O1 -Wall -Werror -o $@ $<
survey-tools: build/dj/IFTEST.EXE

clean:
	rm -rf build out
