/* gb.c - GB47: the Game Boy around Peanut-GB. See gb.h.
 *
 * The ROM: the CPU reads it through a table of 4 KB pieces (page[]). When the whole ROM is in memory every entry
 * points into it. When it is not (a cartridge bigger than the heap: Pokemon is 1 MB, Zelda 512 KB), the pieces are
 * read from the ROM file on first use into a cache of as many slots as the heap allows (as HP47 reads the HP 48's
 * ROM). The first 16 KB (bank 0, always mapped) stay; for the rest the slot used longest ago goes. A slot counts as
 * used when it was read, and each frame for the bank mapped then and the piece the CPU runs in.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <stdlib.h>
#include <string.h>
#include "gb.h"
#include "sound.h"

#define ENABLE_SOUND 1
#define ENABLE_LCD 1
#include "peanut_gb.h"

Gb G;

static struct gb_s *gb;                 /* Peanut-GB's machine (heap: about 17 KB) */
static const uint8_t **page;            /* ROM piece -> its bytes, NULL not loaded */
static uint32_t npages;
static uint8_t *cache;                  /* paged: the slots */
static int16_t *slot_page;              /* slot -> piece, -1 free */
static uint32_t *stamp;                 /* slot -> the tick it was last used */
static uint32_t tick;
static int (*fetch)(uint32_t page, uint8_t *dst);
static void (*serial_tx)(struct gb_s *, uint8_t);
#ifdef GB47_TEST
void (*gb_test_line)(const uint8_t *px, int line);
#endif

/* ------------------------------------------------------------------ the ROM */
static uint8_t rom_miss(uint32_t addr) {
  uint32_t p = addr / GB_PAGE;
  if (p >= npages || !cache) return 0xFF;
  int sl = -1;
  for (int i = 0; i < G.slots && sl < 0; i++) if (slot_page[i] < 0) sl = i;
  if (sl < 0) {                                       /* the one used longest ago, never bank 0 */
    uint32_t best = 0xFFFFFFFFu;
    for (int i = 0; i < G.slots; i++)
      if (slot_page[i] >= 4 && stamp[i] < best) { best = stamp[i]; sl = i; }
    if (sl < 0) return 0xFF;
    page[slot_page[sl]] = NULL;
  }
  uint8_t *dst = cache + (uint32_t)sl * GB_PAGE;
  if (!fetch || fetch(p, dst)) { G.fetch_error = 1; slot_page[sl] = -1; return 0xFF; }
  slot_page[sl] = (int16_t)p;
  stamp[sl] = ++tick;
  page[p] = dst;
  G.misses++;
  return dst[addr % GB_PAGE];
}

static uint8_t rom_read(struct gb_s *g, const uint_fast32_t addr) {
  (void)g;
  uint32_t p = addr / GB_PAGE;
  if (p < npages && page[p]) return page[p][addr % GB_PAGE];
  return rom_miss(addr);
}

static void touch(uint32_t p) {               /* the piece is in use: keep it */
  if (!cache || p >= npages || !page[p]) return;
  int sl = (int)((page[p] - cache) / GB_PAGE);
  if (sl >= 0 && sl < G.slots) stamp[sl] = tick;
}

static void touch_frame(void) {
  if (!G.paged) return;
  tick++;
  uint32_t bank = gb->selected_rom_bank;
  for (uint32_t i = 0; i < 4; i++) touch(bank * 4 + i);
  uint16_t pc = gb->cpu_reg.pc.reg;
  if (pc >= 0x4000 && pc < 0x8000) touch(bank * 4 + (pc - 0x4000) / GB_PAGE);
}

void gb_flush_cache(void) {
  if (!G.paged) return;
  for (int i = 0; i < G.slots; i++)
    if (slot_page[i] >= 4) { page[slot_page[i]] = NULL; slot_page[i] = -1; }
}

/* ------------------------------------------------------------------ the cartridge RAM */
static uint8_t ram_read(struct gb_s *g, const uint_fast32_t addr) {
  (void)g;
  return addr < G.ram_size ? G.ram[addr] : 0xFF;
}

static void ram_write(struct gb_s *g, const uint_fast32_t addr, const uint8_t val) {
  (void)g;
  if (addr >= G.ram_size) return;
  if (G.ram[addr] != val) { G.ram[addr] = val; G.ram_dirty = 1; G.ram_write_frame = G.frames; }
}

