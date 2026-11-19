/*
 * GB47 for the SwissMicros DM42n (DMCP5 native program, .pg5): a Game Boy.
 * (C) 2026 Victor Valdes (valdes.vj@gmail.com) - GPL-3.0-or-later
 *
 * Built like ZX47 (the loop, the screen, the menu) and HP47 (the ROM cache). The Game Boy itself is Peanut-GB (MIT).
 * The screen: the picture in the middle (screen.c: 1.5x or 1x, four greys as dots), the status line above it, the
 * keys in the margins. The loop: read the two keys held, run one Game Boy frame (16.7 ms of Game Boy time), draw it
 * only if the LCD will have finished the last one (FPS47: 0.22 ms a line), play the tune's note on the buzzer, wait
 * for the next 16.7 ms. The game runs at full speed; the LCD shows as many of its frames as it can (15-20 a second
 * when the whole picture moves, every frame when little of it does).
 * The cartridge's RAM (the game's saves) goes to /GB47/<game>.SAV two seconds after the game last wrote it, and
 * when the menu opens or the calculator turns off. EXIT opens the menu: load a ROM from /GB47, save or load a state,
 * reset, the picture and sound settings, off.
 */
#include <main.h>
#include <dmcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gb.h"
#include "sound.h"
#include "screen.h"
#include "keys.h"

#define DIR        "/GB47"
#define CFG_FILE   DIR "/GB47.CFG"
#define RESERVE    (96u * 1024u)                 /* the heap kept for the rest: the machine, saving a state, menus */
#define LINE_US    230                           /* the LCD's time a line (FPS47: 48 ms for 216 lines) */
#define MAX_SKIP   6                             /* frames not shown in a row when behind */

typedef struct { int size, greys, invert, sound, speed; char last[64]; } Cfg;
static Cfg cfg = { SIZE_BIG, GREYS_DOTS, 0, 1, 0, "" };

static char game[40] = "";                       /* the ROM's file name, for the status line and its .SAV */
static char msg[64] = "";                        /* the menu's message line */
static int loaded;                               /* a game is in */
static int block;                                /* B held down by SHIFT */

/* ------------------------------------------------------------------ text */
static void text(int x, int y, const char *s, int inv) {   /* as Chess 47: no line fill (it would clear the row) */
  uint8_t lnf = t20->lnfill, nl = t20->newln;
  t20->lnfill = 0; t20->newln = 0; t20->inv = (uint8_t)inv;
  lcd_setXY(t20, x, y);
  lcd_writeText(t20, s);
  t20->inv = 0; t20->lnfill = lnf; t20->newln = nl;
}

static void bar(const char *s) { lcd_fill_rect(0, 0, LCD_X, 22, 1); text(4, 1, s, 1); }

