/* gb.h - GB47: the Game Boy around Peanut-GB (third_party/peanut_gb): the cartridge's ROM (all of it in memory, or
 * read from the disk 4 KB at a time into a cache), its RAM, the picture as 2-bit pixels, save states.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#ifndef GB47_GB_H
#define GB47_GB_H
#include <stdint.h>

#define GB_W 160
#define GB_H 144
#define GB_ROW 40                       /* bytes a picture row: 4 pixels a byte, the leftmost in bits 7-6 */
#define GB_PAGE 4096u                   /* the ROM cache's piece */
#define GB_FRAME_US 16742               /* a Game Boy frame: 70224 cycles at 4.194304 MHz */

enum { JOY_A = 0x01, JOY_B = 0x02, JOY_SELECT = 0x04, JOY_START = 0x08,
       JOY_RIGHT = 0x10, JOY_LEFT = 0x20, JOY_UP = 0x40, JOY_DOWN = 0x80 };

typedef struct {
  uint8_t *fb;                          /* GB_H rows of GB_ROW bytes: the last picture drawn */
  uint32_t frames;                      /* frames run */
  uint32_t drawn;                       /* fb changed since the screen last took it */
  uint8_t *ram;                         /* the cartridge's RAM (battery-backed in most), ram_size bytes */
  uint32_t ram_size;
  int ram_dirty;                        /* written since the last save */
  uint32_t ram_write_frame;             /* the frame of the last write */
  char title[17];                       /* from the cartridge's header */
  uint32_t rom_size;
  int mbc, cgb;                         /* the MBC type (0 1 2 3 5), cgb: 1 dual, 2 Color only */
  int paged;                            /* the ROM is read from the disk as needed */
  int slots;                            /* the cache's 4 KB slots (paged) */
  uint32_t misses;                      /* pieces read into the cache */
  int fetch_error;                      /* a piece could not be read */
  int error;                            /* Peanut-GB stopped: an invalid opcode ... (gb_error) */
} Gb;

extern Gb G;

/* the cartridge: rom_size bytes; all in memory (rom), or paged: cache (slots x GB_PAGE bytes) filled by
   fetch(page, dst), page = the ROM offset / GB_PAGE. 0 ok, else an error (gb_error_text). */
int  gb_open(const uint8_t *rom, uint32_t rom_size);
int  gb_open_paged(uint32_t rom_size, uint8_t *cache, int slots, int (*fetch)(uint32_t page, uint8_t *dst));
const char *gb_error_text(int e);
void gb_close(void);                    /* frees all but the resident ROM and the cache (the caller's) */
void gb_reset_game(void);
void gb_frame(int draw, uint8_t joy);   /* one frame (1/59.7 s); draw: the picture goes to fb */
void gb_rtc(int sec, int min, int hour, int day);   /* MBC3's clock, from the calculator's */
void gb_flush_cache(void);              /* forget the cached pieces (the ROM file was closed) */
int  gb_header_ok(const uint8_t *h, uint32_t n);    /* a Game Boy header in the first n bytes (n >= 0x150) */

/* save states: the whole machine and the cartridge RAM */
uint32_t gb_state_size(void);
void gb_state_save(uint8_t *dst);
int  gb_state_load(const uint8_t *src, uint32_t n);  /* 0 ok, -1 not this game's or another GB47's */

#ifdef GB47_TEST                        /* the tests' view */
struct gb_s;
struct gb_s *gb_core(void);
void gb_serial(void (*tx)(struct gb_s *, uint8_t));
extern void (*gb_test_line)(const uint8_t *px, int line);   /* the raw pixels of each line drawn */
#endif
#endif
