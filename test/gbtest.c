/* gbtest.c - GB47's PC tests of the Game Boy (src/gb.c, src/sound.c and Peanut-GB):
 *   cpu_instrs    Blargg's CPU instruction tests: "Passed all tests" on the serial port
 *   instr_timing  Blargg's instruction timing test: "Passed"
 *   dmg-acid2     Matt Currie's PPU test: the picture's hash as Peanut-GB's own test expects it
 *   paged         cpu_instrs again with the ROM read from a file into a cache smaller than the ROM
 *   state         a save state loaded gives the same frames again
 *   sound         the registers, NR52, lengths, envelopes, and the buzzer's tone
 * The test ROMs are in test/roms (see its README). Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gb.h"
#include "sound.h"
#include "../test/roms/cpu_instrs.h"
#include "../test/roms/instr_timing.h"
#include "../test/roms/dmg-acid2.gb.h"

static int fails;
#define CHECK(c, ...) do { if (c) printf("  ok   "); else { printf("  FAIL "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static char out[4096];
static int nout;
static void tx(struct gb_s *g, uint8_t c) { (void)g; if (nout < (int)sizeof out - 1) out[nout++] = c < 32 ? ' ' : (char)c; out[nout] = 0; }

static int run_serial(const char *want, int max_frames) {
  nout = 0; out[0] = 0;
  gb_serial(tx);
  for (int f = 0; f < max_frames; f++) {
    gb_frame(0, 0);
    if (strstr(out, want)) return f;
    if (strstr(out, "Failed") || G.error) return -1;
  }
  return -1;
}

/* ------------------------------------------------------------------ the picture's hash, as Peanut-GB's test */
static uint8_t raw[GB_H][GB_W];
static void line_cb(const uint8_t *px, int line) { memcpy(raw[line], px, GB_W); }
static uint32_t fnv(const void *d, size_t n) {
  const uint8_t *b = d; uint32_t h = 2166136261u;
  while (n--) { h ^= *b++; h *= 16777619u; }
  return h;
}

/* ------------------------------------------------------------------ the ROM from a file, paged */
static FILE *romf;
static int fetches;
static int fetch(uint32_t p, uint8_t *dst) {
  fetches++;
  if (fseek(romf, (long)(p * GB_PAGE), SEEK_SET)) return -1;
  return fread(dst, 1, GB_PAGE, romf) == GB_PAGE ? 0 : -1;
}

