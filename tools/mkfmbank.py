#!/usr/bin/env python3
"""mkfmbank.py - GLOS's General MIDI instruments for the FM chip: kernel/drv/fmbank.c, the bank GLOS's FM
driver (kernel/drv/fm.c) plays, and kernel/drv/fmtab.c, its computed tables. The one copy: sdk/fm's
libfmsynth.a and dgk's dgk/midi.h library (its libs.mk) compile these files from here.

  tools/mkfmbank.py kernel/drv/fmbank.c kernel/drv/fmtab.c        (make fmbank)

Fifth Wheel's instruments (Fifth Wheel's game, 2026-11-26), extended to every family of General MIDI's 128
programs and its 47 drum keys (35-81). The owner chose them over Freedoom's GENMIDI the same day, for dgk's games
and then for GLOS's kernel too ("Bank and curve"); written in dgk (its branch music, 6596b84, tools/fmbank.py)
and moved here, so that bank tweaks come to this file. First-party work (THIRD_PARTY.md).

The format is DMX's GENMIDI, as GLOS's driver reads it: 175 patches of 36 bytes (128 programs, then the drums
for keys 35 to 81): flags (1: a fixed note, 4: two voices), a spare byte, the second voice's tuning (128: none;
each 2 above is a 64th of a semitone up), the fixed note, then two voices of 16 bytes: the modulator's
registers 20h, 60h, 80h, E0h, its 40h key scaling (bits 6-7), its 40h level, the C0h feedback and connection,
the carrier's six, a spare byte, and the note offset (a signed 16-bit number). The driver adds 12 to a note
(DMX's octave): a voice's offset of -12 sounds a key at its own pitch, and a drum's fixed note N sounds as key
N + 12. The driver adds velocity, CC7 and CC11 to the carrier's level (and the modulator's, when the voice
adds the two), each by fm_atten.

TABLES.c (kernel/drv/fmtab.c) holds the driver's two computed tables: fm_fnum
as GLOS has always computed it (A = 440 Hz at the chip's 49716 Hz), and fm_atten, a MIDI level as attenuation in the
chip's 0.75 dB steps at half General MIDI's slope: 20 log10(127 / v) dB, not 40. On General MIDI's curve the
FM music sat 15 dB (Fifth Wheel) to 40 dB (Echoes, playing softly) under the games' sounds in 86Box; the owner
chose the gentler curve (2026-11-26).

Times, roughly, for an operator's rates (OPL2/OPL3, key scaling off): attack to full, AR 1-15: 2.8 s, 1.4 s,
0.7 s, 350 ms, 180 ms, 88, 44, 22, 11, 5.5, 2.8, 1.4, 0.7, 0.35, 0; decay or release over the full 96 dB,
DR/RR 1-15: 39 s, 20 s, 9.8 s, 4.9 s, 2.5 s, 1.2 s, 614 ms, 307, 153, 77, 38, 19, 9.6, 4.8, 2.4.
"""
import math
import sys


def op(mult=1, tl=0, ar=15, dr=4, sl=4, rr=6, eg=1, vib=0, am=0, ksr=0, ksl=0, wave=0):
    """An operator's registers: 20h (AM, vibrato, sustained, key scaling, multiple), 40h's two parts, 60h, 80h,
    E0h. eg=1: held at its sustain level while the key is down; eg=0: it decays on through it."""
    m = {0.5: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 7: 7, 8: 8, 9: 9, 10: 10, 12: 12, 15: 15}[mult]
    return dict(r20=am << 7 | vib << 6 | eg << 5 | ksr << 4 | m, ksl=ksl << 6, tl=tl, r60=ar << 4 | dr,
                r80=sl << 4 | rr, re0=wave)


def raw(r20, r40, r60, r80, re0):
    """An operator from its registers as written (Fifth Wheel's instruments, kept exact)."""
    return dict(r20=r20, ksl=r40 & 0xC0, tl=r40 & 0x3F, r60=r60, r80=r80, re0=re0)


def voice(mod, car, fb=0, add=0, offset=-12):
    return dict(mod=mod, car=car, c0=fb << 1 | add, offset=offset)


def patch(name, v1, v2=None, detune=0, fixed=None):
    """A program or drum: one voice, or two (v2; detune in 64ths of a semitone up), at the key's pitch or at a
    fixed note (drums)."""
    return dict(name=name, v1=v1, v2=v2, detune=detune, fixed=fixed)


def pbytes(p):
    flags = (1 if p["fixed"] is not None else 0) | (4 if p["v2"] else 0)
    out = [flags, 0, 128 + 2 * p["detune"] if p["v2"] else 128, p["fixed"] or 0]
    for v in (p["v1"], p["v2"] or p["v1"]):
        m, c = v["mod"], v["car"]
        off = v["offset"] & 0xFFFF
        out += [m["r20"], m["r60"], m["r80"], m["re0"], m["ksl"], m["tl"], v["c0"],
                c["r20"], c["r60"], c["r80"], c["re0"], c["ksl"], c["tl"], 0, off & 255, off >> 8]
    assert len(out) == 36, p["name"]
    return out