static void on_error(struct gb_s *g, const enum gb_error_e e, const uint16_t addr) {
  (void)g; (void)addr;
  G.error = (int)e + 1;
}

/* ------------------------------------------------------------------ the picture */
static void draw_line(struct gb_s *g, const uint8_t *px, const uint_fast8_t line) {
  (void)g;
  if (line >= GB_H) return;
  uint8_t *d = G.fb + line * GB_ROW;
  for (int i = 0; i < GB_ROW; i++, px += 4)
    d[i] = (uint8_t)((px[0] & 3) << 6 | (px[1] & 3) << 4 | (px[2] & 3) << 2 | (px[3] & 3));
  G.drawn = 1;
#ifdef GB47_TEST
  if (gb_test_line) gb_test_line(px - GB_W, line);
#endif
}

/* ------------------------------------------------------------------ open, close */
int gb_header_ok(const uint8_t *h, uint32_t n) {
  if (n < 0x150) return 0;
  uint8_t x = 0;
  for (int i = 0x134; i <= 0x14C; i++) x = (uint8_t)(x - h[i] - 1);
  return x == h[0x14D];
}

static int start(void) {
  gb = malloc(sizeof *gb);
  G.fb = calloc(GB_H, GB_ROW);
  if (!gb || !G.fb) return 10;
  memset(gb, 0, sizeof *gb);
  sound_reset();
  enum gb_init_error_e e = gb_init(gb, rom_read, ram_read, ram_write, on_error, NULL);
  if (e != GB_INIT_NO_ERROR) return e == GB_INIT_CARTRIDGE_UNSUPPORTED ? 11 : e == GB_INIT_INVALID_CHECKSUM ? 12 : 13;
  if (G.fetch_error) return 14;
  uint8_t c = rom_read(gb, 0x143);
  G.cgb = c == 0xC0 ? 2 : c == 0x80 ? 1 : 0;
  if (G.cgb == 2) return 15;
  G.mbc = gb->mbc;
  gb_get_rom_name(gb, G.title);
  size_t rs = 0;
  if (gb_get_save_size_s(gb, &rs) < 0) rs = 0;
  if (rs) {
    G.ram = malloc(rs);
    if (!G.ram) return 10;
    memset(G.ram, 0xFF, rs);
    G.ram_size = (uint32_t)rs;
  }
  gb_init_lcd(gb, draw_line);
  gb_init_serial(gb, serial_tx, NULL);
  return 0;
}

static int pages_for(uint32_t rom_size) {
  if (rom_size < 0x8000 || rom_size > 8u * 1024u * 1024u || rom_size % 0x4000) return 16;
  npages = rom_size / GB_PAGE;
  page = calloc(npages, sizeof *page);
  return page ? 0 : 10;
}

int gb_open(const uint8_t *rom, uint32_t rom_size) {
  gb_close();
  int e = pages_for(rom_size);
  if (e) return e;
  for (uint32_t p = 0; p < npages; p++) page[p] = rom + p * GB_PAGE;
  G.rom_size = rom_size;
  return start();
}

int gb_open_paged(uint32_t rom_size, uint8_t *c, int slots, int (*f)(uint32_t page, uint8_t *dst)) {
  gb_close();
  if (slots < 6) return 10;                         /* bank 0 (4) and 2 more */
  int e = pages_for(rom_size);
  if (e) return e;
  slot_page = malloc((size_t)slots * sizeof *slot_page);
  stamp = calloc((size_t)slots, sizeof *stamp);
  if (!slot_page || !stamp) return 10;
  cache = c; fetch = f; G.slots = slots; G.paged = 1; G.rom_size = rom_size;
  for (int i = 0; i < slots; i++) slot_page[i] = -1;
  for (uint32_t a = 0; a < 0x4000; a += GB_PAGE) rom_read(NULL, a);    /* bank 0 */
  if (G.fetch_error) return 14;
  return start();
}

void gb_close(void) {
  free(gb); gb = NULL;
  free(G.fb); free(G.ram);
  free(page); page = NULL; npages = 0;
  free(slot_page); slot_page = NULL;
  free(stamp); stamp = NULL;
  cache = NULL; fetch = NULL; tick = 0;
  memset(&G, 0, sizeof G);
}

