# GB47 manual

GB47 0.1 — a Game Boy for the SwissMicros DM42n. See the [README](../README.md) for building and the files.

## Installing and playing

1. Copy `release/GB47.pg5` to the calculator's disk (keep `DM42-x.yy.pg5` there too, to go back).
2. Make a folder **`/GB47`** on the disk and copy your cartridges into it as `.gb` (or `.gbc`) files. To try GB47
   without a game, copy `release/GB47DEMO.GB`: our own little cartridge (bands of the four greys scrolling, a ball the
   pad moves, a tune).
3. **SETUP → System → Run DMCP** (or RESET with F1 held), **Load program → GB47.pg5**.
4. The menu opens: **F1** lists the `.gb` files, **F2** the `.gbc` files. Pick one, and it plays. Next time GB47
   starts with the same game, its saves read back.

Needs DMCP5 3.57 or later. GB47 comes with no games: use your own cartridges' dumps or homebrew.

### Keys

| Key | Game Boy | | Key | GB47 |
|---|---|---|---|---|
| **8** / ▲ | up | | **EXIT** | the menu |
| **4** | left | | **SHIFT** | B held down (on/off), for games that run with B |
| **6** | right | | **F1** (held) | fast forward |
| **5** / **2** / ▼ | down | | **F2** | sound on/off |
| **7 9 1 3** | the diagonals | | **F3** | 4 greys / two-tone |
| **×** | A | | **F4** | big / small |
| **−** and **+** | B | | **F5** | screenshot (`/SCREENS`) |
| **÷** | A and B together | | **F6** | speed shown on/off |
| **ENTER** and **R/S** | START | | | |
| **←** and **.** | SELECT | | | |

The calculator reads **two keys at once**. A direction and a button is fine; for three buttons, **÷** presses A and B
together (6 + ÷ is Mario's running jump), and **SHIFT** keeps B down (then 6 + × runs and jumps).

### The menu (EXIT)

| | |
|---|---|
| **F1 GB**, **F2 GBC** | load a cartridge from `/GB47` |
| **F3 SAVEST** | save the whole Game Boy as it is now: `/GB47/<game>.STA` |
| **F4 LOADST** | go back to that state |
| **F5 RESET** | restart the game (its saves stay) |
| **F6 OFF** | turn off; ON comes back to the game where it was |
| **1 – 5** | picture big/small, greys, invert, sound, speed shown |
| **6** key test, **7** DMCP's setup (USB disk, time, System), **8** About | |

**EXIT** or **ENTER** goes back to the game.

### The game's saves

A game "saves" by writing its cartridge RAM. GB47 writes that to `/GB47/<game>.SAV` two seconds after the game last
wrote to it ("game saved" shows in the status line), and when the menu opens or the calculator turns off. The file
is the usual `.sav` of other emulators: you can bring your saves over, or take them to a PC.

## Sound

The Game Boy has four voices; the DM42n's buzzer has one, with no volume. GB47 keeps the Game Boy's sound registers
as the real one does (lengths, volume envelopes, channel 1's sweep) and plays one note: the louder of the two
square-wave channels (the higher one when both are as loud, which is usually the melody), the wave channel (the bass
in most games) only when both are silent, and never the noise channel (the drums). So you hear the tune, not the
chords. **F2** turns it off.

## Speed

The game always runs at the Game Boy's own speed (59.7 frames a second) as long as the calculator keeps up; the LCD
shows as many of those frames as it can. FPS47's numbers (below) say the LCD takes about 0.22 ms a line: a whole big
picture 48 ms, so 15–20 shown frames a second when everything moves (scrolling), and every frame when only a little
of the screen changes (menus, text, Pokémon's battles). GB47 draws a frame only when the LCD will have finished the
last one, so time isn't spent drawing frames nobody sees.

**F6** shows the speed in the status line: `98% z4.1 s9.0 18fps` is the Game Boy's speed, ms to run a frame, ms to
draw one, and frames shown a second. Please send me those numbers from your games, on battery and on USB.
If a game is slow when everything scrolls, try **small** (F4): 144 lines instead of 216.

## Memory and the ROM cache

When the heap has room for the whole cartridge (and 96 KB for the rest), it is read into memory. When it does not
(Pokémon is 1 MB), the ROM file stays open and is read 4 KB at a time as the game needs it, into a cache as big as the
heap allows; bank 0 always stays. *About* (menu 8) shows which, the cache's size and how many pieces were read.
The first time a game reaches a new part (a new town, a new world) it may wait a moment for the disk.

## Screenshots

**F5** while playing saves the whole LCD as a `.bmp` in `/SCREENS` ("screenshot saved" in the status line).
Copy them off with the USB disk: EXIT → **7 setup** → USB disk. (Not SHIFT: in GB47 it holds B.)

## When something is wrong

| Message | Why |
|---|---|
| the file is not in the list | it must be in `/GB47` and end in `.gb` (F1) or `.gbc` (F2) |
| not a Game Boy ROM | the file is damaged or not a Game Boy dump (its header's checksum is wrong) |
| Game Boy Color only | a cartridge for the Game Boy Color only: GB47 is an original Game Boy |
| cartridge type not supported | a rare cartridge chip (MBC7, HuC1, HuC3, the camera ...) |
| no room | the heap is too small even for the cache: turn the calculator off and on, load again |
| cannot read the ROM file | the file changed or went away while the game ran (the USB disk): load it again |