# ---- Fifth Wheel's seventeen, as they were (game/src/music.c, 2026-11-26) -------------------------------------
FW = {
    "piano":    voice(raw(0x01, 0x5A, 0xF3, 0x55, 0), raw(0x01, 0x00, 0xF2, 0x75, 0), 3),
    "epiano":   voice(raw(0x01, 0x24, 0xF5, 0x56, 0), raw(0x01, 0x00, 0xF2, 0x66, 0), 0),
    "vibes":    voice(raw(0x84, 0x35, 0xF4, 0xF5, 0), raw(0x81, 0x00, 0xF3, 0x55, 0), 0),
    "marimba":  voice(raw(0x04, 0x2A, 0xF7, 0xF6, 0), raw(0x01, 0x00, 0xF6, 0xF6, 0), 0),
    "glock":    voice(raw(0x07, 0x30, 0xF6, 0xF5, 0), raw(0x04, 0x00, 0xF5, 0xF5, 0), 0),
    "organ":    voice(raw(0x22, 0x10, 0xF0, 0x07, 0), raw(0x21, 0x00, 0xF0, 0x07, 0), 0, 1),
    "reed":     voice(raw(0x62, 0x18, 0x92, 0x17, 1), raw(0x61, 0x00, 0x96, 0x17, 0), 4),
    "clarinet": voice(raw(0x22, 0x16, 0x76, 0x17, 0), raw(0x21, 0x00, 0x75, 0x17, 0), 0),
    "flute":    voice(raw(0x61, 0x24, 0x75, 0x17, 0), raw(0x61, 0x00, 0x65, 0x17, 0), 6),
    "trumpet":  voice(raw(0x21, 0x12, 0x84, 0x24, 0), raw(0x21, 0x00, 0x82, 0x15, 0), 6),
    "trombone": voice(raw(0x21, 0x18, 0x73, 0x23, 0), raw(0x21, 0x00, 0x73, 0x15, 0), 5),
    "sax":      voice(raw(0x31, 0x14, 0x86, 0x25, 1), raw(0x21, 0x00, 0x76, 0x17, 0), 5),
    "strings":  voice(raw(0x61, 0x1C, 0x54, 0x14, 0), raw(0x61, 0x00, 0x53, 0x15, 0), 3),
    "guitar":   voice(raw(0x01, 0x1E, 0xF4, 0x95, 0), raw(0x01, 0x00, 0xF3, 0x96, 0), 4),
    "banjo":    voice(raw(0x02, 0x16, 0xF6, 0xF6, 0), raw(0x01, 0x00, 0xF5, 0xF6, 0), 5),
    "bass":     voice(raw(0x01, 0x1C, 0xF4, 0x36, 0), raw(0x01, 0x00, 0xF3, 0x78, 0), 4),
    "tuba":     voice(raw(0x21, 0x14, 0xA3, 0x25, 0), raw(0x21, 0x00, 0xA3, 0x16, 0), 5),
}


def fw(name, offset=-12, **car):
    """One of Fifth Wheel's, perhaps with its carrier's level changed (tl=...) or an octave moved."""
    v = dict(FW[name])
    v["offset"] = offset
    if car:
        c = dict(v["car"])
        c.update(car)
        v["car"] = c
    return v


# ---- New instruments ------------------------------------------------------------------------------------------
# Echoes of the Void's beds (sustained, soft attacks, long releases, gentle vibrato, warm), leads, ostinatos,
# then the rest of General MIDI's families.

warm_pad = voice(op(mult=1, tl=34, ar=4, dr=2, sl=2, rr=4, vib=1), op(mult=1, tl=3, ar=4, dr=2, sl=2, rr=4, vib=1), 2)
warm_pad2 = voice(op(mult=2, tl=40, ar=3, dr=2, sl=3, rr=4, vib=1), op(mult=1, tl=5, ar=4, dr=2, sl=2, rr=4, vib=1), 1)
choir = voice(op(mult=2, tl=30, ar=4, dr=3, sl=3, rr=5, vib=1, ksl=1), op(mult=1, tl=5, ar=4, dr=2, sl=2, rr=5,
                                                                          vib=1), 3)
halo = voice(op(mult=4, tl=38, ar=3, dr=2, sl=4, rr=3, vib=1, am=1), op(mult=1, ar=3, dr=2, sl=2, rr=3, vib=1), 0)
new_age = voice(op(mult=3, tl=28, ar=12, dr=5, sl=6, rr=4), op(mult=1, tl=4, ar=5, dr=2, sl=2, rr=4, vib=1), 2)
new_age_bell = voice(op(mult=7, tl=30, ar=15, dr=5, sl=15, rr=5, eg=0), op(mult=2, tl=6, ar=15, dr=4, sl=6, rr=4,
                                                                              eg=0), 0)
