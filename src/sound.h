/* sound.h - GB47: the Game Boy's sound registers, and the one tone the calculator's buzzer can play.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#ifndef GB47_SOUND_H
#define GB47_SOUND_H
#include <stdint.h>

uint8_t audio_read(uint16_t addr);     /* Peanut-GB's hooks: 0xFF10..0xFF3F */
void audio_write(uint16_t addr, uint8_t val);

void sound_reset(void);
void sound_frame(void);                /* a Game Boy frame passed: lengths, envelopes, sweep */
uint32_t sound_tone(void);             /* the tone to play now, in Hz (0: silence) */
int  sound_channel(void);              /* the channel it comes from (1..3, 0 none) */

/* the state, for save states */
uint32_t sound_state_size(void);
void sound_state_save(uint8_t *dst);
void sound_state_load(const uint8_t *src);
#endif
