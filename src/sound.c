/* sound.c - GB47: the Game Boy's sound registers (0xFF10..0xFF3F), kept as the real APU keeps them (the bits a game
 * reads back, NR52's channel flags, lengths, volume envelopes, channel 1's sweep), and the one tone the DM42n's
 * buzzer plays from them.
 *
 * The buzzer has one voice and no volume, so of the Game Boy's four channels it plays the tune: the louder of the
 * two square-wave channels (1, 2), the higher one when both are as loud (the melody is usually above the harmony);
 * the wave channel (3, the bass in most games) only when both are silent; the noise channel (4, drums) never.
 * The frame sequencer (512 Hz) is stepped a frame at a time: 8.57 steps a frame.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <string.h>
#include "sound.h"

typedef struct {
  uint8_t on, vol, env_timer, sweep_timer, sweep_on, pad;
  uint16_t length, freq, shadow;
} Ch;

static struct {
  uint8_t reg[0x30];                    /* 0xFF10..0xFF3F as written */
  Ch ch[4];
  uint32_t seq_cycles;                  /* cycles towards the next frame-sequencer step */
  uint8_t step;
} S;

#define R(a) S.reg[(a) - 0xFF10]
static const uint8_t OR_MASK[0x17] = {  /* the bits that read back as 1, 0xFF10..0xFF26 (Pan Docs) */
  0x80, 0x3F, 0x00, 0xFF, 0xBF, 0xFF, 0x3F, 0x00, 0xFF, 0xBF, 0x7F, 0xFF, 0x9F, 0xFF, 0xBF,
  0xFF, 0xFF, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x70 };

static int power(void) { return R(0xFF26) & 0x80; }

void sound_reset(void) {
  memset(&S, 0, sizeof S);
  static const uint8_t init[0x17] = {   /* the registers after the boot ROM */
    0x80, 0xBF, 0xF3, 0xFF, 0xBF, 0xFF, 0x3F, 0x00, 0xFF, 0xBF, 0x7F, 0xFF, 0x9F, 0xFF, 0xBF,
    0xFF, 0xFF, 0x00, 0x00, 0xBF, 0x77, 0xF3, 0xF1 };
  memcpy(S.reg, init, sizeof init);
  S.ch[0].on = 1;                       /* the boot ROM's ding leaves channel 1 on, silent */
}

uint8_t audio_read(uint16_t a) {
  if (a < 0xFF10 || a > 0xFF3F) return 0xFF;
  if (a >= 0xFF30) return R(a);         /* wave RAM */
  if (a > 0xFF26) return 0xFF;
  if (a == 0xFF26) {
    uint8_t v = (uint8_t)(R(a) & 0x80) | 0x70;
    for (int i = 0; i < 4; i++) if (S.ch[i].on) v |= (uint8_t)(1u << i);
    return v;
  }
  return R(a) | OR_MASK[a - 0xFF10];
}

static int dac_on(int c) {
  if (c == 2) return R(0xFF1A) & 0x80;
  static const uint16_t NRX2[4] = { 0xFF12, 0xFF17, 0, 0xFF21 };
  return R(NRX2[c]) & 0xF8;
}

static uint16_t sweep_calc(Ch *c) {
  uint8_t r = R(0xFF10);
  uint16_t d = c->shadow >> (r & 7);
  uint16_t f = r & 8 ? (uint16_t)(c->shadow - d) : (uint16_t)(c->shadow + d);
  if (f > 2047) c->on = 0;
  return f;
}

static void trigger(int n) {
  Ch *c = &S.ch[n];
  static const uint16_t NRX2[4] = { 0xFF12, 0xFF17, 0, 0xFF21 };
  c->on = dac_on(n) ? 1 : 0;
  if (!c->length) c->length = n == 2 ? 256 : 64;
  if (n != 2) {
    uint8_t e = R(NRX2[n]);
    c->vol = e >> 4;
    c->env_timer = e & 7;
  }
  if (n == 0) {
    uint8_t r = R(0xFF10);
    c->shadow = c->freq;
    c->sweep_timer = (r >> 4) & 7 ? (r >> 4) & 7 : 8;
    c->sweep_on = (r & 0x77) != 0;
    if (r & 7) sweep_calc(c);
  }
}