int main(void) {
  printf("cpu_instrs (Blargg)\n");
  int e = gb_open(cpu_instrs_gb, cpu_instrs_gb_len);
  CHECK(!e, "open: %s, \"%s\", MBC%d", gb_error_text(e), G.title, G.mbc);
  int f = run_serial("Passed all tests", 60 * 120);
  CHECK(f > 0, "%s (%d frames)", out, f);

  printf("instr_timing (Blargg)\n");
  e = gb_open(instr_timing_gb, instr_timing_gb_len);
  f = run_serial("Passed", 60 * 30);
  CHECK(f > 0, "%s (%d frames)", out, f);

  printf("dmg-acid2\n");
  e = gb_open(dmg_acid2_gb, dmg_acid2_gb_len);
  gb_test_line = line_cb;
  for (int i = 0; i < 100; i++) gb_frame(1, 0);
  uint32_t h = fnv(raw, sizeof raw);
  CHECK(h == 0xF91DF416u, "the picture's hash %08X (Peanut-GB's reference F91DF416)", h);
  /* the packed picture holds the same shades */
  int same = 1;
  for (int y = 0; y < GB_H; y++)
    for (int x = 0; x < GB_W; x++)
      if (((G.fb[y * GB_ROW + x / 4] >> (6 - 2 * (x & 3))) & 3) != (raw[y][x] & 3)) same = 0;
  CHECK(same, "the packed 2-bit picture matches");
  FILE *pg = fopen("acid2.pgm", "w");
  if (pg) {
    fprintf(pg, "P2\n%d %d\n3\n", GB_W, GB_H);
    for (int y = 0; y < GB_H; y++) { for (int x = 0; x < GB_W; x++) fprintf(pg, "%d ", 3 - (raw[y][x] & 3)); fputc('\n', pg); }
    fclose(pg);
  }

  printf("save state\n");
  uint32_t n = gb_state_size();
  uint8_t *st = malloc(n);
  gb_state_save(st);
  for (int i = 0; i < 30; i++) gb_frame(1, i & 1 ? JOY_A : 0);
  uint32_t h1 = fnv(G.fb, GB_H * GB_ROW), f1 = G.frames;
  CHECK(gb_state_load(st, n) == 0, "loaded (%u bytes)", (unsigned)n);
  for (int i = 0; i < 30; i++) gb_frame(1, i & 1 ? JOY_A : 0);
  CHECK(fnv(G.fb, GB_H * GB_ROW) == h1, "the same picture after the same 30 frames (frames %u, %u)", (unsigned)f1, (unsigned)G.frames);
  st[0] ^= 1;
  CHECK(gb_state_load(st, n) == -1, "a damaged state is refused");
  free(st);
  gb_test_line = NULL;

  printf("paged ROM (cpu_instrs read from a file, 6 slots of 4 KB: bank 0 and 8 KB more)\n");
  romf = fopen("cpu_instrs.gb", "w+b");
  fwrite(cpu_instrs_gb, 1, cpu_instrs_gb_len, romf);
  fflush(romf);
  uint8_t *cache = malloc(6 * GB_PAGE);
  e = gb_open_paged(cpu_instrs_gb_len, cache, 6, fetch);
  CHECK(!e, "open paged: %s", gb_error_text(e));
  f = run_serial("Passed all tests", 60 * 120);
  CHECK(f > 0, "%s (%d frames, %u pieces read)", out, f, (unsigned)G.misses);
  CHECK(G.misses > 6, "%u pieces through 6 slots: evicted and read again as needed", (unsigned)G.misses);
  gb_close();
  free(cache);
  fclose(romf);

  printf("sound\n");
  sound_reset();
  CHECK(audio_read(0xFF26) == 0xF1, "NR52 after boot: %02X", audio_read(0xFF26));
  audio_write(0xFF26, 0x80); audio_write(0xFF25, 0xFF); audio_write(0xFF24, 0x77);
  audio_write(0xFF16, 0x80 | 0); audio_write(0xFF17, 0xF0);               /* ch2: volume 15, no envelope */
  audio_write(0xFF18, 0xD6); audio_write(0xFF19, 0x80 | 0x06);            /* 1750: 131072 / 298 = 439 Hz */
  uint32_t t = sound_tone();
  CHECK(t == 439 && sound_channel() == 2, "ch2 at 440 Hz: %u Hz from channel %d", (unsigned)t, sound_channel());
  audio_write(0xFF11, 0x80 | 0); audio_write(0xFF12, 0x80);               /* ch1: volume 8, higher pitch */
  audio_write(0xFF13, 0x00); audio_write(0xFF14, 0x80 | 0x07);
  sound_tone();
  CHECK(sound_channel() == 2, "the louder channel wins (ch2 15 against ch1 8)");
  audio_write(0xFF17, 0x80); audio_write(0xFF19, 0x86);                   /* ch2 now as loud, lower */
  sound_tone();
  CHECK(sound_channel() == 1, "as loud: the higher one (ch1)");
  audio_write(0xFF12, 0xF1); audio_write(0xFF14, 0x87);                   /* ch1: 15, falling every 1/64 s */
  for (int i = 0; i < 60; i++) sound_frame();
  CHECK(sound_tone() && sound_channel() == 2, "ch1 faded out, ch2 left");
  audio_write(0xFF16, 0x3F); audio_write(0xFF19, 0xC6);                   /* ch2: length 1/256 s */
  sound_frame();
  CHECK(!(audio_read(0xFF26) & 2), "ch2's length ran out: NR52 %02X", audio_read(0xFF26));
  audio_write(0xFF1A, 0x80); audio_write(0xFF1C, 0x20); audio_write(0xFF1D, 0x00); audio_write(0xFF1E, 0x87);
  t = sound_tone();
  CHECK(sound_channel() == 3 && t == 256, "only the wave channel left: %u Hz from channel %d", (unsigned)t, sound_channel());
  audio_write(0xFF26, 0x00);
  CHECK(!sound_tone() && audio_read(0xFF26) == 0x70 && audio_read(0xFF12) == 0x00, "sound off: silence, registers cleared");

  printf("the demo cartridge (release/GB47DEMO.GB): its tune on the buzzer\n");
  FILE *d = fopen("../../release/GB47DEMO.GB", "rb");
  if (!d) { printf("  FAIL no release/GB47DEMO.GB (make demo)\n"); fails++; }
  else {
    static uint8_t demo[0x8000];
    size_t dn = fread(demo, 1, sizeof demo, d);
    fclose(d);
    e = gb_open(demo, (uint32_t)dn);
    CHECK(!e && !strcmp(G.title, "GB47 DEMO"), "open: %s \"%s\"", gb_error_text(e), G.title);
    int by[4] = {0}, changes = 0;
    uint32_t was = 0;
    for (int i = 0; i < 600; i++) {
      gb_frame(i % 3 == 0, i < 300 ? JOY_RIGHT : JOY_LEFT);
      uint32_t t2 = sound_tone();
      by[sound_channel()]++;
      if (t2 != was) changes++;
      was = t2;
    }
    CHECK(by[1] > 300 && by[2] > 10, "10 s: melody (ch1) %d frames, bass in the rests (ch2) %d, silent %d; %d note changes",
          by[1], by[2], by[0], changes);
    CHECK(changes > 40, "the tune moves");
  }

  printf(fails ? "\n%d FAILED\n" : "\nall tests passed\n", fails);
  return fails != 0;
}