sweep = voice(op(mult=2, tl=22, ar=2, dr=1, sl=0, rr=4, vib=1), op(mult=1, tl=4, ar=4, dr=2, sl=2, rr=4, vib=1), 4)
bowed_glass = voice(op(mult=5, tl=36, ar=3, dr=2, sl=3, rr=4, vib=1), op(mult=1, ar=3, dr=2, sl=2, rr=4, vib=1), 1)
metallic = voice(op(mult=5, tl=36, ar=4, dr=3, sl=4, rr=4, vib=1), op(mult=1, tl=3, ar=4, dr=2, sl=3, rr=4), 2)
poly = voice(op(mult=1, tl=26, ar=9, dr=4, sl=4, rr=5), op(mult=1, ar=9, dr=3, sl=3, rr=5, vib=1), 5)
slow_strings = voice(op(mult=1, tl=28, ar=4, dr=3, sl=2, rr=5, vib=1), op(mult=1, ar=4, dr=2, sl=1, rr=5, vib=1), 4)
syn_strings = voice(op(mult=1, tl=24, ar=6, dr=3, sl=2, rr=5, vib=1), op(mult=1, ar=6, dr=2, sl=1, rr=5, vib=1), 6)
strings = voice(op(mult=1, tl=27, ar=6, dr=3, sl=2, rr=6, vib=1), op(mult=1, ar=6, dr=3, sl=1, rr=6, vib=1), 4)
violin = voice(op(mult=1, tl=24, ar=8, dr=3, sl=2, rr=6, vib=1), op(mult=1, ar=8, dr=2, sl=1, rr=6, vib=1), 5)
cello = voice(op(mult=1, tl=24, ar=7, dr=3, sl=2, rr=6, vib=1), op(mult=1, ar=7, dr=2, sl=1, rr=6, vib=1), 4)
trem_strings = voice(op(mult=1, tl=27, ar=7, dr=3, sl=2, rr=6, vib=1, am=1), op(mult=1, ar=7, dr=2, sl=1, rr=6,
                                                                                am=1), 4)
pizz = voice(op(mult=1, tl=24, ar=15, dr=5, sl=7, rr=6, eg=0), op(mult=1, ar=15, dr=5, sl=7, rr=6, eg=0), 3)
harp = voice(op(mult=2, tl=34, ar=15, dr=5, sl=9, rr=5, eg=0), op(mult=1, ar=15, dr=4, sl=8, rr=5, eg=0), 2)
timpani = voice(op(mult=1, tl=22, ar=15, dr=4, sl=8, rr=4, eg=0), op(mult=1, ar=15, dr=4, sl=6, rr=4, eg=0), 4,
                offset=-24)
orch_hit = voice(op(mult=1, tl=14, ar=15, dr=5, sl=6, rr=6, eg=0), op(mult=1, ar=15, dr=5, sl=5, rr=6, eg=0), 6)

celesta = voice(op(mult=4, tl=36, ar=15, dr=4, sl=10, rr=4, eg=0), op(mult=4, ar=15, dr=3, sl=8, rr=4, eg=0), 0,
                offset=-24)
music_box = voice(op(mult=8, tl=38, ar=15, dr=5, sl=12, rr=4, eg=0), op(mult=4, ar=15, dr=3, sl=9, rr=4, eg=0), 0,
                  offset=-24)
tub_bells = voice(op(mult=7, tl=28, ar=15, dr=3, sl=7, rr=3, eg=0), op(mult=2, ar=15, dr=3, sl=6, rr=3, eg=0), 2,
                  offset=-24)
dx_epiano = voice(op(mult=14 if False else 12, tl=36, ar=15, dr=6, sl=15, rr=5, eg=0),
                  op(mult=1, ar=15, dr=3, sl=6, rr=5, eg=0), 0)
crystal = voice(op(mult=10, tl=32, ar=15, dr=4, sl=10, rr=4, eg=0), op(mult=2, ar=15, dr=3, sl=8, rr=4, eg=0), 0,
                offset=-24)
sitar = voice(op(mult=1, tl=20, ar=15, dr=4, sl=6, rr=5, eg=0), op(mult=1, ar=15, dr=3, sl=6, rr=5, eg=0, vib=1),
              6)
