# Build configuration. Override any of these in config.local.mk (not committed)
# or on the make command line.

# RetroGPU's checkout beside this one: the HAL, the Loop A harness, 86Box's patches, DOS-GL in gl/.
RETROGPU      ?= $(HOME)/RetroGPU
WATCOM        ?= $(HOME)/.local/opt/watcom-20260901
DJGPP_PREFIX  ?= $(HOME)/.local/opt/djgpp-gcc1220
HOST_CC       ?= gcc

# Loop A (86Box), run through RetroGPU's harness.
CARD          ?= g450
LOOPA_TIMEOUT ?= 120

# Private test fixtures, never committed: Turbo Pascal 7 / Turbo C++ installs
# (TPX.EXE on RTM.EXE + DPMI16BI.OVL is a 16-bit DPMI acceptance test, PRD D30).
BORLAND_DIR   ?= $(HOME)/BORLAND

# DOS-GL, for M8's windowed GL suites (its conformance EXEs, SDL): RetroGPU's gl/, built.
DOSGL_DIR     ?= $(RETROGPU)/gl
# Fifth Wheel, for M8's games suite: a tree built against DOSGL_DIR (its source unchanged).
FIFTHWHEEL_DIR ?= $(HOME)/FifthWheel
