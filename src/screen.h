/* screen.h - GB47: the Game Boy's 160x144 picture on the DM42n's 400x240 LCD.
 *   big    1.5x: 240x216 at (80, 24), every 2 Game Boy pixels as 3 (FPS47's "GB lines")
 *   small  1x:   160x144 at (120, 60), fewer lines to send: faster on scrolling games
 * The four greys as dot patterns (white, 1 dot in 4, 1 in 2, black), or two-tone (the two darker greys black).
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#ifndef GB47_SCREEN_H
#define GB47_SCREEN_H
#include <stdint.h>

enum { SIZE_BIG, SIZE_SMALL, SIZE_CNT };
enum { GREYS_DOTS, GREYS_TWO, GREYS_CNT };

int  screen_init(void);                 /* learns the LCD buffer's layout; 0 ok, -1 no memory, -2 slow path */
int  screen_fast(void);                 /* 1: 8 pixels a byte straight from a table */
void screen_layout(int size);           /* sets the picture's place; the next render sends every line */
void screen_invalidate(void);
int  screen_x(void), screen_y(void), screen_w(void), screen_h(void);
/* the picture to the LCD (fb: GB_H rows of GB_ROW bytes, 2 bits a pixel); returns the LCD lines sent */
int  screen_render(const uint8_t *fb, int greys, int invert);
#endif
