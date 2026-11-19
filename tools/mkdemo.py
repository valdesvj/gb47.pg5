#!/usr/bin/env python3
"""mkdemo.py - GB47's own little Game Boy cartridge (32 KB, no MBC): mkdemo.py out.gb

What it shows, to try GB47 without a game:
  - the background: bands of the four greys, scrolling left all the time (the whole picture changes every frame)
  - a ball (a sprite) the pad moves; held B makes it light
  - a window at the bottom with the text "GB47 DEMO" / "PAD MOVES"
  - a tune of our own: a melody on channel 1 (loud, short notes) and a bass on channel 2 (quieter, held), so the
    buzzer plays the melody and the bass in the melody's rests

Written as Game Boy machine code here (a few helpers, labels resolved in two passes): no assembler needed.
There is no Nintendo logo in the header: GB47 (Peanut-GB) does not check it; a real Game Boy would not start it.
Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later.
"""
import sys

ROM = bytearray(0x8000)
code, labels, fix = bytearray(), {}, []
ORG = 0x150


def b(*xs):
    code.extend(x & 0xFF for x in xs)


def w(x):
    b(x & 0xFF, x >> 8)


def label(n):
    labels[n] = ORG + len(code)


def jr(op, n):          # relative jump to a label
    b(op, 0)
    fix.append(("r", len(code) - 1, n))


def jp(n, op=0xC3):
    b(op, 0, 0)
    fix.append(("a", len(code) - 2, n))


def ldw(op, n):         # ld rr,label
    b(op, 0, 0)
    fix.append(("a", len(code) - 2, n))


def copy(dst, src_label, length):   # hl = dst, de = src, bc = length: copy
    b(0x21); w(dst)
    ldw(0x11, src_label)
    b(0x01); w(length)
    tag = "copy%d" % len(code)
    label(tag)
    b(0x1A, 0x22, 0x13, 0x0B, 0x78, 0xB1)   # ld a,(de); ld (hl+),a; inc de; dec bc; ld a,b; or c
    jr(0x20, tag)


# ------------------------------------------------------------------ tiles
def solid(c):
    return bytes([0xFF if c & 1 else 0, 0xFF if c & 2 else 0] * 8)


BALL = ["..3333..", ".322223.", "32211223", "32111123", "32111123", "32211223", ".322223.", "..3333.."]


def tile(rows):
    out = bytearray()
    for r in rows:
        lo = hi = 0
        for i, ch in enumerate(r):
            c = 0 if ch == "." else int(ch)
            lo |= (c & 1) << (7 - i)
            hi |= ((c >> 1) & 1) << (7 - i)
        out += bytes([lo, hi])
    return bytes(out)


FONT = {  # 5x7, our own
    "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01111"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
}
CHARS = " " + "".join(FONT)


def glyph(ch):          # black text on white: colour 3 on colour 0
    rows = ["........"]
    for r in FONT[ch]:
        rows.append("." + "".join("3" if x == "1" else "." for x in r) + "..")
    return tile(rows)


TILES = solid(0) + solid(0) + solid(1) + solid(2) + solid(3) + tile(BALL) + b"".join(glyph(c) for c in FONT)
T_BALL, T_TEXT = 5, 6


def text_tiles(s):
    return bytes(0 if c == " " else T_TEXT + CHARS.index(c) - 1 for c in s)


WINDOW = text_tiles(" GB47 DEMO".ljust(32)) + text_tiles(" PAD MOVES".ljust(32))


# ------------------------------------------------------------------ the tune (ours): note numbers, 0 = rest
def x(freq):            # the 11-bit value for a square channel's frequency
    return 2048 - round(131072 / freq)


def note(n):            # semitones from A4 = 440 Hz
    return 440 * 2 ** (n / 12)


C5, D5, E5, F5, G5, A5, B5, C6 = (note(n) for n in (3, 5, 7, 8, 10, 12, 14, 15))
MELODY = [C5, E5, G5, C6, G5, E5, 0, E5, D5, F5, A5, F5, D5, 0, B5, G5,
          E5, G5, C6, G5, F5, A5, C6, A5, G5, F5, E5, D5, C5, 0, C5, 0]
BASS = [note(n) for n in (-21, -21, -19, -19, -21, -16, -14, -21)]      # C3 C3 D3 D3 C3 F3 G3 C3


def notes(lst):
    out = bytearray()
    for f in lst:
        v = x(f) if f else 0
        out += bytes([v & 0xFF, v >> 8])
    return bytes(out)


# ------------------------------------------------------------------ the program
Y, X_, CNT, IDX = 0xC000, 0xC001, 0xC002, 0xC003

