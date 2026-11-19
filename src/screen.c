/* screen.c - GB47: the Game Boy's picture on the DM42n's LCD. See screen.h.
 *
 * A Game Boy row is 40 bytes of 4 pixels (2 bits each). A table turns each byte into its LCD pixels at once: 6 for
 * big (pixels 0 and 2 drawn twice as wide), 4 for small, already as the grey's dots for that LCD line's parity;
 * the dots are fixed to the LCD's pixels (x and y even or odd), so a grey area keeps one even pattern however the
 * game scrolls. Big draws Game Boy rows 0 1 2 3 ... on LCD rows 0+1, 2, 3+4, 5 ...
 * Only the LCD lines whose Game Boy row changed are sent (FPS47: about 0.22 ms a line, a whole big picture 48 ms).
 * The LCD buffer's layout is learnt at start by setting single pixels (as FPS47 and ZX47); when each group of 8
 * pixels lands in one byte, a 256-entry table converts a byte at a time.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <main.h>
#include <dmcp.h>
#include <stdlib.h>
#include <string.h>
#include "gb.h"
#include "screen.h"

static int size = SIZE_BIG, X0 = 80, Y0 = 24, W = 240, H = 216;

static uint8_t *last;                   /* the Game Boy rows on the LCD (heap, 5.8 KB) */
static uint8_t *dirty;                  /* LCD_Y flags: this LCD line changed */
static int valid, last_greys = -1, last_inv = -1;

static uint8_t xbyte[LCD_X], xbit[LCD_X];   /* pixel -> byte and bit mask in lcd_line_addr() */
static int ink;                             /* the bit value of a black pixel */
static int fast;                            /* 8 pixels -> one byte everywhere, the same bit order */
static uint8_t lut[256];                    /* fast: 8 pixels (bit 7 = left, 1 = black) -> the LCD byte */
static uint8_t L6[2][256];                  /* big: a byte of 4 Game Boy pixels -> 6 LCD pixels (bit 5 left) */
static uint8_t L4[2][256];                  /* small: -> 4 LCD pixels (bit 3 left) */

/* ------------------------------------------------------------------ the LCD buffer's layout (as ZX47) */
static int find_bit(int x, int y, int *byte, int *bit, int *val) {
  uint8_t a[LCD_LINE_BUF_SIZE], b[LCD_LINE_BUF_SIZE];
  lcd_fill_rect((uint32_t)x, (uint32_t)y, 1, 1, 1);
  memcpy(a, lcd_line_addr(y), sizeof a);
  lcd_fill_rect((uint32_t)x, (uint32_t)y, 1, 1, 0);
  memcpy(b, lcd_line_addr(y), sizeof b);
  for (int i = 0; i < (int)sizeof a; i++) {
    uint8_t d = (uint8_t)(a[i] ^ b[i]);
    if (!d) continue;
    for (int k = 0; k < 8; k++)
      if (d == (1u << k)) { *byte = i; *bit = k; *val = (a[i] >> k) & 1; return 1; }
    return 0;
  }
  return 0;
}

static int learn(void) {
  int y = 30, byte, bit, val;
  for (int x = 0; x < LCD_X; x++) {
    if (!find_bit(x, y, &byte, &bit, &val)) return 0;
    xbyte[x] = (uint8_t)byte;
    xbit[x] = (uint8_t)(1u << bit);
    if (x == 0) ink = val;
    else if (val != ink) return 0;
  }
  fast = 1;                                         /* each group of 8 in one byte, the same order? */
  for (int g = 0; g < LCD_X / 8 && fast; g++)
    for (int j = 0; j < 8; j++)
      if (xbyte[g * 8 + j] != xbyte[g * 8] || xbit[g * 8 + j] != xbit[j]) fast = 0;
  if (fast)
    for (int v = 0; v < 256; v++) {
      uint8_t o = 0;
      for (int j = 0; j < 8; j++) if ((v >> (7 - j) & 1) == ink) o |= xbit[j];
      lut[v] = o;
    }
  return 1;
}

/* ------------------------------------------------------------------ greys to dots */
static int dot(int shade, int greys, int xo, int yo) {   /* 1 = black at an LCD pixel of parity (xo, yo) */
  if (greys == GREYS_TWO) return shade >= 2;
  switch (shade) {
  case 0: return 0;
  case 1: return !xo && !yo;                        /* light grey: 1 dot in 4 */
  case 2: return xo == yo;                          /* dark grey: a checkerboard */
  default: return 1;
  }
}