kalimba = voice(op(mult=6, tl=34, ar=15, dr=7, sl=15, rr=6, eg=0), op(mult=1, ar=15, dr=5, sl=11, rr=6, eg=0), 0)
steel_drum = voice(op(mult=3, tl=26, ar=15, dr=5, sl=12, rr=5, eg=0), op(mult=1, ar=15, dr=5, sl=9, rr=5, eg=0), 2)
koto = voice(op(mult=3, tl=30, ar=15, dr=6, sl=12, rr=5, eg=0), op(mult=1, ar=15, dr=4, sl=9, rr=5, eg=0), 3)
harpsichord = voice(op(mult=4, tl=26, ar=15, dr=5, sl=10, rr=6, eg=0), op(mult=1, ar=15, dr=4, sl=8, rr=6, eg=0), 5)
clav = voice(op(mult=3, tl=22, ar=15, dr=6, sl=12, rr=7, eg=0), op(mult=1, ar=15, dr=5, sl=10, rr=7, eg=0), 5)
xylophone = voice(op(mult=4, tl=34, ar=15, dr=7, sl=15, rr=7, eg=0), op(mult=1, ar=15, dr=6, sl=13, rr=7, eg=0), 0)
dulcimer = voice(op(mult=2, tl=24, ar=15, dr=5, sl=9, rr=5, eg=0), op(mult=1, ar=15, dr=4, sl=8, rr=5, eg=0), 4)

syn_bass = voice(op(mult=1, tl=22, ar=15, dr=5, sl=5, rr=6), op(mult=1, ar=15, dr=3, sl=3, rr=6), 6, offset=-12)
round_bass = voice(op(mult=1, tl=30, ar=14, dr=5, sl=6, rr=6), op(mult=1, ar=14, dr=2, sl=2, rr=6), 2, offset=-12)
fretless = voice(op(mult=1, tl=30, ar=11, dr=4, sl=5, rr=6), op(mult=1, ar=11, dr=3, sl=4, rr=6), 3)

horn = voice(op(mult=1, tl=24, ar=7, dr=3, sl=2, rr=6), op(mult=1, ar=7, dr=2, sl=1, rr=6, vib=1), 4)
oboe = voice(op(mult=3, tl=24, ar=8, dr=3, sl=2, rr=6), op(mult=1, tl=3, ar=8, dr=2, sl=1, rr=6, vib=1), 3)
bassoon = voice(op(mult=1, tl=24, ar=8, dr=3, sl=2, rr=6), op(mult=1, ar=8, dr=2, sl=1, rr=6), 5)
recorder = voice(op(mult=1, tl=40, ar=8, dr=3, sl=2, rr=6), op(mult=1, ar=8, dr=2, sl=1, rr=6, vib=1), 2)
pan_flute = voice(op(mult=2, tl=38, ar=6, dr=3, sl=3, rr=6, am=1), op(mult=1, tl=6, ar=6, dr=2, sl=1, rr=6,
                                                                       vib=1), 6)
whistle = voice(op(mult=1, tl=50, ar=8, dr=3, sl=2, rr=6), op(mult=2, tl=3, ar=8, dr=2, sl=1, rr=6, vib=1), 0)
square_lead = voice(op(mult=1, tl=16, ar=14, dr=3, sl=1, rr=6), op(mult=1, tl=4, ar=14, dr=2, sl=1, rr=6), 6,
                    offset=-12)
saw_lead = voice(op(mult=1, tl=12, ar=14, dr=3, sl=1, rr=6), op(mult=1, ar=14, dr=2, sl=1, rr=6), 7)
dist_guitar = voice(op(mult=1, tl=14, ar=15, dr=3, sl=3, rr=6), op(mult=1, ar=15, dr=2, sl=2, rr=6), 7)
over_guitar = voice(op(mult=1, tl=18, ar=15, dr=3, sl=3, rr=6), op(mult=1, ar=15, dr=2, sl=2, rr=6), 6)
mute_guitar = voice(op(mult=1, tl=28, ar=15, dr=7, sl=12, rr=8, eg=0), op(mult=1, ar=15, dr=7, sl=12, rr=8, eg=0),
                    4)
harmonics = voice(op(mult=4, tl=40, ar=15, dr=4, sl=10, rr=5, eg=0), op(mult=2, ar=15, dr=3, sl=8, rr=5, eg=0), 0)
honky = (fw("piano"), fw("piano"), 10)
noise_fx = voice(op(mult=15, tl=0, ar=10, dr=4, sl=4, rr=4), op(mult=1, ar=10, dr=4, sl=4, rr=4), 7)
blip_fx = voice(op(mult=5, tl=20, ar=15, dr=6, sl=10, rr=5, eg=0), op(mult=1, ar=15, dr=6, sl=10, rr=5, eg=0), 5)

P = [None] * 128


def prog(lo, hi, p):
    for i in range(lo, hi + 1):
        P[i] = p


