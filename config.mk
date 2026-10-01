# Build configuration. Override any of these in config.local.mk (not committed)
# or on the make command line.

MGA_GLIDE     ?= $(HOME)/MGA-Glide
WATCOM        ?= $(HOME)/.local/opt/watcom-20260901
DJGPP_PREFIX  ?= $(HOME)/.local/opt/djgpp-gcc1220
HOST_CC       ?= gcc

# Loop A (86Box), run through MGA-Glide's harness.
CARD          ?= g450
LOOPA_TIMEOUT ?= 120

# Private test fixtures, never committed: Turbo Pascal 7 / Turbo C++ installs
# (TPX.EXE on RTM.EXE + DPMI16BI.OVL is a 16-bit DPMI acceptance test, PRD D30).
BORLAND_DIR   ?= $(HOME)/BORLAND
