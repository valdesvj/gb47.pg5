/* keys.c - GB47: the DM42n's keys as the Game Boy's buttons.
 *
 *     7 8 9        the digit block is the pad: 8 up, 4 left, 6 right, 5 and 2 down,
 *     4 5 6        7 9 1 3 the diagonals (one key for two directions); the arrows up and down too
 *     1 2 3
 *   x  A    -  B    / A and B together    + B
 *   ENTER and R/S  START     <- and .  SELECT
 * The calculator reads two keys at once: a direction and a button. / is one key for A+B, so a direction + / is
 * Mario's running jump; SHIFT latches B down (main.c) for games that run with B held.
 * EXIT (menu), SHIFT and F1..F6 are GB47's (main.c).
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <main.h>
#include <dmcp.h>
#include "gb.h"
#include "keys.h"

static uint8_t one(int k) {
  switch (k) {
  case KEY_8: case KEY_UP: return JOY_UP;
  case KEY_2: case KEY_5: case KEY_DOWN: return JOY_DOWN;
  case KEY_4: return JOY_LEFT;
  case KEY_6: return JOY_RIGHT;
  case KEY_7: return JOY_UP | JOY_LEFT;
  case KEY_9: return JOY_UP | JOY_RIGHT;
  case KEY_1: return JOY_DOWN | JOY_LEFT;
  case KEY_3: return JOY_DOWN | JOY_RIGHT;
  case KEY_MUL: return JOY_A;
  case KEY_SUB: case KEY_ADD: return JOY_B;
  case KEY_DIV: return JOY_A | JOY_B;
  case KEY_ENTER: case KEY_RUN: return JOY_START;
  case KEY_BSP: case KEY_DOT: return JOY_SELECT;
  default: return 0;
  }
}

int keys_is_pad(int k) { return one(k) != 0; }

uint8_t keys_joy(int k1, int k2) {
  uint8_t j = (uint8_t)(one(k1) | one(k2));
  if ((j & JOY_UP) && (j & JOY_DOWN)) j &= (uint8_t)~(JOY_UP | JOY_DOWN);       /* no game expects both */
  if ((j & JOY_LEFT) && (j & JOY_RIGHT)) j &= (uint8_t)~(JOY_LEFT | JOY_RIGHT);
  return j;
}