void audio_write(uint16_t a, uint8_t v) {
  if (a < 0xFF10 || a > 0xFF3F) return;
  if (a >= 0xFF30) { R(a) = v; return; }
  if (a == 0xFF26) {
    if (!(v & 0x80) && power()) {       /* off: every register cleared, every channel stops */
      memset(S.reg, 0, 0x16);
      for (int i = 0; i < 4; i++) S.ch[i].on = 0;
    }
    R(a) = v & 0x80;
    return;
  }
  if (!power() || a > 0xFF26) return;
  R(a) = v;
  switch (a) {
  case 0xFF11: S.ch[0].length = (uint16_t)(64 - (v & 63)); break;
  case 0xFF16: S.ch[1].length = (uint16_t)(64 - (v & 63)); break;
  case 0xFF1B: S.ch[2].length = (uint16_t)(256 - v); break;
  case 0xFF20: S.ch[3].length = (uint16_t)(64 - (v & 63)); break;
  case 0xFF12: if (!dac_on(0)) S.ch[0].on = 0; break;
  case 0xFF17: if (!dac_on(1)) S.ch[1].on = 0; break;
  case 0xFF1A: if (!dac_on(2)) S.ch[2].on = 0; break;
  case 0xFF21: if (!dac_on(3)) S.ch[3].on = 0; break;
  case 0xFF13: S.ch[0].freq = (uint16_t)((S.ch[0].freq & 0x700) | v); break;
  case 0xFF18: S.ch[1].freq = (uint16_t)((S.ch[1].freq & 0x700) | v); break;
  case 0xFF1D: S.ch[2].freq = (uint16_t)((S.ch[2].freq & 0x700) | v); break;
  case 0xFF14: case 0xFF19: case 0xFF1E: case 0xFF23: {
    int n = a == 0xFF14 ? 0 : a == 0xFF19 ? 1 : a == 0xFF1E ? 2 : 3;
    if (n < 3) S.ch[n].freq = (uint16_t)((S.ch[n].freq & 0xFF) | (v & 7) << 8);
    if (v & 0x80) trigger(n);
    break;
  }
  default: break;
  }
}

/* ------------------------------------------------------------------ the frame sequencer */
static void step_length(void) {
  static const uint16_t NRX4[4] = { 0xFF14, 0xFF19, 0xFF1E, 0xFF23 };
  for (int i = 0; i < 4; i++) {
    Ch *c = &S.ch[i];
    if ((R(NRX4[i]) & 0x40) && c->length && !--c->length) c->on = 0;
  }
}

static void step_sweep(void) {
  Ch *c = &S.ch[0];
  if (!c->sweep_timer || --c->sweep_timer) return;
  uint8_t r = R(0xFF10);
  c->sweep_timer = (r >> 4) & 7 ? (r >> 4) & 7 : 8;
  if (!c->sweep_on || !((r >> 4) & 7)) return;
  uint16_t f = sweep_calc(c);
  if (f <= 2047 && (r & 7)) {
    c->shadow = c->freq = f;
    R(0xFF13) = (uint8_t)f;
    R(0xFF14) = (uint8_t)((R(0xFF14) & 0xF8) | (f >> 8));
    sweep_calc(c);
  }
}

static void step_env(void) {
  static const uint16_t NRX2[4] = { 0xFF12, 0xFF17, 0, 0xFF21 };
  for (int i = 0; i < 4; i++) {
    if (i == 2) continue;
    Ch *c = &S.ch[i];
    uint8_t e = R(NRX2[i]);
    if (!(e & 7) || !c->env_timer || --c->env_timer) continue;
    c->env_timer = e & 7;
    if (e & 8) { if (c->vol < 15) c->vol++; }
    else if (c->vol) c->vol--;
  }
}

void sound_frame(void) {
  if (!power()) return;
  S.seq_cycles += 70224;
  while (S.seq_cycles >= 8192) {
    S.seq_cycles -= 8192;
    uint8_t s = S.step++ & 7;
    if (!(s & 1)) step_length();
    if (s == 2 || s == 6) step_sweep();
    if (s == 7) step_env();
  }
}

/* ------------------------------------------------------------------ the buzzer's tone */
static int heard(int n) {                /* on, and sent to a speaker (NR51) */
  return S.ch[n].on && (R(0xFF25) & (0x11u << n));
}

static int chosen;

uint32_t sound_tone(void) {
  chosen = 0;
  if (!power()) return 0;
  int best = -1, bv = 0;
  uint32_t bf = 0;
  for (int n = 0; n < 2; n++) {
    if (!heard(n) || !S.ch[n].vol) continue;
    uint32_t f = 131072u / (2048u - S.ch[n].freq);
    if (S.ch[n].vol > bv || (S.ch[n].vol == bv && f > bf)) { best = n; bv = S.ch[n].vol; bf = f; }
  }
  if (best < 0 && heard(2) && dac_on(2) && (R(0xFF1C) & 0x60)) { best = 2; bf = 65536u / (2048u - S.ch[2].freq); }
  if (best < 0) return 0;
  chosen = best + 1;
  return bf;
}

int sound_channel(void) { return chosen; }

uint32_t sound_state_size(void) { return sizeof S; }
void sound_state_save(uint8_t *d) { memcpy(d, &S, sizeof S); }
void sound_state_load(const uint8_t *s) { memcpy(&S, s, sizeof S); }
