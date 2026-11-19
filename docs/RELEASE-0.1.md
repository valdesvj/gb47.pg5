# GB47 0.1 — a Game Boy for the SwissMicros DM42n

First release, for testing: Game Boy games on the DM42n, with the tune on the buzzer. It passes the Game Boy test
cartridges on the PC (Blargg's CPU tests, dmg-acid2) and runs in the simulator; **how fast it runs on the
calculator is what this release is for**. Please send the speed line (F6) from your games, on battery and on USB.

**Files**
- `GB47.pg5` — the program (DMCP5 3.57 or later). Copy it to the calculator's disk, then **Load program → GB47.pg5**.
- `GB47DEMO.GB` — our own little cartridge (grey bands scrolling, a ball the pad moves, a tune): put it in a `GB47`
  folder on the disk with your cartridges, then **F1** in GB47's menu.
- `MANUAL.md` — keys, menu, saves, sound.

**What it does**
- original Game Boy cartridges: ROM only, MBC1, MBC2, MBC3 (clock), MBC5; dual-mode `.gbc` as on a Game Boy
- Peanut-GB (MIT) as the Game Boy: passes Blargg's cpu_instrs and instr_timing, and dmg-acid2
- big cartridges (Pokémon 1 MB) read from the disk 4 KB at a time into a cache when the heap can't hold them
- the picture big (1.5×) or small (1×), four greys as dots or two-tone, invert; only the changed lines sent, and a
  frame drawn only when the LCD is ready for it
- the melody on the buzzer (the louder square channel; the bass in its rests)
- the game's saves to `/GB47/<game>.SAV` by themselves (the usual `.sav` of other emulators), save states
- the pad on the digit block with diagonals, × A, − B, ÷ A+B, SHIFT holds B; fast forward, screenshots (F5),
  DMCP's setup menu (USB disk, System), About

**Not yet:** Game Boy Color-only games, the noise channel and chords (one buzzer voice), link cable.

See the [manual](MANUAL.md). GPL-3.0-or-later © 2026 Victor Valdes; written with the help of AI (`AI_ASSISTANCE.md`).
Peanut-GB is MIT; the DMCP5 SDK is BSD-3 ("NOMAS"). No games included. Please don't contact SwissMicros about this
program. Game Boy is a trademark of Nintendo; this project is not affiliated with or endorsed by Nintendo or SwissMicros.
