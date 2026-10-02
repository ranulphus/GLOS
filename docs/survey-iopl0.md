# IOPL-0 survey (M0): findings

| | |
|---|---|
| Date | 2026-10-01/02 |
| Tool | `tools/survey/survey.py` (raw table in [survey-iopl0-results.md](survey-iopl0-results.md)) |
| Machine | Loop A `bf6` (Pentium II 350), G450, 86Box with local patches 0001–0110 (MGA-Glide branch `glos-harness`) |
| Hosts | Base (CWSDPMI r7 for DJGPP, DOS/4GW's own host), HDPMI32 2.23 (IOPL 3), HDPMI32i 2.23 (IOPL 0) |

## Why

GLOS runs DPMI clients at IOPL 0 (PRD D20) so that no program can stop the agent with `cli; jmp $`. The
baseline hosts give programs IOPL 3: their CLI, STI and POPF act on the real interrupt flag.

At IOPL 0, CLI and STI trap and are virtualised. **POPF neither traps nor changes IF**, so a program that
restores interrupts with `pushf; cli; ...; popf` leaves them disabled. The same holds with PVI (§9.3 of
supervisor.md).

Before writing a DPMI host, this survey ran our programs under an existing IOPL-0 host. The aim was to see
whether the hazard bites in practice, and which programs would need a direct-mode profile (D20).

## The hazard is real

`tests/dos/iftest.c` (IFTEST) times a busy loop by the BIOS tick after CLI, after PUSHF/CLI/POPF, and
after STI. It also reads DPMI 0902h.

| Host | After CLI | After PUSHF/CLI/POPF | After STI |
|---|---|---|---|
| CWSDPMI (IOPL 3) | 0 ticks, VIF 0 | 9 ticks, VIF 1 | 9 ticks, VIF 1 |
| HDPMI32 (IOPL 3) | 0, 0 | 8, 1 | 8, 1 |
| HDPMI32i (IOPL 0) | 0, 0 | **0, 0** | 8, 1 |

HDPMI32i virtualises the interrupt flag faithfully, and the POPF pattern leaves interrupts off until the
next STI. GLOS's host will behave the same (the CPU gives it no other choice).

## Our programs don't hit it

Every job is compared with its base run on four things: status, HX-TEST results, end markers (including
frame counts and GL errors), and the frames it dumps. Fifth Wheel's run is also compared on its
deterministic state hashes.

**Same under HDPMI32i as on its usual host:**

| Job | Program kind |
|---|---|
| mga-stackpg, mga-mouse, mga-sbbeep | DJGPP: DPMI calls on fresh stack pages, INT 33h, SB DAC timed with `uclock` |
| mga-conform t04, t13, t15, t26, t27 | DOS/4GW with the MGA-Glide OVL |
| dosgl-conform t01, t05, t09, t13, t15, t20, t25; dosgl-texcube | DJGPP, DOS-GL |
| classicube | DJGPP, DOS-GL; INT 21h 2Ch every frame |
| fifthwheel | SDL3 with cooperative threads, SB16 audio, DOS-GL; identical state hashes |
| prboom | SDL3, SB16; 1710 game tics timed identically |
| halflife | DJGPP, 128 MB, DOSLFN; timedemo completes |
| screamer-rally | Retail, DOS/4GW 1.97, HMI SOS, MGA-Glide; 400 frames, attract mode |

**mga-hello** differs only in its text-screen capture, which shows HDPMI's banner. Its tests and end
markers are the same.

No job behaved differently at IOPL 0 than at IOPL 3. The code paths that clear IF (SDL3's mutexes, the
DJGPP runtime, HMI SOS, DOS-GL's FIFO waits) all re-enable interrupts with STI or a DPMI call, not with
POPF alone.

**Direct-mode candidates found: none** (PRD Q17). The M4e gate's forced-direct runs remain the check for
programs outside this set.

## One host difference: GLQuake and UNIX_SBRK

GLQuake fails the same way under **both** HDPMI variants:
- `DGL-GLERR 0x0505` (GL_OUT_OF_MEMORY) in `glTexImage2D`'s `calloc` for its first texture;
- then exit after 0 frames.

It still fails with HDPMI's `-n` (report less free memory) and with Quake's own `-mem 24`, so it isn't
Quake taking all reported memory.

GLQuake sets `_CRT0_FLAG_UNIX_SBRK`, where DJGPP grows one memory block with DPMI 0503h. CWSDPMI resizes in
place or moves the block (supervisor.md §12.3); HDPMI evidently doesn't let it grow. This is a host
difference, not an IOPL one, and it confirms that GLOS's 0503h must behave like CWSDPMI's.

M4a adds a DPMICONF test that grows a UNIX_SBRK heap past 32 MB, and GLQuake is in the M4c gate.

## Not covered here

These run at M4, where their milestones need them:
- the SDL3 example suite (`tools/sdl/loopa.sh`; its examples weren't built in the shared DOS-GL tree);
- Quake 2;
- GTA (needs its key script);
- DOSBench's nested DJGPP → DOS/4GW clients;
- DJGPP's `djtst205`;
- ecm's dpmitest;
- HDPMI's regression suite.

## What changes in the design

1. **D20 stands.** IOPL 0 costs our programs nothing observable. Direct mode stays as an escape hatch,
   with no default profiles.
2. **The `vif-stuck` watchdog (supervisor.md §9.3) is worth having.** It is the only way GLOS will see a
   program that relies on POPF. It keeps logging only; the forced-VIF option stays off by default.
3. **0503h and 0500h must match CWSDPMI** (supervisor.md §12.3), backed by GLQuake. HDPMI is not a safe
   behavioural reference for memory.
4. **HDPMI32i is a good IOPL-0 reference for everything else.** The gate keeps it as a baseline next to
   CWSDPMI.