# Pianos and chromatic percussion
prog(0, 1, patch("Piano", fw("piano")))
P[2] = patch("Electric Grand", fw("piano", tl=2), fw("epiano", tl=6), detune=3)
P[3] = patch("Honky-tonk Piano", fw("piano", tl=5), fw("piano", tl=5), detune=10)
P[4] = patch("Electric Piano 1", fw("epiano"))
P[5] = patch("Electric Piano 2", dx_epiano)
P[6] = patch("Harpsichord", harpsichord)
P[7] = patch("Clavinet", clav)
P[8] = patch("Celesta", celesta)
P[9] = patch("Glockenspiel", fw("glock"))
P[10] = patch("Music Box", music_box)
P[11] = patch("Vibraphone", fw("vibes"))
P[12] = patch("Marimba", fw("marimba"))
P[13] = patch("Xylophone", xylophone)
P[14] = patch("Tubular Bells", tub_bells)
P[15] = patch("Dulcimer", dulcimer)
# Organs
organ = dict(FW["organ"], mod=dict(FW["organ"]["mod"], tl=21), car=dict(FW["organ"]["car"], tl=5), offset=-12)
prog(16, 18, patch("Organ", organ))
P[19] = patch("Church Organ", dict(organ, mod=dict(organ["mod"], tl=26), car=dict(organ["car"], tl=10)),
              dict(organ, mod=dict(organ["mod"], tl=28), car=dict(organ["car"], tl=12), offset=0))
P[20] = patch("Reed Organ", fw("reed"))
P[21] = patch("Accordion", fw("reed", tl=4), fw("reed", tl=4), detune=6)
P[22] = patch("Harmonica", fw("reed"))
P[23] = patch("Tango Accordion", fw("reed", tl=5), fw("reed", tl=5), detune=8)
# Guitars
prog(24, 27, patch("Guitar", fw("guitar")))
P[28] = patch("Muted Guitar", mute_guitar)
P[29] = patch("Overdriven Guitar", over_guitar)
P[30] = patch("Distortion Guitar", dist_guitar)
P[31] = patch("Guitar Harmonics", harmonics)
# Basses
prog(32, 34, patch("Bass", fw("bass")))
P[35] = patch("Fretless Bass", fretless)
prog(36, 37, patch("Slap Bass", fw("bass", tl=0)))
P[38] = patch("Synth Bass 1", syn_bass)
P[39] = patch("Synth Bass 2", round_bass)
# Strings
prog(40, 41, patch("Violin", violin))
P[42] = patch("Cello", cello)
P[43] = patch("Contrabass", cello)
P[44] = patch("Tremolo Strings", trem_strings)
P[45] = patch("Pizzicato Strings", pizz)
P[46] = patch("Harp", harp)
P[47] = patch("Timpani", timpani)
P[48] = patch("Strings", strings)
P[49] = patch("Slow Strings", slow_strings)
prog(50, 51, patch("Synth Strings", syn_strings))
P[52] = patch("Choir Aahs", choir)
P[53] = patch("Voice Oohs", choir)
P[54] = patch("Synth Voice", choir)
P[55] = patch("Orchestra Hit", orch_hit)
# Brass
P[56] = patch("Trumpet", fw("trumpet"))
P[57] = patch("Trombone", fw("trombone"))
P[58] = patch("Tuba", fw("tuba"))
P[59] = patch("Muted Trumpet", fw("trumpet", tl=4))
P[60] = patch("French Horn", horn)
P[61] = patch("Brass Section", fw("trumpet", tl=4), fw("trombone", tl=4), detune=4)
prog(62, 63, patch("Synth Brass", fw("trumpet")))
# Reeds and pipes
prog(64, 67, patch("Saxophone", fw("sax")))
P[68] = patch("Oboe", oboe)
P[69] = patch("English Horn", oboe)
P[70] = patch("Bassoon", bassoon)
# (Fifth Wheel's clarinet, softened 2026-11-26: the owner found Open Road Rag's melody shrill on bench A. The
# modulator quieter up the keyboard (KSL, 1.5 dB an octave): its high notes have fewer high overtones.)
clarinet = dict(FW["clarinet"], offset=-12, mod=dict(FW["clarinet"]["mod"], ksl=2 << 6))
P[71] = patch("Clarinet", clarinet)
P[72] = patch("Piccolo", fw("flute"))
P[73] = patch("Flute", fw("flute"))
P[74] = patch("Recorder", recorder)
P[75] = patch("Pan Flute", pan_flute)
P[76] = patch("Blown Bottle", pan_flute)
P[77] = patch("Shakuhachi", pan_flute)
P[78] = patch("Whistle", whistle)
P[79] = patch("Ocarina", recorder)
# Synth leads
P[80] = patch("Square Lead", square_lead)
P[81] = patch("Saw Lead", saw_lead)
P[82] = patch("Calliope Lead", fw("flute"))
P[83] = patch("Chiff Lead", recorder)
P[84] = patch("Charang Lead", over_guitar)
P[85] = patch("Voice Lead", choir)
P[86] = patch("Fifths Lead", dict(square_lead, car=dict(square_lead["car"], tl=9)),
              dict(square_lead, car=dict(square_lead["car"], tl=9), offset=-5))
