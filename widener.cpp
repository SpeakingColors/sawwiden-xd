/*
 * SawWiden : mono-compatible stereo widener / ensemble for KORG minilogue xd (ModFX)
 * Author: SpeakingColors
 *
 * Mid (the dry centre) is never touched. A "side" signal is built from two
 * delay taps modulated by 3-phase LFOs (string-ensemble style), high-passed so
 * the bass stays centred, then added to L and subtracted from R.
 * Summing to mono cancels the side exactly: no phasing, centre always kept.
 *
 *   TIME  : movement speed (slow drift to lively shimmer)
 *   DEPTH : width (0 = off / dry, max = very wide)
 */

#include "usermodfx.h"

#define DLY_SIZE 2048            // ~42 ms at 48 kHz, power of two
#define DLY_MASK (DLY_SIZE - 1)

static float s_line[DLY_SIZE] __sdram;
static uint32_t s_wpos;
static float s_lfo_phase;
static float s_rate, s_width;      // smoothed targets
static float s_rate_t, s_width_t;
static float s_hp_x1, s_hp_y1;

static inline float lfo_tri(float ph) {   // bipolar triangle, ph in [0,1)
  const float t = 4.f * ph;
  return (t < 1.f) ? t : (t < 3.f) ? (2.f - t) : (t - 4.f);
}

static inline float read_frac(float delay_samples) {
  float rp = (float)s_wpos - delay_samples;
  while (rp < 0.f) rp += (float)DLY_SIZE;
  const uint32_t i0 = (uint32_t)rp;
  const float fr = rp - (float)i0;
  const float a = s_line[i0 & DLY_MASK];
  const float b = s_line[(i0 + 1) & DLY_MASK];
  return a + fr * (b - a);
}

void MODFX_INIT(uint32_t platform, uint32_t api) {
  (void)platform; (void)api;
  for (uint32_t i = 0; i < DLY_SIZE; ++i) s_line[i] = 0.f;
  s_wpos = 0;
  s_lfo_phase = 0.f;
  s_rate = s_rate_t = 0.5f / 48000.f;
  s_width = s_width_t = 0.5f;
  s_hp_x1 = s_hp_y1 = 0.f;
}

void MODFX_PROCESS(const float *main_xn, float *main_yn,
                   const float *sub_xn, float *sub_yn,
                   uint32_t frames) {
  const float base_ms = 9.f, depth_ms = 2.2f;
  const float spm = 48.f;                       // samples per ms
  const float hp_a = 0.98055f;                  // ~150 Hz one-pole HPF on the side signal

  for (uint32_t i = 0; i < frames; ++i) {
    // parameter smoothing (avoid zipper noise)
    s_rate  += 0.001f * (s_rate_t  - s_rate);
    s_width += 0.001f * (s_width_t - s_width);

    const float inL = main_xn[2 * i];
    const float inR = main_xn[2 * i + 1];
    const float mid = 0.5f * (inL + inR);

    s_line[s_wpos & DLY_MASK] = mid;

    // three LFO phases, 120 degrees apart
    float p0 = s_lfo_phase;
    float p1 = p0 + 0.3333333f; if (p1 >= 1.f) p1 -= 1.f;
    float p2 = p0 + 0.6666667f; if (p2 >= 1.f) p2 -= 1.f;

    const float d0 = read_frac((base_ms + depth_ms * lfo_tri(p0)) * spm);
    const float d1 = read_frac((base_ms * 1.37f + depth_ms * lfo_tri(p1)) * spm);
    const float d2 = read_frac((base_ms * 1.81f + depth_ms * lfo_tri(p2)) * spm);

    float side = (d0 - d1) + 0.5f * (d2 - d0);

    // keep the low end centred
    const float hp = hp_a * (s_hp_y1 + side - s_hp_x1);
    s_hp_x1 = side;
    s_hp_y1 = hp;
    side = hp * s_width;

    main_yn[2 * i]     = inL + side;
    main_yn[2 * i + 1] = inR - side;

    ++s_wpos;
    s_lfo_phase += s_rate;
    if (s_lfo_phase >= 1.f) s_lfo_phase -= 1.f;
  }

  // sub timbre (prologue only) passes through untouched
  for (uint32_t i = 0; i < 2 * frames; ++i) sub_yn[i] = sub_xn[i];
}

void MODFX_PARAM(uint8_t index, int32_t value) {
  const float v = q31_to_f32(value);   // 0..1
  switch (index) {
  case k_user_modfx_param_time:
    // 0.05 Hz .. ~4 Hz, exponential feel
    s_rate_t = (0.05f + 3.95f * v * v) / 48000.f;
    break;
  case k_user_modfx_param_depth:
    s_width_t = 1.2f * v;
    break;
  default: break;
  }
}