label("start")
b(0xF3)                                   # di
b(0x31); w(0xFFFE)                        # ld sp,$FFFE
label("wait")
b(0xF0, 0x44, 0xFE, 144)                  # ldh a,(LY); cp 144
jr(0x38, "wait")                          # jr c
b(0xAF, 0xE0, 0x40)                       # xor a; ldh (LCDC),a: LCD off
copy(0x8000, "tiles", len(TILES))
copy(0x9C00, "window", len(WINDOW))
b(0x21); w(0x9800)                        # the background: bands of 4 tiles of each grey
label("bg")
b(0x7D, 0xCB, 0x3F, 0xCB, 0x3F, 0xE6, 3, 0x3C, 0x22)   # ld a,l; srl a; srl a; and 3; inc a; ld (hl+),a
b(0x7C, 0xFE, 0x9C)                       # ld a,h; cp $9C
jr(0x20, "bg")
b(0x21); w(0xFE00)                        # OAM cleared
label("oam")
b(0xAF, 0x22, 0x7D, 0xFE, 0xA0)           # xor a; ld (hl+),a; ld a,l; cp $A0
jr(0x20, "oam")
b(0x3E, 80, 0xEA); w(Y)                   # the ball in the middle
b(0x3E, 84, 0xEA); w(X_)
b(0x3E, 1, 0xEA); w(CNT)
b(0x3E, 31, 0xEA); w(IDX)
b(0x3E, 0xE4, 0xE0, 0x47, 0xE0, 0x48)     # BGP, OBP0: 3 2 1 0
b(0x3E, 0x90, 0xE0, 0x49)                 # OBP1: light (colours 0 1 0 2)
b(0x3E, 128, 0xE0, 0x4A, 0x3E, 7, 0xE0, 0x4B)   # the window: WY 128, WX 7
b(0x3E, 0x80, 0xE0, 0x26, 0x3E, 0xFF, 0xE0, 0x25, 0x3E, 0x77, 0xE0, 0x24)   # sound on, both sides, loud
b(0x3E, 0xF3, 0xE0, 0x40)                 # LCD on: BG, sprites, window at $9C00, tiles at $8000
b(0x3E, 1, 0xE0, 0xFF, 0xFB)              # IE = VBLANK; ei

label("main")
b(0x76, 0x00)                             # halt; nop: wait for the frame
b(0xF0, 0x43, 0x3C, 0xE0, 0x43)           # SCX++: the background scrolls
# the keys -> b: bits 4-7 right left up down, bits 0-3 A B select start
b(0x3E, 0x20, 0xE0, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0x2F, 0xE6, 0x0F, 0xCB, 0x37, 0x47)
b(0x3E, 0x10, 0xE0, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0x2F, 0xE6, 0x0F, 0xB0, 0x47)
b(0x3E, 0x30, 0xE0, 0x00)
for bit, addr, op, nm in ((4, X_, 0x3C, "r"), (5, X_, 0x3D, "l"), (6, Y, 0x3D, "u"), (7, Y, 0x3C, "d")):
    b(0xCB, 0x40 + 8 * bit)               # bit n,b
    jr(0x28, "k" + nm)                    # jr z: not held
    b(0xFA); w(addr); b(op, op, 0xEA); w(addr)   # two pixels a frame
    label("k" + nm)
b(0x21); w(0xFE00)                        # the sprite: y, x, tile, attributes
b(0xFA); w(Y); b(0xC6, 16, 0x22)
b(0xFA); w(X_); b(0xC6, 8, 0x22)
b(0x3E, T_BALL, 0x22)
b(0x78, 0xE6, 0x02, 0xCB, 0x27, 0xCB, 0x27, 0xCB, 0x27, 0x77)   # B held: OBP1 (light): bit 4
# the tune: every 10 frames the next step; a melody note on ch1, every 4th step a bass note on ch2
b(0x21); w(CNT); b(0x35)                  # dec (hl)
jp("main", 0xC2)                          # jp nz,main
b(0x36, 10)                               # ld (hl),10
b(0xFA); w(IDX); b(0x3C, 0xE6, 31, 0xEA); w(IDX)
b(0x4F, 0x06, 0x00)                       # ld c,a; ld b,0
ldw(0x21, "melody"); b(0x09, 0x09)        # hl = melody + 2*step
b(0x2A, 0x5F, 0x7E, 0xB7)                 # ld a,(hl+); ld e,a; ld a,(hl); or a
jr(0x28, "rest")
b(0x57, 0x3E, 0x80, 0xE0, 0x11, 0x3E, 0xF2, 0xE0, 0x12)    # duty 50%, volume 15 falling
b(0x7B, 0xE0, 0x13, 0x7A, 0xF6, 0x80, 0xE0, 0x14)          # the note, triggered
label("rest")
b(0x79, 0xE6, 3)
jp("main", 0xC2)
b(0x79, 0xCB, 0x3F, 0xCB, 0x3F, 0x4F)     # c = step / 4
ldw(0x21, "bass"); b(0x09, 0x09)
b(0x2A, 0x5F, 0x7E, 0x57)
b(0x3E, 0x40, 0xE0, 0x16, 0x3E, 0x97, 0xE0, 0x17)          # volume 9, slowly falling
b(0x7B, 0xE0, 0x18, 0x7A, 0xF6, 0x80, 0xE0, 0x19)
jp("main")

label("tiles"); code.extend(TILES)
label("window"); code.extend(WINDOW)
label("melody"); code.extend(notes(MELODY))
label("bass"); code.extend(notes(BASS))

for kind, at, n in fix:
    t = labels[n]
    if kind == "r":
        d = t - (ORG + at + 1)
        assert -128 <= d < 128, n
        code[at] = d & 0xFF
    else:
        code[at] = t & 0xFF
        code[at + 1] = t >> 8

ROM[0x40] = 0xD9                          # VBLANK: reti (it only wakes the halt)
ROM[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])   # nop; jp $0150
ROM[0x134:0x143] = b"GB47 DEMO".ljust(15, b"\0")
ROM[0x143] = 0x00                         # Game Boy
ROM[0x147] = 0x00                         # ROM only
ROM[0x148] = 0x00                         # 32 KB
ROM[0x149] = 0x00                         # no RAM
ROM[0x14A] = 0x01
chk = 0
for i in range(0x134, 0x14D):
    chk = (chk - ROM[i] - 1) & 0xFF
ROM[0x14D] = chk
ROM[ORG:ORG + len(code)] = code
g = 0
for i, v in enumerate(ROM):
    if i not in (0x14E, 0x14F):
        g = (g + v) & 0xFFFF
ROM[0x14E], ROM[0x14F] = g >> 8, g & 0xFF
open(sys.argv[1], "wb").write(ROM)
print("%s: %d bytes of code and data" % (sys.argv[1], len(code)))