P[87] = patch("Bass and Lead", saw_lead)
# Synth pads: Echoes' beds
P[88] = patch("New Age Pad", new_age, dict(new_age_bell, car=dict(new_age_bell["car"], tl=10)))
P[89] = patch("Warm Pad", dict(warm_pad, car=dict(warm_pad["car"], tl=6)), dict(warm_pad2, car=dict(warm_pad2["car"],
                                                                                                tl=8)), detune=6)
P[90] = patch("Polysynth Pad", poly)
P[91] = patch("Choir Pad", dict(choir, car=dict(choir["car"], tl=9)), dict(warm_pad, car=dict(warm_pad["car"], tl=8)),
              detune=4)
P[92] = patch("Bowed Glass Pad", bowed_glass)
P[93] = patch("Metallic Pad", metallic)
P[94] = patch("Halo Pad", halo)
P[95] = patch("Sweep Pad", sweep)
# Synth effects
P[96] = patch("Rain", crystal)
P[97] = patch("Soundtrack", slow_strings)
P[98] = patch("Crystal", crystal)
P[99] = patch("Atmosphere", warm_pad)
P[100] = patch("Brightness", bowed_glass)
P[101] = patch("Goblins", metallic)
P[102] = patch("Echoes", halo)
P[103] = patch("Sci-fi", metallic)
# Ethnic
P[104] = patch("Sitar", sitar)
P[105] = patch("Banjo", fw("banjo"))
P[106] = patch("Shamisen", fw("banjo"))
P[107] = patch("Koto", koto)
P[108] = patch("Kalimba", kalimba)
P[109] = patch("Bagpipe", fw("reed"))
P[110] = patch("Fiddle", violin)
P[111] = patch("Shanai", oboe)
# Percussive
P[112] = patch("Tinkle Bell", music_box)
P[113] = patch("Agogo", steel_drum)
P[114] = patch("Steel Drums", steel_drum)
P[115] = patch("Woodblock", xylophone)
P[116] = patch("Taiko Drum", timpani)
P[117] = patch("Melodic Tom", timpani)
P[118] = patch("Synth Drum", blip_fx)
P[119] = patch("Reverse Cymbal", noise_fx)
# Sound effects
prog(120, 127, patch("Sound Effect", noise_fx))
P[120] = patch("Guitar Fret Noise", mute_guitar)
P[121] = patch("Breath Noise", pan_flute)
P[123] = patch("Bird Tweet", whistle)
P[124] = patch("Telephone Ring", blip_fx)



def softer(progs, steps):
    """The programs' voices quieter by steps of 0.75 dB (the carrier's level; the modulator's too where the voice
    adds the two): the beds below the leads, as General MIDI banks keep their pads."""
    for i in progs:
        p = dict(P[i])
        for k in ("v1", "v2"):
            if p[k]:
                v = dict(p[k])
                v["car"] = dict(v["car"], tl=min(63, v["car"]["tl"] + steps))
                if v["c0"] & 1:
                    v["mod"] = dict(v["mod"], tl=min(63, v["mod"]["tl"] + steps))
                p[k] = v
        P[i] = p


softer([48, 49, 50, 51, 52, 53, 54, 88, 89, 90, 91, 92, 93, 94, 95, 97, 99, 100, 102], 4)
softer([92, 94, 100, 102], 3)                    # (halo and bowed glass: the loudest pads)

# ---- Drums: keys 35-81, each at a fixed note (sounding as fixed + 12) -----------------------------------------


def drum(mod, car, fb, fixed, add=0):
    return dict(v=voice(mod, car, fb, add, offset=0), fixed=fixed)


kick = drum(op(mult=1, tl=24, ar=15, dr=8, sl=15, rr=8, eg=0), op(mult=1, ar=15, dr=6, sl=10, rr=7, eg=0), 1, 22)
snare = drum(op(mult=15, tl=0, ar=15, dr=7, sl=15, rr=7, eg=0), op(mult=2, ar=15, dr=7, sl=12, rr=7, eg=0), 7, 48)
side_stick = drum(op(mult=5, tl=20, ar=15, dr=10, sl=15, rr=10, eg=0), op(mult=3, ar=15, dr=9, sl=15, rr=9, eg=0),
                  3, 60)
clap = drum(op(mult=15, tl=4, ar=15, dr=8, sl=15, rr=8, eg=0), op(mult=4, ar=15, dr=8, sl=15, rr=8, eg=0), 7, 55)


def tom(note):
    return drum(op(mult=1, tl=28, ar=15, dr=6, sl=15, rr=6, eg=0), op(mult=1, ar=15, dr=5, sl=9, rr=5, eg=0), 2, note)


hat_closed = drum(op(mult=15, tl=0, ar=15, dr=10, sl=15, rr=10, eg=0), op(mult=10, ar=15, dr=10, sl=15, rr=10,
                                                                             eg=0), 7, 78)
