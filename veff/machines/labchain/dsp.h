#pragma once
// Fixed-point helpers for Lab Chain's slots, after veffbox's vb_dsp.h. Audio
// is Q15 in i32 between stages; wide products go through opxy::q31::wide.
#include <opxy/q31.hpp>
#include "tables.h"

namespace labchain { namespace dsp {
using opxy::i16; using opxy::i32; using opxy::u32; using opxy::i64; using opxy::u8;

inline i32 clamp(i32 x, i32 lo, i32 hi) { return x < lo ? lo : x > hi ? hi : x; }
inline i32 sat(i32 x) { return clamp(x, -32768, 32767); }
inline i32 iabs(i32 x) { return x < 0 ? -x : x; }
// (a * b) >> sh through a full 64-bit product.
inline i32 mul_sh(i32 a, i32 b, int sh) { return static_cast<i32>(opxy::q31::wide(a, b) >> sh); }

// Sine of a 32-bit phase, Q15, linearly interpolated.
inline i32 sine(u32 ph) {
    const u32 i = ph >> 22, f = (ph >> 7) & 0x7fff;
    const i32 a = tables::sine[i], b = tables::sine[i + 1];
    return a + (((b - a) * static_cast<i32>(f)) >> 15);
}
// tanh of a Q15 value read as x / 8192 (+-32767 is about +-4), Q15 out.
inline i32 tanh(i32 x) {
    x = clamp(x, -32767, 32767);
    const u32 u = static_cast<u32>(x + 32768), i = u >> 8, f = u & 0xff;
    const i32 a = tables::tanh[i], b = tables::tanh[i + 1];
    return a + (((b - a) * static_cast<i32>(f)) >> 8);
}
inline u32 rng(u32& s) { u32 x = s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return s = x; }
inline i32 noise(u32& s) { return static_cast<i32>(rng(s) >> 16) - 32768; }

// One-pole low-pass whose step is rounded away from zero, so the state reaches
// its target exactly. |x - z| * k must fit 31 bits.
inline i32 lpz(i32& z, i32 x, i32 k) {
    const i32 e = x - z;
    z += ((e * k) >> 15) + (e > 0);
    return z;
}
// x minus its one-pole low-pass at fs / (2 pi 2^sh), carrying the step's
// remainder: linear to the last bit, and exactly 0 once x stays 0. |x| < 2^24.
inline i32 hpz(i32& lp, i32& err, i32 x, int sh) {
    const i32 e = x * 64 - lp + err, d = e >> sh;
    err = e - d * (1 << sh);
    lp += d;
    return x - ((lp + 32) >> 6);
}
// Soft clip: linear to +-16384, then a tanh knee up to +-32767.
inline i32 softclip(i32 x) {
    if (x > 16384) return 16384 + (tanh((x - 16384) >> 1) >> 1);
    if (x < -16384) return -16384 - (tanh((-16384 - x) >> 1) >> 1);
    return x;
}
// 2^(x / 65536) in Q16, x in -16 .. +14 octaves.
inline u32 exp2_q16(i32 x) {
    i32 i = x >> 16;
    const u32 f = static_cast<u32>(x) & 0xffffu, j = f >> 8;
    const u32 m = tables::exp2[j] + (((tables::exp2[j + 1] - tables::exp2[j]) * (f & 255u)) >> 8);
    if (i > 14) i = 14;
    return i >= 0 ? m << i : i > -31 ? m >> -i : 0;
}
// tan(pi fc / fs) at a Q8 index into tables::tangent.
inline i32 tangent(i32 c) {
    const i32 i = c >> 8;
    return tables::tangent[i] + (((tables::tangent[i + 1] - tables::tangent[i]) * (c & 255)) >> 8);
}
// Triangle, Q15, in phase with sine (0 at 0, rising).
inline i32 tri15(u32 ph) {
    const i32 t = static_cast<i32>((ph + 0x40000000u) >> 15);
    return t < 65536 ? t - 32768 : 98303 - t;
}
}}
