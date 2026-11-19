# Test cartridges (PC tests only)

These are for `make test` (test/gbtest.c) and the simulator's screens, as C arrays (`xxd -i`). They are not
games and are not part of GB47.pg5. `tools/hex2bin.py` turns one back into its `.gb` file.

| File | Cartridge | Author, licence |
|---|---|---|
| `cpu_instrs.h` | cpu_instrs: all the CPU's instructions | Shay Green (Blargg), freely distributed |
| `instr_timing.h` | instr_timing: the instructions' cycle counts | Shay Green (Blargg), freely distributed |
| `dmg-acid2.gb.h` | dmg-acid2: the picture (PPU) test | Matt Currie, MIT |

All three come from [Peanut-GB](https://github.com/deltabeard/Peanut-GB)'s `test/` directory, where its own tests
use them. dmg-acid2's reference hash (0xF91DF416) is Peanut-GB's.