hat_pedal = drum(op(mult=15, tl=0, ar=15, dr=10, sl=15, rr=10, eg=0), op(mult=9, ar=15, dr=9, sl=15, rr=9, eg=0),
                 7, 74)
hat_open = drum(op(mult=15, tl=0, ar=15, dr=6, sl=15, rr=6, eg=0), op(mult=10, ar=15, dr=6, sl=12, rr=6, eg=0), 7,
                78)
crash = drum(op(mult=15, tl=0, ar=15, dr=4, sl=15, rr=4, eg=0), op(mult=5, ar=15, dr=4, sl=10, rr=4, eg=0), 7, 66)
ride = drum(op(mult=12, tl=10, ar=15, dr=5, sl=15, rr=5, eg=0), op(mult=6, ar=15, dr=4, sl=10, rr=4, eg=0), 6, 70)
ride_bell = drum(op(mult=7, tl=26, ar=15, dr=4, sl=12, rr=4, eg=0), op(mult=3, ar=15, dr=4, sl=10, rr=4, eg=0), 2,
                 69)
china = drum(op(mult=15, tl=0, ar=15, dr=4, sl=15, rr=4, eg=0), op(mult=7, ar=15, dr=4, sl=10, rr=4, eg=0), 7, 62)
tamb = drum(op(mult=15, tl=4, ar=15, dr=8, sl=15, rr=8, eg=0), op(mult=12, ar=15, dr=8, sl=13, rr=8, eg=0), 7, 80)
splash = drum(op(mult=15, tl=0, ar=15, dr=5, sl=15, rr=5, eg=0), op(mult=8, ar=15, dr=5, sl=11, rr=5, eg=0), 7, 72)
cowbell = drum(op(mult=3, tl=24, ar=15, dr=7, sl=12, rr=7, eg=0), op(mult=2, ar=15, dr=7, sl=12, rr=7, eg=0), 3, 63)
vibraslap = drum(op(mult=9, tl=10, ar=15, dr=4, sl=12, rr=4, eg=0, am=1), op(mult=3, ar=15, dr=4, sl=10, rr=4,
                                                                               eg=0, am=1), 5, 56)
bongo = drum(op(mult=1, tl=30, ar=15, dr=8, sl=15, rr=8, eg=0), op(mult=1, ar=15, dr=7, sl=12, rr=7, eg=0), 2, 0)
conga = drum(op(mult=1, tl=30, ar=15, dr=7, sl=15, rr=7, eg=0), op(mult=1, ar=15, dr=6, sl=11, rr=6, eg=0), 2, 0)
timbale = drum(op(mult=2, tl=28, ar=15, dr=7, sl=15, rr=7, eg=0), op(mult=1, ar=15, dr=6, sl=11, rr=6, eg=0), 3, 0)
agogo = drum(op(mult=4, tl=26, ar=15, dr=6, sl=12, rr=6, eg=0), op(mult=2, ar=15, dr=6, sl=11, rr=6, eg=0), 2, 0)
shaker = drum(op(mult=15, tl=6, ar=13, dr=9, sl=15, rr=9, eg=0), op(mult=15, ar=13, dr=9, sl=15, rr=9, eg=0), 7, 84)
whistle_d = drum(op(mult=1, tl=50, ar=12, dr=4, sl=2, rr=6), op(mult=1, ar=12, dr=3, sl=1, rr=6, vib=1), 0, 0)
guiro = drum(op(mult=15, tl=8, ar=12, dr=7, sl=15, rr=7, eg=0, am=1), op(mult=6, ar=12, dr=7, sl=13, rr=7, eg=0,
                                                                         am=1), 7, 60)
claves = drum(op(mult=4, tl=30, ar=15, dr=10, sl=15, rr=10, eg=0), op(mult=2, ar=15, dr=9, sl=15, rr=9, eg=0), 1, 72)
woodblock = drum(op(mult=3, tl=28, ar=15, dr=10, sl=15, rr=10, eg=0), op(mult=1, ar=15, dr=9, sl=15, rr=9, eg=0), 1,
                 0)
cuica = drum(op(mult=1, tl=30, ar=12, dr=6, sl=12, rr=6, eg=0, vib=1), op(mult=1, ar=12, dr=6, sl=10, rr=6, eg=0,
                                                                            vib=1), 4, 0)
triangle = drum(op(mult=6, tl=36, ar=15, dr=4, sl=10, rr=4, eg=0), op(mult=4, ar=15, dr=3, sl=8, rr=3, eg=0), 0, 0)


def at(d, fixed):
    return dict(v=d["v"], fixed=fixed)