static void make_tables(int greys, int invert) {
  static const int COL6[6] = { 0, 0, 1, 2, 2, 3 };  /* the Game Boy pixel of each of the 6 LCD pixels */
  for (int yo = 0; yo < 2; yo++)
    for (int b = 0; b < 256; b++) {
      uint8_t o6 = 0, o4 = 0;
      for (int c = 0; c < 6; c++) {
        int sh = b >> (6 - 2 * COL6[c]) & 3;
        if (dot(sh, greys, c & 1, yo) ^ invert) o6 |= (uint8_t)(0x20 >> c);
      }
      for (int c = 0; c < 4; c++) {
        int sh = b >> (6 - 2 * c) & 3;
        if (dot(sh, greys, c & 1, yo) ^ invert) o4 |= (uint8_t)(0x08 >> c);
      }
      L6[yo][b] = o6; L4[yo][b] = o4;
    }
  last_greys = greys; last_inv = invert;
}

/* ------------------------------------------------------------------ setup */
int screen_init(void) {
  last = malloc(GB_H * GB_ROW);
  dirty = malloc(LCD_Y);
  if (!last || !dirty) return -1;
  make_tables(GREYS_DOTS, 0);
  valid = 0;
  if (!learn()) { fast = 0; ink = 0; memset(xbyte, 0, sizeof xbyte); memset(xbit, 0, sizeof xbit); return -2; }
  return 0;
}

int screen_fast(void) { return fast; }
void screen_invalidate(void) { valid = 0; }
int screen_x(void) { return X0; }
int screen_y(void) { return Y0; }
int screen_w(void) { return W; }
int screen_h(void) { return H; }

void screen_layout(int s) {
  size = s;
  if (s == SIZE_SMALL) { X0 = 120; Y0 = 60; W = GB_W; H = GB_H; }
  else { X0 = 80; Y0 = 24; W = 240; H = 216; }
  valid = 0;
}

/* ------------------------------------------------------------------ a frame */
static void put_line(int y, const uint8_t *m, int nbytes) {   /* m: nbytes of pixels from X0, bit 7 left */
  uint8_t *p = lcd_line_addr(y);
  if (fast) { for (int g = 0, b = X0 / 8; g < nbytes; g++, b++) p[xbyte[b * 8]] = lut[m[g]]; return; }
  for (int i = 0; i < nbytes * 8; i++) {
    int x = X0 + i, black = m[i >> 3] >> (7 - (i & 7)) & 1;
    if (black == ink) p[xbyte[x]] |= xbit[x]; else p[xbyte[x]] &= (uint8_t)~xbit[x];
  }
}

static void line_big(int y, const uint8_t *row) {             /* 40 bytes -> 240 pixels -> 30 bytes */
  const uint8_t *t = L6[y & 1];
  uint8_t m[30];
  for (int g = 0, o = 0; g < GB_ROW; g += 4, o += 3) {
    uint32_t v = (uint32_t)t[row[g]] << 18 | (uint32_t)t[row[g + 1]] << 12 | (uint32_t)t[row[g + 2]] << 6 | t[row[g + 3]];
    m[o] = (uint8_t)(v >> 16); m[o + 1] = (uint8_t)(v >> 8); m[o + 2] = (uint8_t)v;
  }
  put_line(y, m, 30);
}

static void line_small(int y, const uint8_t *row) {           /* 40 bytes -> 160 pixels -> 20 bytes */
  const uint8_t *t = L4[y & 1];
  uint8_t m[20];
  for (int g = 0; g < GB_ROW; g += 2) m[g / 2] = (uint8_t)(t[row[g]] << 4 | t[row[g + 1]]);
  put_line(y, m, 20);
}

int screen_render(const uint8_t *fb, int greys, int invert) {
  if (!last || !fb) return 0;
  if (greys != last_greys || invert != last_inv) { make_tables(greys, invert); valid = 0; }
  lcd_refresh_wait();                               /* the last frame's lines are out before the buffer changes */
  memset(dirty, 0, LCD_Y);
  int any = 0;
  for (int r = 0; r < GB_H; r++) {
    const uint8_t *row = fb + r * GB_ROW;
    if (valid && !memcmp(row, last + r * GB_ROW, GB_ROW)) continue;
    memcpy(last + r * GB_ROW, row, GB_ROW);
    if (size == SIZE_SMALL) { line_small(Y0 + r, row); dirty[Y0 + r] = 1; any++; continue; }
    int y = Y0 + (r >> 1) * 3 + (r & 1) * 2;        /* even rows: 2 LCD lines, odd: 1 */
    line_big(y, row); dirty[y] = 1; any++;
    if (!(r & 1)) { line_big(y + 1, row); dirty[y + 1] = 1; any++; }
  }
  valid = 1;
  if (!any) return 0;
  /* send the changed bands; bands closer than 6 lines go as one; the last one is not waited for */
  int sent = 0, lines = 0;
  for (int y = Y0; y < Y0 + H;) {
    if (!dirty[y]) { y++; continue; }
    int s = y, e = y + 1, gap = 0;
    for (int k = e; k < Y0 + H && gap < 6; k++) { if (dirty[k]) { e = k + 1; gap = 0; } else gap++; }
    if (sent++) lcd_refresh_wait();
    lcd_refresh_lines(s, e - s);
    lines += e - s;
    y = e;
  }
  return lines;
}
