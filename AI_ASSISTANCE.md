# AI Assistance Policy

## How this project uses AI

This project was **developed with the help of AI** — Claude (Anthropic), Gemini (Google), and Grok (xAI) — as a cooperation. AI assisted with brainstorming, code generation, and documentation, but I reviewed all output, validated results against reference implementations, and take full responsibility for the work and any mistakes.

## Responsible use

- ✅ **I checked the results** — The Game Boy is checked with the standard test cartridges: Blargg's CPU instruction and timing tests and dmg-acid2 (`test/gbtest.c`, `make test`); FPS47's LCD numbers come from my own DM42n, on battery and on USB power
- ✅ **I maintained domain expertise** — I guide the AI with the design decisions: the DM42n's screen, keys, memory and buzzer, and how the games should feel on the calculator
- ✅ **I take full responsibility** — The work is mine; any errors are mine to fix
- ✅ **I'm transparent** — This file documents how AI was used

## Testing & Validation

- CPU: Blargg's cpu_instrs (all 11) and instr_timing pass
- Picture: dmg-acid2 gives the reference picture (Peanut-GB's hash)
- The ROM cache, save states, the sound registers and the demo's tune: `make test`
- Every screen checked in the PC simulator (`make shots`, `make fps-shots`) before a commit
- The LCD timings measured on real DM42n hardware (FPS47); GB47 itself to be tested on the DM42n, on battery and on USB power

## License

This does not affect the GPL-3.0-or-later license. All code remains open-source and fully accessible.

---

*For more details, see `LICENSE` and the README.*