DRUMS = {
    35: ("Bass Drum 2", at(kick, 20)), 36: ("Bass Drum 1", kick), 37: ("Side Stick", side_stick),
    38: ("Snare", snare), 39: ("Hand Clap", clap), 40: ("Electric Snare", at(snare, 50)),
    41: ("Low Floor Tom", tom(31)), 42: ("Closed Hi-hat", hat_closed), 43: ("High Floor Tom", tom(34)),
    44: ("Pedal Hi-hat", hat_pedal), 45: ("Low Tom", tom(37)), 46: ("Open Hi-hat", hat_open),
    47: ("Low-mid Tom", tom(40)), 48: ("High-mid Tom", tom(43)), 49: ("Crash Cymbal 1", crash),
    50: ("High Tom", tom(46)), 51: ("Ride Cymbal 1", ride), 52: ("Chinese Cymbal", china),
    53: ("Ride Bell", ride_bell), 54: ("Tambourine", tamb), 55: ("Splash Cymbal", splash),
    56: ("Cowbell", cowbell), 57: ("Crash Cymbal 2", at(crash, 68)), 58: ("Vibraslap", vibraslap),
    59: ("Ride Cymbal 2", at(ride, 72)), 60: ("High Bongo", at(bongo, 52)), 61: ("Low Bongo", at(bongo, 47)),
    62: ("Mute High Conga", at(conga, 45)), 63: ("Open High Conga", at(conga, 43)), 64: ("Low Conga", at(conga, 38)),
    65: ("High Timbale", at(timbale, 50)), 66: ("Low Timbale", at(timbale, 45)), 67: ("High Agogo", at(agogo, 65)),
    68: ("Low Agogo", at(agogo, 60)), 69: ("Cabasa", shaker), 70: ("Maracas", at(shaker, 86)),
    71: ("Short Whistle", at(whistle_d, 72)), 72: ("Long Whistle", at(whistle_d, 69)), 73: ("Short Guiro", guiro),
    74: ("Long Guiro", at(guiro, 57)), 75: ("Claves", claves), 76: ("High Wood Block", at(woodblock, 64)),
    77: ("Low Wood Block", at(woodblock, 59)), 78: ("Mute Cuica", at(cuica, 62)), 79: ("Open Cuica", at(cuica, 57)),
    80: ("Mute Triangle", at(dict(v=voice(triangle["v"]["mod"], dict(triangle["v"]["car"], r60=0xF8, r80=0xF8),
                                          0, offset=0)), 76)),
    81: ("Open Triangle", at(triangle, 76)),
}


def main():
    out = ["/* Made by tools/mkfmbank.py (make fmbank): don't edit. GLOS's General MIDI instruments for the FM chip,",
           " * in GENMIDI's layout for the FM driver (kernel/drv/fm.c; fm.h, FM_PATCH_BYTES): Fifth Wheel's",
           " * instruments, extended (first-party). sdk/fm and dgk's dgk/midi.h compile this file from here. The",
           " * driver's computed tables are kernel/drv/fmtab.c. */",
           "#include \"fm.h\"", "", "const u8 fm_bank[FM_PATCHES][FM_PATCH_BYTES] = {"]
    for i in range(128):
        assert P[i], "program %d" % i
        out.append("    { %s },  /* %d %s */" % (",".join(str(b) for b in pbytes(P[i])), i, P[i]["name"]))
    for k in range(35, 82):
        name, d = DRUMS[k]
        out.append("    { %s },  /* key %d %s */" % (",".join(str(b) for b in pbytes(patch(name, d["v"], fixed=d["fixed"]))), k,
                                                     name))
    out.append("};")
    out.append("")
    open(sys.argv[1], "w").write("\n".join(out) + "\n")
    t = ["/* Made by tools/mkfmbank.py (make fmbank): don't edit. The FM driver's computed tables, apart from the",
         " * instruments (kernel/drv/fmbank.c): fm_fnum, an octave of F-numbers (A = 440 Hz at the chip's",
         " * 49716 Hz); fm_atten, a MIDI level as attenuation at half General MIDI's slope, 20 log10(127 / v) dB",
         " * in 0.75 dB steps (63: silent; the owner's curve, 2026-11-26). */",
         "#include \"fm.h\"", "", "const u16 fm_fnum[384] = {"]
    f = [round(440.0 * 2 ** ((i - 96) / 384.0) * 65536 / 49716) for i in range(384)]
    for i in range(0, 384, 16):
        t.append("    " + ",".join(str(v) for v in f[i:i + 16]) + ",")
    t += ["};", "", "const u8 fm_atten[128] = {"]
    a = [63] + [min(63, round(20 * math.log10(127.0 / v) / 0.75)) for v in range(1, 128)]
    for i in range(0, 128, 16):
        t.append("    " + ",".join(str(v) for v in a[i:i + 16]) + ",")
    t.append("};")
    open(sys.argv[2], "w").write("\n".join(t) + "\n")


if __name__ == "__main__":
    main()
