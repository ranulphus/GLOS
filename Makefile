# GLOS (Graphics Library Operating System, for DOS). See PRD.md.
#
#   make                build/ow/GLOS.EXE (16-bit loader; a stub until G1)
#   make loopa [CARD=g450]   run GLOS.EXE in 86Box through MGA-Glide's Loop A: out/loopa-CARD/
#   make check-deps     MGA-Glide at or after deps.mk's pin
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

.PHONY: all loopa check-deps clean help
all: build/ow/GLOS.EXE

help:
	@sed -n '2,6p' Makefile | sed 's/^# //'

check-deps:
	@git -C "$(MGA_GLIDE)" merge-base --is-ancestor "$(MGA_GLIDE_PIN)" HEAD 2>/dev/null \
	  || { echo "$(MGA_GLIDE) is not at or after $(MGA_GLIDE_PIN) (deps.mk)"; exit 1; }

build/ow/obj16/glos.obj: loader/glos.c build/build_id
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC16   $<"
	$(Q)$(WCC16) -bt=dos -ms -0 -os -zq -we -dGLOS_BUILD="\"$(BUILD_ID)\"" -fo=$@ $<

build/ow/GLOS.EXE: build/ow/obj16/glos.obj
	$(Q)echo "  WLINK   $@"
	$(Q)$(WLINK) system dos option quiet name $@ file $<

# Touched only when the build id (git describe) changes, so the loader is
# rebuilt with the new id and otherwise left alone.
build/build_id: FORCE
	@mkdir -p build
	@echo '$(BUILD_ID)' | cmp -s - $@ || echo '$(BUILD_ID)' > $@
FORCE:
.PHONY: FORCE

loopa: all check-deps
	$(Q)$(DEV) python3 $(MGA_GLIDE)/tools/loopa/run.py --name glos --card $(CARD) \
	  --exe build/ow/GLOS.EXE --out $(CURDIR)/out/loopa-$(CARD) --timeout $(LOOPA_TIMEOUT); \
	  cat out/loopa-$(CARD)/status

clean:
	rm -rf build out