/* ------------------------------------------------------------------ settings */
static void cfg_save(void) {
  if (!sys_disk_ok()) return;
  char s[160];
  int n = sprintf(s, "size=%d\ngreys=%d\ninvert=%d\nsound=%d\nspeed=%d\nlast=%s\n",
                  cfg.size, cfg.greys, cfg.invert, cfg.sound, cfg.speed, cfg.last);
  sys_disk_write_enable(1);
  check_create_dir(DIR);
  FIL f; UINT bw;
  if (f_open(&f, CFG_FILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) { f_write(&f, s, (UINT)n, &bw); f_close(&f); }
  sys_disk_write_enable(0);
}

static void cfg_load(void) {
  FIL f; UINT br;
  char s[200];
  if (f_open(&f, CFG_FILE, FA_READ) != FR_OK) return;
  f_read(&f, s, sizeof s - 1, &br);
  f_close(&f);
  s[br] = 0;
  static const struct { const char *k; int *v, max; } T[] = {
    {"size", &cfg.size, SIZE_CNT - 1}, {"greys", &cfg.greys, GREYS_CNT - 1}, {"invert", &cfg.invert, 1},
    {"sound", &cfg.sound, 1}, {"speed", &cfg.speed, 1} };
  for (unsigned i = 0; i < sizeof T / sizeof *T; i++) {
    char key[16];
    sprintf(key, "%s=", T[i].k);
    char *p = strstr(s, key);
    if (p) { int v = atoi(p + strlen(key)); if (v >= 0 && v <= T[i].max) *T[i].v = v; }
  }
  char *p = strstr(s, "last=");
  if (p) {
    p += 5;
    int i = 0;
    while (*p && *p != '\n' && i < (int)sizeof cfg.last - 1) cfg.last[i++] = *p++;
    cfg.last[i] = 0;
  }
}

/* ------------------------------------------------------------------ the ROM: in the heap, or paged from its file */
static uint8_t *rom;                             /* the whole ROM (resident) */
static uint8_t *cache;                           /* the cache's slots (paged) */
static FIL *romf;                                /* the ROM file, open while paged (closed when off) */
static char rom_path[64];
static int rom_open;

static void rom_file_close(void) { if (romf && rom_open) f_close(romf); rom_open = 0; }

static int fetch(uint32_t p, uint8_t *dst) {
  if (!romf) return -1;
  if (!rom_open) { if (f_open(romf, rom_path, FA_READ) != FR_OK) return -1; rom_open = 1; }
  UINT br = 0;
  if (f_lseek(romf, (FSIZE_t)p * GB_PAGE) != FR_OK || f_read(romf, dst, GB_PAGE, &br) != FR_OK) return -1;
  return br == GB_PAGE ? 0 : -1;
}

/* ------------------------------------------------------------------ the cartridge's RAM: the game's saves */
static void sav_path(char *p, const char *ext) {
  char base[40];
  strcpy(base, game);
  char *dot = strrchr(base, '.');
  if (dot) *dot = 0;
  snprintf(p, 64, DIR "/%s.%s", base, ext);
}

static int sav_write(void) {                     /* 1 written */
  if (!loaded || !G.ram_size || !G.ram_dirty || !sys_disk_ok()) return 0;
  char p[64];
  sav_path(p, "SAV");
  rom_file_close();
  sys_disk_write_enable(1);
  check_create_dir(DIR);
  FIL f; UINT bw = 0;
  if (f_open(&f, p, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) { f_write(&f, G.ram, G.ram_size, &bw); f_close(&f); }
  sys_disk_write_enable(0);
  if (bw == G.ram_size) G.ram_dirty = 0;
  return bw == G.ram_size;
}

static void sav_read(void) {
  if (!G.ram_size) return;
  char p[64];
  sav_path(p, "SAV");
  FIL f; UINT br = 0;
  if (f_open(&f, p, FA_READ) != FR_OK) return;
  if (f_size(&f) >= G.ram_size) f_read(&f, G.ram, G.ram_size, &br);   /* other emulators may add the clock */
  f_close(&f);
  G.ram_dirty = 0;
}

static void game_close(void) {
  if (loaded) sav_write();
  gb_close();
  rom_file_close();
  free(romf); romf = NULL;
  free(rom); rom = NULL;
  free(cache); cache = NULL;
  loaded = 0;
}

static int rom_load(const char *path, const char *fname) {   /* 1 loaded, else msg says why */
  game_close();
  lcd_refresh_wait();
  lcd_clear_buf();
  bar("GB47: reading the cartridge");
  text(4, 40, fname, 0);
  lcd_refresh();
  romf = malloc(sizeof *romf);
  if (!romf) { strcpy(msg, "no memory"); return 0; }
  if (f_open(romf, path, FA_READ) != FR_OK) { snprintf(msg, sizeof msg, "%s: cannot open", fname); free(romf); romf = NULL; return 0; }
  uint32_t size = (uint32_t)f_size(romf);
  uint8_t h[0x150];
  UINT br = 0;
  f_read(romf, h, sizeof h, &br);
  if (br != sizeof h || !gb_header_ok(h, br)) {
    snprintf(msg, sizeof msg, "%s: not a Game Boy ROM", fname);
    f_close(romf); free(romf); romf = NULL; return 0;
  }
  if (h[0x143] == 0xC0) {
    snprintf(msg, sizeof msg, "%s: Game Boy Color only", fname);
    f_close(romf); free(romf); romf = NULL; return 0;
  }
  static const uint32_t RAMS[6] = { 0, 2048, 8192, 32768, 131072, 65536 };
  uint32_t ram = h[0x149] < 6 ? RAMS[h[0x149]] : 0;
  uint32_t largest = (uint32_t)sys_largest_free_mem(), need = RESERVE + ram;
  int e;
  if (largest >= size + need && (rom = malloc(size))) {             /* all of it in memory */
    f_lseek(romf, 0);
    br = 0;
    f_read(romf, rom, size, &br);
    f_close(romf); free(romf); romf = NULL;
    e = br == size ? gb_open(rom, size) : 14;
  } else {                                                           /* read 4 KB at a time as the game needs */
    int slots = largest > need ? (int)((largest - need) / GB_PAGE) : 0;
    if (slots > (int)(size / GB_PAGE)) slots = (int)(size / GB_PAGE);
    cache = slots >= 6 ? malloc((size_t)slots * GB_PAGE) : NULL;
    if (!cache) { snprintf(msg, sizeof msg, "%s: no room (heap %u KB)", fname, (unsigned)(largest >> 10)); game_close(); return 0; }
    strncpy(rom_path, path, sizeof rom_path - 1);
    rom_path[sizeof rom_path - 1] = 0;
    rom_open = 1;
    e = gb_open_paged(size, cache, slots, fetch);
  }
  if (e) { snprintf(msg, sizeof msg, "%s: %s", fname, gb_error_text(e)); game_close(); return 0; }
  strncpy(game, fname, sizeof game - 1);
  game[sizeof game - 1] = 0;
  strncpy(cfg.last, path, sizeof cfg.last - 1);
  cfg.last[sizeof cfg.last - 1] = 0;
  loaded = 1;
  block = 0;
  sav_read();
  snprintf(msg, sizeof msg, "%s: %s", fname, G.title);
  return 1;
}

static int load_ok;
static int load_cb(const char *fpath, const char *fname, void *data) {
  (void)data;
  load_ok = rom_load(fpath, fname);
  return MRET_EXIT;
}

static int load(int kind) {                      /* 1: a game was loaded */
  load_ok = 0;
  check_create_dir(DIR);
  file_selection_screen(kind ? "Load a .GBC cartridge" : "Load a .GB cartridge", DIR, kind ? ".gbc" : ".gb",
                        load_cb, 0, 0, NULL);
  wait_for_key_release(-1);
  return load_ok;
}

/* ------------------------------------------------------------------ save states: /GB47/<game>.STA */
static void state_save(void) {
  if (!loaded) { strcpy(msg, "no game in"); return; }
  uint32_t n = gb_state_size();
  uint8_t *b = malloc(n);
  if (!b) { strcpy(msg, "save state: no memory"); return; }
  gb_state_save(b);
  char p[64];
  sav_path(p, "STA");
  rom_file_close();
  sys_disk_write_enable(1);
  check_create_dir(DIR);
  FIL f; UINT bw = 0;
  if (f_open(&f, p, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) { f_write(&f, b, n, &bw); f_close(&f); }
  sys_disk_write_enable(0);
  free(b);
  snprintf(msg, sizeof msg, bw == n ? "state saved: %.40s" : "state NOT saved: %.40s", p + sizeof DIR);
}

static int state_load(void) {                    /* 1 loaded */
  if (!loaded) { strcpy(msg, "no game in"); return 0; }
  char p[64];
  sav_path(p, "STA");
  uint32_t n = gb_state_size();
  FIL f; UINT br = 0;
  if (f_open(&f, p, FA_READ) != FR_OK) { snprintf(msg, sizeof msg, "no state saved (%.40s)", p + sizeof DIR); return 0; }
  uint8_t *b = f_size(&f) == n ? malloc(n) : NULL;
  if (b) f_read(&f, b, n, &br);
  f_close(&f);
  int ok = b && br == n && gb_state_load(b, n) == 0;
  free(b);
  snprintf(msg, sizeof msg, ok ? "state loaded: %.40s" : "%.40s: not this game's state", p + sizeof DIR);
  return ok;
}

/* ------------------------------------------------------------------ the frame around the picture */
static char status_now[96];
static int pct = 100, emu_ms, scr_ms, fps;       /* the last second: speed, ms a frame (x10), frames shown */
static char note[40];                            /* a short message in the status line ... */
static uint32_t note_until;                      /* ... until this time */

static void set_note(const char *s) { strncpy(note, s, sizeof note - 1); note[sizeof note - 1] = 0; note_until = sys_current_ms() + 2000; }

static void status(int turbo) {
  char s[96];
  if (note[0] && (int32_t)(sys_current_ms() - note_until) < 0) snprintf(s, sizeof s, "GB47  %s", note);
  else {
    note[0] = 0;
    if (cfg.speed) snprintf(s, sizeof s, "%d%% z%d.%d s%d.%d %dfps %s%s", pct, emu_ms / 10, emu_ms % 10, scr_ms / 10,
                            scr_ms % 10, fps, block ? "B " : "", turbo ? ">>" : "");
    else snprintf(s, sizeof s, "GB47  %-16s %s%s", G.title, block ? "  B held" : "", turbo ? "  >>" : "");
  }
  if (!strcmp(s, status_now)) return;
  strcpy(status_now, s);
  lcd_refresh_wait();                            /* the picture's last lines may still be going out */
  lcd_fill_rect(0, 0, LCD_X, 22, 0);
  text(4, 1, s, 0);
  lcd_fill_rect(0, 21, LCD_X, 1, 1);
  lcd_refresh_lines(0, 22);
}

static void frame_ui(void) {                     /* the whole LCD: the keys in the margins, the status */
  static const char *L[] = { "Pad:", " 8 up", " 4 left", " 6 right", " 5 down", " 7 9 1 3", " diag.", "EXIT", " menu" };
  static const char *R[] = { "x  A", "-  B", "/  A+B", "ENTER", " Start", "<-", " Select", "SHIFT", " B held" };
  lcd_refresh_wait();
  lcd_clear_buf();
  screen_layout(cfg.size);
  status_now[0] = 0;
  int xr = screen_x() + screen_w() + 6;
  for (unsigned i = 0; i < sizeof L / sizeof *L; i++) {
    text(2, 28 + (int)i * 22, L[i], 0);
    text(xr, 28 + (int)i * 22, R[i], 0);
  }
  if (cfg.size == SIZE_SMALL) {                 /* a frame around the small picture */
    lcd_fill_rect((uint32_t)screen_x() - 2, (uint32_t)screen_y() - 2, (uint32_t)screen_w() + 4, (uint32_t)screen_h() + 4, 1);
    lcd_fill_rect((uint32_t)screen_x(), (uint32_t)screen_y(), (uint32_t)screen_w(), (uint32_t)screen_h(), 0);
  }
  lcd_forced_refresh();
  lcd_refresh_wait();
  screen_invalidate();
}

/* ------------------------------------------------------------------ keys */
static int read_keys(int *k1, int *k2) {
  *k1 = *k2 = 0;
  __sysfn_read_key(k1, k2);
  if (*k1 < 0) *k1 = 0;
  if (*k2 < 0) *k2 = 0;
  return (*k1 > 0) + (*k2 > 0);
}

static void wait_release(void) { int a, b; while (read_keys(&a, &b)) sys_delay(10); key_pop_all(); }

static void keytest(void) {
  lcd_clear_buf();
  bar("Key test: the keys held (EXIT twice ends)");
  lcd_refresh();
  int exits = 0, was = -1;
  for (;;) {
    int a, b;
    read_keys(&a, &b);
    int now = a * 100 + b;
    if (now != was) {
      char s[64];
      uint8_t j = keys_joy(a, b);
      sprintf(s, "k1 = %2d   k2 = %2d   %s%s%s%s%s%s%s%s", a, b, j & JOY_UP ? "U" : "", j & JOY_DOWN ? "D" : "",
              j & JOY_LEFT ? "L" : "", j & JOY_RIGHT ? "R" : "", j & JOY_A ? " A" : "", j & JOY_B ? " B" : "",
              j & JOY_START ? " START" : "", j & JOY_SELECT ? " SELECT" : "");
      lcd_fill_rect(0, 60, LCD_X, 24, 0);
      text(20, 62, s, 0);
      lcd_refresh_lines(60, 24);
      lcd_refresh_wait();
      if (a == KEY_EXIT && was != now) exits++;
      was = now;
    }
    if (exits >= 2 && !a) break;
    sys_delay(20);
    reset_auto_off();
  }
  key_pop_all();
}

/* ------------------------------------------------------------------ About, and DMCP's setup menu (as ZX47) */
static void about(void) {
  char s[64];
  lcd_clear_buf();
  bar("GB47 v" PROGRAM_VERSION "   a Game Boy for the DM42n");
  int y = 26;
  static const char *L[] = {
    "(C) 2026 Victor Valdes - GPL-3.0-or-later",
    "Game Boy core: Peanut-GB (MIT)",
    "DMCP5 SDK (BSD-3, NOMAS)",
  };
  for (unsigned i = 0; i < sizeof L / sizeof *L; i++) { text(4, y, L[i], 0); y += 21; }
  y += 4;
  sprintf(s, "Free heap: %d KB  (largest %d KB)", sys_free_mem() / 1024, sys_largest_free_mem() / 1024);
  text(4, y, s, 0); y += 21;
  sprintf(s, "CPU: %s   LCD: %s", usb_powered() ? "160 MHz (USB)" : "80 MHz (battery)", screen_fast() ? "fast" : "slow path");
  text(4, y, s, 0); y += 21;
  if (loaded) {
    if (G.paged) sprintf(s, "ROM %u KB: cache %d KB, %u reads", (unsigned)(G.rom_size >> 10), G.slots * 4, (unsigned)G.misses);
    else sprintf(s, "ROM %u KB in memory, MBC%d, RAM %u KB", (unsigned)(G.rom_size >> 10), G.mbc, (unsigned)(G.ram_size >> 10));
    text(4, y, s, 0); y += 21;
  }
  text(4, y + 4, "Written with the help of AI (see README)", 0);
  lcd_refresh();
  lcd_refresh_wait();
  wait_release();
  for (;;) {
    if (key_empty()) { sys_timer_start(0, 500); CLR_ST(STAT_RUNNING); sys_sleep(); }
    if (ST(STAT_PGM_END) || ST(STAT_SUSPENDED)) return;
    int k = key_pop();
    if (k == KEY_EXIT || k == KEY_ENTER) break;
  }
  wait_release();
}

#define MI_GB47_ABOUT 1
static const uint8_t setup_items[] = {
  MI_SET_TIME, MI_SET_DATE, MI_BEEP_MUTE, MI_SLOW_AUTOREP, MI_MSC, MI_SYSTEM_ENTER, MI_GB47_ABOUT, 0 };
static const smenu_t SETUP_MENU = { "GB47 Setup", setup_items, NULL, NULL };

static int run_menu_item(uint8_t id) {
  if (id == MI_GB47_ABOUT) { about(); return 0; }
  return MRET_UNIMPL;
}

static const char *menu_line_str(uint8_t id, char *s, const int slen) {
  (void)s; (void)slen;
  return id == MI_GB47_ABOUT ? "About GB47 >" : NULL;
}

/* ------------------------------------------------------------------ the menu */
static void menu_draw(void) {
  char l[7][56];
  lcd_clear_buf();
  bar("GB47 " PROGRAM_VERSION "  menu      EXIT: play");
  sprintf(l[0], "1 picture: %s", cfg.size == SIZE_BIG ? "big (1.5x)" : "small (1x, faster)");
  sprintf(l[1], "2 greys: %s", cfg.greys == GREYS_DOTS ? "4 (dots)" : "2 (two-tone)");
  sprintf(l[2], "3 invert: %s", cfg.invert ? "on" : "off");
  sprintf(l[3], "4 sound: %s", cfg.sound ? "on" : "off");
  sprintf(l[4], "5 speed shown: %s", cfg.speed ? "on" : "off");
  sprintf(l[5], "6 key test  7 setup (USB disk)  8 about");
  text(4, 25, "Cartridges in " DIR ":  F1 .GB   F2 .GBC", 0);
  for (int i = 0; i < 6; i++) text(4, 48 + i * 21, l[i], 0);
  const char *m = msg[0] ? msg : loaded ? G.title : "No cartridge: F1 or F2 to load one";
  text(4, 48 + 6 * 21 + 3, m, 0);
  static const char *K[6] = { "GB", "GBC", "SAVEST", "LOADST", "RESET", "OFF" };
  lcd_draw_menu_keys(K);
  lcd_refresh();
  lcd_refresh_wait();
}

/* returns 1 to play, 0 to turn off */
static int menu(void) {
  lcd_refresh_wait();
  stop_buzzer();
  if (sav_write()) strcpy(msg, "game saved (.SAV)");
  rom_file_close();
  wait_release();
  menu_draw();
  for (;;) {
    if (key_empty()) { sys_timer_start(0, 200); CLR_ST(STAT_RUNNING); sys_sleep(); }
    if (ST(STAT_PGM_END) || ST(STAT_SUSPENDED)) return 0;
    int k = key_pop();
    if (k <= 0 || k == KEY_DOUBLE_RELEASE) continue;
    switch (k) {
    case KEY_EXIT: case KEY_ENTER:
      if (!loaded) { strcpy(msg, "No cartridge: F1 or F2 to load one"); break; }
      cfg_save(); wait_release(); msg[0] = 0; return 1;
    case KEY_F1: case KEY_F2:
      if (load(k - KEY_F1)) { cfg_save(); wait_release(); msg[0] = 0; return 1; }   /* loaded: play */
      break;
    case KEY_F3: state_save(); break;
    case KEY_F4: if (state_load()) { wait_release(); return 1; } break;
    case KEY_F5: if (loaded) { gb_reset_game(); strcpy(msg, "reset"); } break;
    case KEY_F6: cfg_save(); return 0;
    case KEY_1: cfg.size = (cfg.size + 1) % SIZE_CNT; break;
    case KEY_2: cfg.greys = (cfg.greys + 1) % GREYS_CNT; break;
    case KEY_3: cfg.invert ^= 1; break;
    case KEY_4: cfg.sound ^= 1; break;
    case KEY_5: cfg.speed ^= 1; break;
    case KEY_6: keytest(); break;
    case KEY_7: wait_release(); handle_menu(&SETUP_MENU, MENU_RESET, 0); wait_release(); break;
    case KEY_8: about(); break;
    default: continue;
    }
    menu_draw();
  }
}

/* ------------------------------------------------------------------ sound: the tune's note on the buzzer */
static uint32_t tone;

static void sound(int turbo) {
  uint32_t f = sound_tone();
  if (!cfg.sound || turbo || ST(STAT_BEEP_MUTE) || f < 60 || f > 9000) {
    if (tone) { stop_buzzer(); tone = 0; }
    return;
  }
  uint32_t d = f > tone ? f - tone : tone - f;
  if (!tone || d * 64 > tone) { start_buzzer_freq(f * 1000); tone = f; }   /* a new note (or 1.5% off) */
}

/* ------------------------------------------------------------------ screenshot: the LCD as a .bmp in /SCREENS (DMCP) */
static void screenshot(void) {
  lcd_refresh_wait();
  status_now[0] = 0;
  int r = create_screenshot(1);
  set_note(r < 0 ? "screenshot NOT saved" : "screenshot saved in /SCREENS");
}

/* ------------------------------------------------------------------ MBC3's clock (Pokemon Gold, Silver) */
static void clock_tick(void) {
  tm_t t; dt_t d;
  rtc_read(&t, &d);
  gb_rtc(t.sec, t.min, t.hour, julian_day(&d) & 511);
}

/* ------------------------------------------------------------------ power, as the DMCP SDK's template */
static void power_off(void) {                    /* the Game Boy keeps its state while off */
  lcd_refresh_wait();
  stop_buzzer(); tone = 0;
  sav_write();
  rom_file_close();                              /* reopened on the next read: the USB disk may change it */
  SET_ST(STAT_PGM_END);
  if (!ST(STAT_SUSPENDED)) {
    lcd_set_buf_cleared(0);
    draw_power_off_image(1);
    LCD_power_off(0);
    SET_ST(STAT_SUSPENDED);
    SET_ST(STAT_OFF);
  }
  while (ST(STAT_PGM_END) && ST(STAT_SUSPENDED)) { CLR_ST(STAT_RUNNING); sys_sleep(); }   /* ON clears them */
  SET_ST(STAT_RUNNING);
  if (ST(STAT_OFF)) { LCD_power_on(); rtc_wakeup_delay(); CLR_ST(STAT_OFF); }
  key_pop_all();
  frame_ui();
}

/* ------------------------------------------------------------------ the main loop */
void _exit(int code) { (void)code; sys_reset(); for (;;) {} }

static void play_menu(void) {                    /* the menu until a game plays; off when asked */
  for (;;) {
    if (menu()) { frame_ui(); return; }
    power_off();
    if (loaded) return;
  }
}

void program_main(void) {
  switch_usb_powered_freq();
  int s = screen_init();
  if (s == -1) {
    lcd_clear_buf();
    text(4, 40, "GB47: not enough memory", 0);
    lcd_refresh();
    for (;;) wait_for_key_press();
  }
  run_menu_item_app = run_menu_item;
  menu_line_str_app = menu_line_str;
  cfg_load();
  if (cfg.last[0]) {                             /* the last game again, its saves read */
    const char *n = strrchr(cfg.last, '/');
    char path[64];
    strcpy(path, cfg.last);
    if (!file_exists(path) || !rom_load(path, n ? n + 1 : path)) msg[0] = 0;
  }
  if (s) strcpy(msg, "LCD layout not learnt: slow drawing");
  if (!loaded) play_menu();
  else frame_ui();

  uint32_t now_ms = sys_current_ms();
  uint32_t next = now_ms * 1000u, lcd_free = next;      /* in us */
  uint32_t sec = now_ms, last_auto = now_ms;
  int frames_sec = 0, skipped = 0, prev1 = 0, prev2 = 0, shown = 0;
  uint32_t ms_emu = 0, ms_scr = 0;
  for (;;) {
    if (ST(STAT_PGM_END) || ST(STAT_SUSPENDED)) { power_off(); next = sys_current_ms() * 1000u; }
    if (ST(STAT_POWER_CHANGE)) { switch_usb_powered_freq(); CLR_ST(STAT_POWER_CHANGE); }
    SET_ST(STAT_RUNNING);

    int k1, k2;
    read_keys(&k1, &k2);
    int shot = 0;                                /* the keys go by read_key; DMCP's queue is emptied, */
    for (int q = key_pop(); q >= 0; q = key_pop())   /* but its screenshot key is kept */
      if (q == KEY_SCREENSHOT) shot = 1;
    if (k1 == KEY_EXIT || k2 == KEY_EXIT) {
      play_menu();
      next = lcd_free = sys_current_ms() * 1000u;
      prev1 = prev2 = KEY_EXIT;
      continue;
    }
    #define PRESSED(k) ((k1 == (k) || k2 == (k)) && prev1 != (k) && prev2 != (k))
    int turbo = k1 == KEY_F1 || k2 == KEY_F1;
    if (PRESSED(KEY_SHIFT)) block ^= 1;
    if (PRESSED(KEY_F2)) { cfg.sound ^= 1; set_note(cfg.sound ? "sound on" : "sound off"); }
    if (PRESSED(KEY_F3)) { cfg.greys = (cfg.greys + 1) % GREYS_CNT; set_note(cfg.greys ? "two-tone" : "4 greys (dots)"); }
    if (PRESSED(KEY_F4)) { cfg.size = (cfg.size + 1) % SIZE_CNT; frame_ui(); }
    if (PRESSED(KEY_F5)) shot = 1;
    if (PRESSED(KEY_F6)) cfg.speed ^= 1;
    prev1 = k1; prev2 = k2;
    if (shot) { screenshot(); next = sys_current_ms() * 1000u; }
    uint8_t joy = keys_joy(k1, k2);
    if (block) joy |= JOY_B;

    /* draw this frame? when the LCD will have sent the last one by its end; when behind, every few frames */
    uint32_t now = sys_current_ms() * 1000u;
    int behind = (int32_t)(now - next) > GB_FRAME_US;
    int draw = turbo ? skipped >= 8 : (int32_t)(now + GB_FRAME_US - lcd_free) >= 0 && (!behind || skipped >= MAX_SKIP);

    uint32_t e0 = sys_current_ms();
    gb_frame(draw, joy);
    ms_emu += sys_current_ms() - e0;
    sound(turbo);
    if (G.frames % 60 == 0) clock_tick();

    if (draw && G.drawn) {
      uint32_t r0 = sys_current_ms();
      G.drawn = 0;
      int lines = screen_render(G.fb, cfg.greys, cfg.invert);
      ms_scr += sys_current_ms() - r0;
      lcd_free = sys_current_ms() * 1000u + (uint32_t)lines * LINE_US;
      if (lines) shown++;
      skipped = 0;
    } else skipped++;
    frames_sec++;

    if (G.fetch_error || G.error) {              /* the ROM file went away, or the game crashed the core */
      snprintf(msg, sizeof msg, G.error ? "the game stopped (error %d): reset or load" : "cannot read the ROM file", G.error);
      G.fetch_error = 0;
      play_menu();
      next = lcd_free = sys_current_ms() * 1000u;
      continue;
    }
    if (G.ram_dirty && G.frames - G.ram_write_frame > 120) {   /* the game saved: to the disk after 2 s quiet */
      if (sav_write()) set_note("game saved (.SAV)");
      next = sys_current_ms() * 1000u;
    }

    now_ms = sys_current_ms();
    if (now_ms - sec >= 1000) {
      pct = (int)((uint32_t)frames_sec * GB_FRAME_US / 10u / (now_ms - sec));
      emu_ms = frames_sec ? (int)(ms_emu * 10 / (uint32_t)frames_sec) : 0;   /* tenths of a ms a frame */
      scr_ms = shown ? (int)(ms_scr * 10 / (uint32_t)shown) : 0;
      fps = shown; shown = 0; frames_sec = 0; sec = now_ms; ms_emu = ms_scr = 0;
    }
    status(turbo);
    if (now_ms - last_auto > 10000) { reset_auto_off(); last_auto = now_ms; }

    next += GB_FRAME_US;                         /* 59.7 frames a second, as the Game Boy */
    now = now_ms * 1000u;
    if ((int32_t)(now - next) > 200000) next = now;       /* far behind: do not try to catch up */
    if (turbo) next = now;
    else while ((int32_t)(next - sys_current_ms() * 1000u) > 0) {}
  }
}
