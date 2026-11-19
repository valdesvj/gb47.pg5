/* keys.h - GB47: the DM42n's keys as the Game Boy's buttons. Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#ifndef GB47_KEYS_H
#define GB47_KEYS_H
#include <stdint.h>

uint8_t keys_joy(int k1, int k2);       /* the two keys held -> the Game Boy's buttons (gb.h JOY_*) */
int keys_is_pad(int k);                 /* the key is one of the buttons' */
#endif