const char *gb_error_text(int e) {
  switch (e) {
  case 0: return "ok";
  case 10: return "not enough memory";
  case 11: return "cartridge type not supported";
  case 12: return "not a Game Boy ROM (header)";
  case 14: return "cannot read the ROM file";
  case 15: return "Game Boy Color only";
  case 16: return "not a Game Boy ROM (size)";
  default: return "cannot start the ROM";
  }
}

void gb_reset_game(void) {
  if (!gb) return;
  gb_reset(gb);
  sound_reset();
  G.error = 0;
}

/* ------------------------------------------------------------------ run */
void gb_frame(int draw, uint8_t joy) {
  if (!gb || G.error) return;
  gb->direct.joypad = (uint8_t)~joy;                  /* Peanut-GB: 0 = pressed */
  gb->display.lcd_draw_line = draw ? draw_line : NULL;  /* frames not shown are not drawn */
  gb_run_frame(gb);
  sound_frame();
  G.frames++;
  touch_frame();
}

void gb_rtc(int sec, int min, int hour, int day) {
  if (!gb || G.mbc != 3) return;
  gb->rtc_real.reg.sec = (uint8_t)sec;
  gb->rtc_real.reg.min = (uint8_t)min;
  gb->rtc_real.reg.hour = (uint8_t)hour;
  gb->rtc_real.reg.yday = (uint8_t)day;
  gb->rtc_real.reg.high = (uint8_t)((gb->rtc_real.reg.high & 0xFE) | ((day >> 8) & 1));
}

/* ------------------------------------------------------------------ save states */
#define ST_MAGIC "GB47ST1"
typedef struct { char magic[8]; uint32_t core, ram, sound; char title[17]; uint8_t hdr_sum, pad[2]; } StHead;

uint32_t gb_state_size(void) {
  return (uint32_t)sizeof(StHead) + (uint32_t)sizeof *gb + G.ram_size + sound_state_size();
}

void gb_state_save(uint8_t *d) {
  StHead h;
  memset(&h, 0, sizeof h);
  memcpy(h.magic, ST_MAGIC, 8);
  h.core = sizeof *gb; h.ram = G.ram_size; h.sound = sound_state_size();
  memcpy(h.title, G.title, sizeof h.title);
  h.hdr_sum = rom_read(gb, 0x14D);
  memcpy(d, &h, sizeof h); d += sizeof h;
  memcpy(d, gb, sizeof *gb); d += sizeof *gb;
  if (G.ram_size) memcpy(d, G.ram, G.ram_size);
  d += G.ram_size;
  sound_state_save(d);
}

int gb_state_load(const uint8_t *s, uint32_t n) {
  StHead h;
  if (!gb || n != gb_state_size()) return -1;
  memcpy(&h, s, sizeof h);
  if (memcmp(h.magic, ST_MAGIC, 8) || h.core != sizeof *gb || h.ram != G.ram_size || h.sound != sound_state_size()
      || memcmp(h.title, G.title, sizeof h.title) || h.hdr_sum != rom_read(gb, 0x14D)) return -1;
  s += sizeof h;
  struct gb_s keep = *gb;                             /* the callbacks are this program's, not the file's */
  memcpy(gb, s, sizeof *gb); s += sizeof *gb;
  gb->gb_rom_read = keep.gb_rom_read; gb->gb_cart_ram_read = keep.gb_cart_ram_read;
  gb->gb_cart_ram_write = keep.gb_cart_ram_write; gb->gb_error = keep.gb_error;
  gb->gb_serial_tx = keep.gb_serial_tx; gb->gb_serial_rx = keep.gb_serial_rx;
  gb->gb_bootrom_read = keep.gb_bootrom_read; gb->display.lcd_draw_line = keep.display.lcd_draw_line;
  gb->direct.priv = keep.direct.priv;
  if (G.ram_size) memcpy(G.ram, s, G.ram_size);
  s += G.ram_size;
  sound_state_load(s);
  G.ram_dirty = 1; G.ram_write_frame = G.frames; G.error = 0;
  return 0;
}

#ifdef GB47_TEST
struct gb_s *gb_core(void) { return gb; }
void gb_serial(void (*tx)(struct gb_s *, uint8_t)) { serial_tx = tx; if (gb) gb_init_serial(gb, tx, NULL); }
#endif
