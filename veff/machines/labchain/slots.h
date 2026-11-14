#pragma once
// Lab Chain's effect types: veffbox's thirteen FX (core/vb_fx.c, by the same
// author), ported to the OP-XY SDK, plus EQ, CLIP and LIMIT. One slot works on
// mono Q15 audio in 32-sample chunks: the input is clamped to 16 bits (the dry
// copy), the type writes its wet signal in place, then MIX blends dry and wet
// (gliding) and the result is clamped again. A slot that has been silent long
// enough for its tail to end clears its filter states and sleeps.
#include "dsp.h"

namespace labchain {
using opxy::i16; using opxy::i32; using opxy::u32; using opxy::u8;

enum Type {
    kEq, kDrive, kFold, kCrunch, kBits, kFilter, kTape, kReverb, kChorus, kPhaser,
    kRing, kReson, kFreeze, kShift, kClip, kLimit, kOff, kTypes
};
const u32 kChunk = 32;              // samples a chunk
const u32 kLine = 8192;             // samples of delay line a slot channel owns
const u32 kMask = kLine - 1;
const u32 kLineWords = kLine / 2;   // two i16 a buffer word
const i32 kQuietLevel = 2;          // a wet peak this small counts as silence (-84 dB)
const i32 kMixUnset = -1;           // z[11] after a reset: the mix glide starts at its target
const i32 kSleepLine = kLine / kChunk + 4;

// A type's controls: four native parameters and MIX, each 0..127. Its four
// encoder roles name which of the five each encoder turns (4 = MIX); the
// others keep their defaults.
struct TypeInfo {
    const char* name;      // up to 8 characters
    const char* label[4];  // the encoders' labels, up to 5 characters
    u8 role[4];            // native parameter (0..3) or 4 = MIX, per encoder
    u8 initial[5];         // p[0..3] and MIX
};
const TypeInfo types[kTypes] = {
    {"EQ",       {"LOW", "MID", "FREQ", "HIGH"}, {0, 1, 2, 3}, {64, 64, 64, 64, 127}},
    {"DRIVE",    {"DRV", "TONE", "BIAS", "MIX"}, {0, 1, 2, 4}, {50, 90, 0, 64, 127}},
    {"FOLD",     {"AMT", "SYM", "PRE", "MIX"},   {0, 1, 2, 4}, {40, 64, 110, 64, 127}},
    {"CRUNCH",   {"AMT", "RATE", "HISS", "MIX"}, {0, 1, 2, 4}, {40, 90, 20, 64, 127}},
    {"BITS",     {"BITS", "SR", "JIT", "MIX"},   {0, 1, 2, 4}, {50, 80, 0, 64, 127}},
    {"FILTER",   {"CUT", "RES", "TYPE", "DRV"},  {0, 1, 2, 3}, {70, 50, 0, 0, 127}},
    {"TAPE DLY", {"TIME", "FB", "LP", "MIX"},    {0, 1, 3, 4}, {48, 60, 10, 90, 48}},
    {"REVERB",   {"SIZE", "DEC", "DAMP", "MIX"}, {0, 1, 2, 4}, {80, 80, 50, 0, 40}},
    {"CHORUS",   {"RATE", "DEP", "FB", "MIX"},   {0, 1, 2, 4}, {40, 60, 0, 100, 64}},
    {"PHASER",   {"RATE", "DEP", "CNTR", "FB"},  {0, 1, 2, 3}, {30, 80, 60, 60, 127}},
    {"RING",     {"TUNE", "WAVE", "EXT", "MIX"}, {0, 1, 2, 4}, {72, 0, 0, 127, 127}},
    {"RESON",    {"TUNE", "DEC", "CHRD", "MIX"}, {0, 1, 3, 4}, {64, 70, 80, 0, 64}},
    {"FREEZE",   {"SIZE", "SPRY", "PTCH", "HOLD"}, {0, 1, 2, 3}, {60, 30, 64, 0, 127}},
    {"SHIFT",    {"PTCH", "FB", "WIN", "MIX"},   {0, 1, 2, 4}, {96, 40, 60, 100, 64}},
    {"CLIP",     {"DRV", "SHPE", "TONE", "OUT"}, {0, 1, 2, 3}, {0, 0, 127, 64, 127}},
    {"LIMIT",    {"GAIN", "CEIL", "REL", "MIX"}, {0, 1, 2, 4}, {0, 127, 40, 0, 127}},
    {"OFF",      {"", "", "", ""},               {0, 1, 2, 3}, {0, 0, 0, 0, 0}},
};

// What a slot plays: its type, four native parameters and MIX, each 0..127.
struct Slot { u8 type, p[4], mix; };

inline Slot initial_slot(u32 type) {
    const TypeInfo& t = types[type < kTypes ? type : kOff];
    Slot s;
    s.type = static_cast<u8>(type < kTypes ? type : kOff);
    for (u32 i = 0; i < 4; ++i) s.p[i] = t.initial[i];
    s.mix = t.initial[4];
    return s;
}

// The runtime state of one slot channel. It lives in the send's scratch
// buffer beside its delay line, so the effect's own State stays small.
struct SlotState {
    i32 type;          // the type the state belongs to; kTypes when unformatted
    u32 pos, random, seed;
    i32 z[12];         // filter, phase and envelope states, meaning per type
    u32 ph[4];
    i32 quiet;         // silent chunks in and out; enough of them and the slot sleeps
    u32 clearing;      // delay-line words still to clear after a type change
    u32 variant;       // 1 for the right channel (stereo spread of reverb and chorus)
};

namespace detail {
using namespace dsp;
// The z[] entries that are filter states (cleared when the slot falls asleep).
const u8 kStates[kTypes] = {6, 5, 3, 6, 2, 4, 3, 4, 0, 9, 1, 4, 0, 1, 1, 2, 0};

// A read between samples of the whole line, the position Q16.
inline i32 read(const i16* b, u32 pos) {
    const u32 i = (pos >> 16) & kMask;
    const i32 a = b[i], c = b[(i + 1) & kMask];
    return a + (((c - a) * static_cast<i32>((pos >> 1) & 0x7fff)) >> 15);
}

// ---------------------------------------------------------------- EQ: LOW MID FREQ HIGH
// Three bands from one-pole splits: a low shelf below ~200 Hz, a broad mid
// band (about two octaves) around FREQ (300 Hz .. 6 kHz) and a high shelf
// above ~5 kHz. Each gain is +-12 dB, 64 = 0 dB. All 64 is an exact bypass.
// z: 0 low split, 1 high split, 2..3 the mid band's two low-passes.
inline i32 eq_gain(u8 p) { return static_cast<i32>(exp2_q16((static_cast<i32>(p) - 64) * 2048)) - 65536; }
inline void eq(SlotState& s, const u8* p, i32* x) {
    if (p[0] == 64 && p[1] == 64 && p[3] == 64) return;
    const i32 gl = eq_gain(p[0]), gm = eq_gain(p[1]), gh = eq_gain(p[3]);
    const i32 centre = 33 + p[2] * 56 / 127;
    const i32 kl = tables::onepole[29], kh = tables::onepole[88];
    const i32 k1 = tables::onepole[centre - 13], k2 = tables::onepole[centre + 13];
    i32 zl = s.z[0], zh = s.z[1], z1 = s.z[2], z2 = s.z[3];
    for (u32 i = 0; i < kChunk; ++i) {
        const i32 v = x[i];
        const i32 low = lpz(zl, v, kl), high = v - lpz(zh, v, kh);
        const i32 mid = lpz(z2, v, k2) - lpz(z1, v, k1);
        x[i] = sat(v + mul_sh(gl, low, 16) + mul_sh(gm, mid, 16) + mul_sh(gh, high, 16));
    }
    s.z[0] = zl; s.z[1] = zh; s.z[2] = z1; s.z[3] = z2;
}

// ---------------------------------------------------------------- DRIVE: DRV TONE BIAS OUT
// Low cut (~60 Hz) -> gain 1x .. 9x -> tanh offset by BIAS (even harmonics) ->
// DC blocker -> one-pole TONE -> make-up that falls with the drive, times OUT.
// z: 0..1 low cut, 2..3 DC, 4 tone.
inline void drive(SlotState& s, const u8* p, i32* x) {
    const i32 d = p[0], g = 4096 + d * d * 2, b = p[2] * 40, b0 = tanh(b);
    const i32 k = tables::onepole[40 + p[1] * 87 / 127], gain = ((32767 - d * 120) * (p[3] * 512)) >> 15;
    i32 z[5];
    for (u32 j = 0; j < 5; ++j) z[j] = s.z[j];
    for (u32 i = 0; i < kChunk; ++i) {
        i32 v = hpz(z[0], z[1], x[i], 7);
        v = tanh((((v >> 1) * g) >> 13) + b) - b0;
        if (b) v = sat(hpz(z[2], z[3], v, 9));
        x[i] = (lpz(z[4], v, k) * gain) >> 15;
    }
    for (u32 j = 0; j < 5; ++j) s.z[j] = z[j];
}

// ---------------------------------------------------------------- FOLD: AMT SYM PRE OUT
// A sine wavefolder: PRE low-passes its input, AMT the gain into the folder
// (x1 .. x13), SYM an offset (64 symmetric), a DC blocker after it, OUT.
// z: 0 pre, 1..2 DC.
inline void fold(SlotState& s, const u8* p, i32* x) {
    const i32 g = 2608 + p[0] * p[0] * 2, off = (p[1] - 64) * 512, y0 = sine(static_cast<u32>(off) << 15);
    const i32 k = tables::onepole[30 + p[2] * 97 / 127], gain = p[3] * 512;
    i32 z0 = s.z[0], z1 = s.z[1], z2 = s.z[2];
    for (u32 i = 0; i < kChunk; ++i) {
        const i32 u = ((lpz(z0, x[i], k) * g) >> 12) + off;
        const i32 v = sine(static_cast<u32>(u) << 15) - y0;
        x[i] = (sat(hpz(z1, z2, v, 9)) * gain) >> 15;
    }
    s.z[0] = z0; s.z[1] = z1; s.z[2] = z2;
}

// ---------------------------------------------------------------- CRUNCH: AMT RATE HISS OUT
// An IMA ADPCM round trip at a lower rate, held between its samples. AMT drives
// the coder and takes bits off the code; RATE 44.1 kHz down 4.5 octaves; HISS
// noise at the input's level, crunched with it.
// z: 0 predictor, 1 step index, 2 held sample, 3 envelope, 4 hiss filter, 5 rate accumulator.
const i16 kImaStep[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
};
const opxy::i8 kImaIndex[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
inline i32 ima(i32& pred, i32& idx, i32 x, i32 mask) {
    const i32 step = kImaStep[idx];
    i32 diff = x - pred, code = 0, delta = step >> 3;
    if (diff < 0) { code = 8; diff = -diff; }
    if (diff >= step) { code |= 4; diff -= step; delta += step; }
    if ((mask & 2) && diff >= step >> 1) { code |= 2; diff -= step >> 1; delta += step >> 1; }
    if ((mask & 1) && diff >= step >> 2) { code |= 1; delta += step >> 2; }
    if (x == 0 && iabs(pred) <= step) pred = 0;   // silence decodes to exact 0
    else pred = sat(pred + ((code & 8) ? -delta : delta));
    idx = clamp(idx + kImaIndex[code & 7], 0, 88);
    return pred;
}
inline void crunch(SlotState& s, const u8* p, i32* x) {
    const i32 amt = p[0], gin = 256 + amt * 768 / 127, mask = amt < 43 ? 7 : amt < 86 ? 6 : 4;
    const i32 div = static_cast<i32>(exp2_q16((127 - p[1]) * 9 * 65536 / 254) >> 8);
    const i32 hg = p[2] * p[2] / 4, gain = p[3] * 512;
    i32 z[6];
    for (u32 j = 0; j < 6; ++j) z[j] = s.z[j];
    for (u32 i = 0; i < kChunk; ++i) {
        i32 v = sat((x[i] * gin) >> 8);
        if (hg) {
            const i32 a = iabs(x[i]);
            lpz(z[3], a, a > z[3] ? 8192 : 60);
            z[4] += ((noise(s.random) - z[4]) * 9000) >> 15;
            v = sat(v + ((((z[4] * z[3]) >> 15) * hg) >> 15));
        }
        z[5] += 256;
        if (z[5] >= div) { z[5] -= div; z[2] = ima(z[0], z[1], v, mask); }
        x[i] = (z[2] * gain) >> 15;
    }
    for (u32 j = 0; j < 6; ++j) s.z[j] = z[j];
}

// ---------------------------------------------------------------- BITS: BITS SR JIT OUT
// Bit depth 1 .. 16, sample and hold 44.1 kHz down to 340 Hz, JIT an unsteady
// clock. z: held sample, clock.
inline void bits(SlotState& s, const u8* p, i32* x) {
    const i32 sh = 15 - p[0] * 15 / 127, half = sh ? 1 << (sh - 1) : 0, jit = p[2], gain = p[3] * 512;
    const i32 inc = static_cast<i32>(exp2_q16((static_cast<i32>(p[1]) - 127) * 7 * 65536 / 127));
    i32 hold = s.z[0], clk = s.z[1];
    for (u32 i = 0; i < kChunk; ++i) {
        clk += inc;
        if (clk >= 65536) {
            clk -= 65536;
            if (jit) clk -= static_cast<i32>(((rng(s.random) >> 16) * static_cast<u32>(jit)) >> 7);
            hold = sat(((x[i] + half) >> sh) * (1 << sh));
        }
        x[i] = (hold * gain) >> 15;
    }
    s.z[0] = hold; s.z[1] = clk;
}

// ---------------------------------------------------------------- FILTER: CUT RES TYPE DRV
// A trapezoidal state-variable filter: CUT 20 Hz .. 16 kHz (glides), RES to the
// edge of self-oscillation, TYPE sweeps LP -> BP -> HP, DRV a tanh in front.
// States at 256x the signal, clamped. A DC blocker on the output lets the tail
// end in exact zeros. z: 0 ic1, 1 ic2, 2..3 DC; 8 the cutoff (Q8 index), 9 set.
inline i32 svp(i32 c, i32 v) { return (((v >> 10) * c) >> 5) + (((v & 1023) * c) >> 15); }
inline i32 svz(i32 c, i32 v) { return v < 0 ? -svp(c, -v) : svp(c, v); }
inline i32 svc(i32 x) { return clamp(x, -(1 << 25), (1 << 25) - 1); }
inline void filter(SlotState& s, const u8* p, i32* x) {
    i32* z = s.z;
    const i32 target = clamp(p[0] * 124 / 127, 0, 124) << 8;
    if (!z[9]) { z[8] = target; z[9] = 1; }
    z[8] += (target - z[8]) / 4;
    const i32 g = tangent(z[8]), k = 65536 - p[1] * 64224 / 127;
    const i32 den = 65536 + mul_sh(g, g + 2 * k, 16);
    const i32 a1 = static_cast<i32>(0x80000000u / static_cast<u32>(den)), a2 = mul_sh(g, a1, 16), a3 = mul_sh(g, a2, 16);
    const i32 t = p[2], wl = t < 64 ? 64 - t : 0, wb = t < 64 ? t : 127 - t, wh = t < 64 ? 0 : t - 64;
    const i32 gd = 4096 + p[3] * p[3] * 2, drv = p[3];
    i32 ic1 = z[0], ic2 = z[1];
    for (u32 i = 0; i < kChunk; ++i) {
        const i32 v0 = (drv ? tanh((x[i] * gd) >> 14) : x[i]) * 256;
        const i32 v3 = v0 - ic2;
        const i32 v1 = svc(svz(a1, ic1) + svp(a2, v3));
        const i32 v2 = svc(ic2 + svp(a2, ic1) + svp(a3, v3));
        ic1 = svc(2 * v1 - ic1);
        ic2 = svc(2 * v2 - ic2);
        i32 y = (v2 >> 8) * wl + (v1 >> 8) * wb;
        if (wh) y += (svc(v0 - svp(k, v1) - v2) >> 8) * wh;
        y = sat(y / 64);
        x[i] = sat(hpz(z[2], z[3], y, 10));
    }
    z[0] = ic1; z[1] = ic2;
}

// ---------------------------------------------------------------- TAPE DLY: TIME FB HP LP
// TIME in 1/256 notes of the tempo (64 = a beat), up to 2 s, on a tape of 8190
// cells written at a rate that makes the loop the delay's length (longer =
// fewer cells a second, lower fidelity). A new TIME glides the rate. The
// feedback runs once a cell through HP, LP and a soft clip (FB up to 1.1).
// z: 0 the cell's sum, 1 high-pass (Q8), 2 low-pass; 8 rate (Q16), 9 loop (Q16). ph[0]: write head.
const u32 kTapeCells = kLine - 2;
inline void tape(SlotState& s, i16* b, const u8* p, i32* x, i32 bpm10) {
    const u32 beat = 26460000u / static_cast<u32>(clamp(bpm10, 200, 3000));
    u32 d = static_cast<u32>(p[0] ? p[0] : 1) * beat / 64u, rt, dt;
    d = d < 64u ? 64u : d > 88200u ? 88200u : d;
    if (d <= kTapeCells) { rt = 65536u; dt = d << 16; }
    else { rt = (kTapeCells << 16) / d; dt = kTapeCells << 16; }
    i32* z = s.z;
    if (!z[8]) { z[8] = static_cast<i32>(rt); z[9] = static_cast<i32>(dt); }
    i32 rate = z[8], dc = z[9];
    const i32 r1 = rate + (static_cast<i32>(rt) - rate) / 16, d1 = dc + (static_cast<i32>(dt) - dc) / 16;
    const i32 drate = (r1 - rate) / static_cast<i32>(kChunk), ddc = (d1 - dc) / static_cast<i32>(kChunk);
    const i32 fb = p[1] * 284, ah = tables::onepole[p[2] * 80 / 127], al = tables::onepole[40 + p[3] * 87 / 127];
    i32 acc = z[0], h = z[1], l = z[2];
    u32 wp = s.ph[0];
    for (u32 i = 0; i < kChunk; ++i) {
        const u32 nx = wp + static_cast<u32>(rate);
        const i32 y = read(b, wp - static_cast<u32>(dc));
        acc += x[i];
        if ((nx ^ wp) >> 16) {
            const u32 c = (wp >> 16) & kMask, r = (c - (static_cast<u32>(dc) >> 16)) & kMask;
            const i32 in = b[r] * 256;
            h += mul_sh(in - h, ah, 15);
            lpz(l, sat((in - h) / 256), al);
            const i32 a = rate >= 65536 ? acc : mul_sh(acc, rate, 16);
            b[c] = static_cast<i16>(sat(softclip(a + l * fb / 32768)));
            acc = 0;
        }
        wp = nx;
        rate += drate;
        dc += ddc;
        x[i] = y;
    }
    z[0] = acc; z[1] = h; z[2] = l; z[8] = r1; z[9] = d1;
    s.ph[0] = wp;
}

// ---------------------------------------------------------------- REVERB: SIZE DEC DAMP GATE
// Schroeder / Freeverb: four damped combs into two allpasses. SIZE scales the
// comb lengths 0.27x .. 1x (gliding), DEC their feedback, DAMP the low-pass in
// their loops; GATE closes the tail GATE x 10 ms after the input last sounded.
// The right channel's combs are 23 samples longer (Freeverb's stereo spread).
// z: 0..3 comb low-passes, 4 gate count, 5 gate gain, 6..7 allpass positions; 8 size. ph[0..3]: comb positions.
const u32 kReverbComb[4] = {1451, 1544, 1660, 1763};
const u32 kReverbAllpass[2] = {556, 441};
const u32 kReverbSpread = 23;
inline void reverb(SlotState& s, i16* b, const u8* p, i32* x) {
    i32* z = s.z;
    const i32 target = (69 + p[0] * 187 / 127) << 8;
    if (!z[8]) z[8] = target;
    z[8] += clamp(target - z[8], -64, 64);
    const u32 spread = s.variant ? kReverbSpread : 0;
    i32 len[4], base[4], o = 0;
    const i32 fb = 22938 + p[1] * 72, damp = tables::onepole[120 - p[2] * 80 / 127], gt = p[3] * 441;
    for (u32 c = 0; c < 4; ++c) {
        len[c] = static_cast<i32>(((kReverbComb[c] + spread) * static_cast<u32>(z[8])) >> 16);
        base[c] = o;
        o += static_cast<i32>(kReverbComb[c] + kReverbSpread);
    }
    const i32 ap0base = o, ap1base = o + static_cast<i32>(kReverbAllpass[0]);
    if (!gt) z[5] = 32767;
    i32 lp[4], ix[4], ap0 = z[6], ap1 = z[7], hold = z[4], gg = z[5];
    for (u32 c = 0; c < 4; ++c) { lp[c] = z[c]; ix[c] = static_cast<i32>(s.ph[c]); if (ix[c] >= len[c]) ix[c] = 0; }
    for (u32 i = 0; i < kChunk; ++i) {
        const i32 in = x[i] / 4;
        i32 wet = 0, fbe = fb;
        if (gt) {
            if (x[i] > 1024 || x[i] < -1024) hold = gt;
            if (hold) { --hold; gg = gg + 1024 > 32767 ? 32767 : gg + 1024; }
            else gg = gg > 256 ? gg - 256 : 0;
            fbe = fb * gg / 32768;
        }
        for (u32 c = 0; c < 4; ++c) {
            i16* q = b + base[c] + ix[c];
            const i32 y = *q;
            *q = static_cast<i16>(sat(in + lpz(lp[c], y, damp) * fbe / 32768));
            if (++ix[c] >= len[c]) ix[c] = 0;
            wet += y;
        }
        i32 u = wet / 2, d = b[ap0base + ap0], ov = d - u / 2;
        b[ap0base + ap0] = static_cast<i16>(sat(u + ov / 2));
        if (++ap0 >= static_cast<i32>(kReverbAllpass[0])) ap0 = 0;
        u = ov;
        d = b[ap1base + ap1];
        ov = d - u / 2;
        b[ap1base + ap1] = static_cast<i16>(sat(u + ov / 2));
        if (++ap1 >= static_cast<i32>(kReverbAllpass[1])) ap1 = 0;
        x[i] = sat(gt ? ov * gg / 32768 : ov);
    }
    for (u32 c = 0; c < 4; ++c) { z[c] = lp[c]; s.ph[c] = static_cast<u32>(ix[c]); }
    z[4] = hold; z[5] = gg; z[6] = ap0; z[7] = ap1;
}

// ---------------------------------------------------------------- CHORUS: RATE DEP FB WIDE
// Three taps of a 15 ms delay swung by triangles (DEP up to +-8 ms, RATE 0.05 ..
// 27 Hz), FB fed back. WIDE spreads the taps' phases; the right channel's LFO
// runs a quarter turn ahead. z: 8..10 the taps' delays (Q8). ph[0] LFO, ph[1] write.
inline void chorus(SlotState& s, i16* b, const u8* p, i32* x) {
    i32 dq[3], dd[3];
    const i32 dep = p[1] * 711, fb = p[2] * 230;
    const u32 spread = (0x55555555u / 127u) * p[3], offset = s.variant ? 0x40000000u : 0;
    s.ph[0] += tables::lfo[p[0]] * kChunk;
    for (u32 j = 0; j < 3; ++j) {
        const i32 d1 = (661 << 8) + (((tri15(s.ph[0] + offset + spread * j) >> 3) * dep) >> 12);
        dq[j] = s.z[8 + j] ? s.z[8 + j] : d1;
        dd[j] = (d1 - dq[j]) / static_cast<i32>(kChunk);
        s.z[8 + j] = d1;
    }
    u32 w = s.ph[1];
    for (u32 i = 0; i < kChunk; ++i, ++w) {
        i32 wet = 0;
        for (u32 j = 0; j < 3; ++j) {
            dq[j] += dd[j];
            wet += read(b, (w << 16) - (static_cast<u32>(dq[j]) << 8));
        }
        wet = (wet * 21845) >> 16;
        b[w & kMask] = static_cast<i16>(sat(x[i] + wet * fb / 32768));
        x[i] = wet;
    }
    s.ph[1] = w;
}

// ---------------------------------------------------------------- PHASER: RATE DEP CNTR FB
// Four first-order allpasses swept by a triangle around CNTR (110 Hz .. 9 kHz),
// DEP octaves, FB fed back; the wet is input plus chain (notches).
// z: 0..3 allpasses, 6 feedback, 7..8 DC; 9 the coefficient (Q13). ph[0]: LFO.
inline void phaser(SlotState& s, const u8* p, i32* x) {
    i32* z = s.z;
    s.ph[0] += tables::lfo[p[0]] * kChunk;
    const i32 c = (32 << 8) + p[2] * (73 << 8) / 127 + (((tri15(s.ph[0]) >> 3) * (p[1] * 9728 / 127)) >> 12);
    const i32 g = tangent(clamp(c, 0, 112 << 8));
    const i32 a1 = (g - 65536) * 8192 / (g + 65536), fb = p[3] * 230;
    i32 a = z[9];
    const i32 da = (a1 - a) / static_cast<i32>(kChunk);
    i32 s0 = z[0], s1 = z[1], s2 = z[2], s3 = z[3], last = z[6], dc = z[7], de = z[8];
    for (u32 i = 0; i < kChunk; ++i) {
        const i32 f = last * fb / 32768;
        i32 u = clamp(x[i] + f - clamp(f, -1, 1), -65536, 65535), y;
        a += da;
        y = clamp(((a * u) >> 13) + s0, -65536, 65535); s0 = u - ((a * y) >> 13); u = y;
        y = clamp(((a * u) >> 13) + s1, -65536, 65535); s1 = u - ((a * y) >> 13); u = y;
        y = clamp(((a * u) >> 13) + s2, -65536, 65535); s2 = u - ((a * y) >> 13); u = y;
        y = clamp(((a * u) >> 13) + s3, -65536, 65535); s3 = u - ((a * y) >> 13); u = y;
        last = u;
        x[i] = sat(hpz(dc, de, (x[i] + u) >> 1, 10));
    }
    z[0] = s0; z[1] = s1; z[2] = s2; z[3] = s3; z[6] = last; z[7] = dc; z[8] = de; z[9] = a1;
}

// ---------------------------------------------------------------- RING: TUNE WAVE EXT KEY
// The input times a carrier at TUNE as a MIDI note (60 = C4; a send effect has
// no played note). WAVE morphs sine -> triangle -> square; EXT leaks the
// carrier in at the input's level. z: envelope. ph[0]: carrier.
const u32 kIncC0 = 796254;            // u32 phase increment of MIDI note 0
const i32 kPeriodC0Q8 = 1380856;      // the period of MIDI note 0 in samples, Q8
inline u32 note_increment(i32 semis_q8) {
    semis_q8 = clamp(semis_q8, 0, 135 << 8);
    return static_cast<u32>(mul_sh(static_cast<i32>(kIncC0), static_cast<i32>(exp2_q16(semis_q8 * 64 / 3)), 16));
}
inline void ring(SlotState& s, const u8* p, i32* x) {
    const i32 wv = p[1], ext = p[2] * 258;
    const i32 wq = wv < 64 ? wv * 512 : (wv - 64) * 520;
    const u32 inc = note_increment(p[0] * 256);
    u32 ph = s.ph[0];
    i32 env = s.z[0];
    for (u32 i = 0; i < kChunk; ++i, ph += inc) {
        i32 c;
        const i32 a = iabs(x[i]);
        if (wv < 64) {
            const i32 sn = sine(ph);
            c = wv ? sn + (((tri15(ph) - sn) * wq) >> 15) : sn;
        } else {
            const i32 t = tri15(ph);
            c = t + ((((ph < 0x80000000u ? 32767 : -32767) - t) * wq) >> 15);
        }
        i32 y = (x[i] * c) >> 15;
        if (ext) { lpz(env, a, a > env ? 8192 : 100); y += (((c * env) >> 15) * ext) >> 15; }
        x[i] = sat(y);
    }
    s.ph[0] = ph;
    s.z[0] = env;
}

// ---------------------------------------------------------------- RESON: TUNE DEC TONE CHRD
// Karplus-Strong strings excited by the input, tuned to C4 + TUNE (-24 .. +24
// semitones, 64 = C4). DEC the ring time, TONE the loop's low-pass, CHRD one
// string or a 2..4-string chord. Each string has a 2048-sample quarter of the line.
// z: 0..3 strings' low-passes; 4..7 their delays (Q8); 8..9 DC. ph[0]: write position.
const opxy::i8 kChord[9][4] = {
    {0, 0, 0, 0}, {0, 12, 0, 0}, {0, 7, 12, 0}, {0, 4, 7, 0}, {0, 3, 7, 0},
    {0, 5, 7, 0}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 7, 12, 19},
};
const u8 kChordSize[9] = {1, 2, 3, 3, 3, 3, 4, 4, 4};
const i32 kChordLevel[5] = {0, 32767, 23170, 18919, 16384};   // 1 / sqrt(strings)
inline void reson(SlotState& s, i16* b, const u8* p, i32* x) {
    const i32 ci = (p[3] + 15) >> 4, nl = kChordSize[ci];
    const i32 dt = p[0] - 64, root = (60 + (dt * 24 + (dt < 0 ? -31 : 31)) / 63) * 256;
    const i32 k = tables::onepole[50 + p[2] * 77 / 127];
    const i32 comp = ((32768 - k) << 8) / k, e = (1 << 30) - tables::decay[40 + p[1] * 87 / 127];
    i32 g[4], dq[4], dd[4];
    i32* z = s.z;
    for (i32 j = 0; j < nl; ++j) {
        const i32 sq8 = root + kChord[ci][j] * 256;
        i32 per = mul_sh(kPeriodC0Q8, static_cast<i32>(exp2_q16(-(sq8 * 64 / 3))), 16);
        per = clamp(per, 8 << 8, 2040 << 8);
        const i32 t = mul_sh(per, e, 22);
        const i32 gq = static_cast<i32>(exp2_q16(-mul_sh(t, 94548, 16)) >> 1);
        g[j] = gq > 32767 ? 32767 : gq;
        const i32 dl = clamp(per - comp, 2 << 8, 2040 << 8);
        if (!z[4 + j]) z[4 + j] = dl;
        dq[j] = z[4 + j];
        z[4 + j] += (dl - z[4 + j]) / 4;
        dd[j] = (z[4 + j] - dq[j]) / static_cast<i32>(kChunk);
    }
    u32 w = s.ph[0];
    i32 lp[4], dc = z[8], de = z[9];
    const i32 on = kChordLevel[nl];
    for (i32 j = 0; j < nl; ++j) lp[j] = z[j];
    for (u32 i = 0; i < kChunk; ++i, w = (w + 1) & 2047u) {
        const i32 in = x[i] / 4;
        i32 out = 0;
        for (i32 j = 0; j < nl; ++j) {
            i16* q = b + j * 2048;
            dq[j] += dd[j];
            const u32 r = (w - (static_cast<u32>(dq[j]) >> 8)) & 2047u;
            const i32 a = q[r], c = q[(r - 1) & 2047u];
            const i32 y = lpz(lp[j], a + (((c - a) * (dq[j] & 255)) >> 8), k);
            q[w] = static_cast<i16>(sat(in + y * g[j] / 32768));
            out += y;
        }
        x[i] = sat(hpz(dc, de, (out * on) >> 15, 10));
    }
    for (i32 j = 0; j < nl; ++j) z[j] = lp[j];
    z[8] = dc; z[9] = de;
    s.ph[0] = w;
}

// ---------------------------------------------------------------- FREEZE: SIZE SPRY PTCH HOLD
// Two grains with triangle windows half a grain apart read the recent line.
// SIZE 6 .. 58 ms; SPRY how far back a grain may start at random; PTCH -12 ..
// +12 semitones; HOLD 64 and over stops recording: the capture is frozen.
// z: grain g at 3g: its sample count, length, 2^27 / length. ph[0]: write, ph[1..2]: grains (Q16).
inline void freeze(SlotState& s, i16* b, const u8* p, i32* x) {
    const i32 gl = 256 + p[0] * 2304 / 127;
    i32* z = s.z;
    const u32 r = exp2_q16((p[2] - 64) * 1024);
    u32 w = s.ph[0];
    const bool hold = p[3] >= 64;
    if (!z[1]) {
        z[1] = z[4] = gl;
        z[2] = z[5] = (1 << 27) / gl;
        z[0] = gl;
        z[3] = gl / 2;
        s.ph[2] = (w - static_cast<u32>(gl)) << 16;
    }
    for (u32 i = 0; i < kChunk; ++i) {
        i32 y = 0;
        if (!hold) b[w++ & kMask] = static_cast<i16>(x[i]);
        for (u32 gi = 0; gi < 2; ++gi) {
            i32* gz = z + 3 * gi;
            if (gz[0] >= gz[1]) {
                const i32 span = static_cast<i32>((static_cast<u32>(gl) * r) >> 16), avail = 8180 - gl - span;
                u32 back = static_cast<u32>(span) + 2u;
                if (avail > 0 && p[1]) back += ((rng(s.random) >> 16) * static_cast<u32>(avail * p[1] / 127)) >> 16;
                gz[0] = 0; gz[1] = gl; gz[2] = (1 << 27) / gl;
                s.ph[1 + gi] = (w - back) << 16;
            }
            const i32 fr = (gz[0]++ * gz[2]) >> 12, win = fr < 16384 ? fr * 2 : (32768 - fr) * 2;
            y += read(b, s.ph[1 + gi]) * win / 32768;
            s.ph[1 + gi] += r;
        }
        x[i] = sat(y);
    }
    s.ph[0] = w;
}

// ---------------------------------------------------------------- SHIFT: PTCH FB WIN TONE
// Two taps sweep a delay of WIN (6 .. 93 ms) half a turn apart, each faded by a
// raised cosine. PTCH -24 .. +24 semitones (64 none); TONE low-passes the
// shifted signal; FB sends it back through a soft clip (the shimmer).
// z: 0 tone; 1 the window (Q8). ph[0]: write, ph[1]: sweep.
inline void shift(SlotState& s, i16* b, const u8* p, i32* x) {
    const i32 dp = p[0] - 64, semis = (dp * 24 + (dp < 0 ? -31 : 31)) / 63;
    const i32 r = static_cast<i32>(exp2_q16(semis * 65536 / 12)), wn = 256 + p[2] * 3840 / 127;
    const i32 inc = mul_sh(r - 65536, static_cast<i32>(0xffffffffu / static_cast<u32>(wn)), 16);
    const i32 fb = p[1] * 232, k = tables::onepole[40 + p[3] * 87 / 127];
    u32 w = s.ph[0], ph = s.ph[1];
    i32 tone = s.z[0], wq = s.z[1] ? s.z[1] : wn << 8;
    s.z[1] = wq + ((wn << 8) - wq) / 4;
    const i32 dw = (s.z[1] - wq) / static_cast<i32>(kChunk);
    for (u32 i = 0; i < kChunk; ++i, ++w, ph -= static_cast<u32>(inc)) {
        const u32 wcur = static_cast<u32>(wq += dw) >> 8;
        const u32 da = (16u << 16) + (ph >> 16) * wcur, db = (16u << 16) + ((ph + 0x80000000u) >> 16) * wcur;
        const i32 ya = read(b, (w << 16) - da), yb = read(b, (w << 16) - db);
        const i32 wa = (32768 - sine(ph + 0x40000000u)) >> 1;
        const i32 y = lpz(tone, (ya * wa + yb * (32768 - wa)) >> 15, k);
        b[w & kMask] = static_cast<i16>(sat(softclip(x[i] + y * fb / 32768)));
        x[i] = y;
    }
    s.ph[0] = w; s.ph[1] = ph; s.z[0] = tone;
}

// ---------------------------------------------------------------- CLIP: DRV SHPE TONE OUT
// Gain 1x .. 16x into a blend of a tanh (SHPE 0) and a hard clip (127), a
// one-pole TONE after it (127 open), OUT (64 = x1, up to 2x). z: 0 tone.
inline void clip(SlotState& s, const u8* p, i32* x) {
    const i32 g = static_cast<i32>(exp2_q16(p[0] * 4 * 65536 / 127) >> 8);   // Q8: 256 .. 4096
    const i32 shape = p[1], k = tables::onepole[40 + p[2] * 87 / 127], gain = p[3] * 512;
    const bool open = p[2] == 127;
    i32 tone = s.z[0];
    for (u32 i = 0; i < kChunk; ++i) {
        const i32 v = (x[i] * g) >> 8;
        const i32 soft = tanh(v >> 2), hard = sat(v);
        i32 y = soft + (((hard - soft) * shape) >> 7);
        if (!open) y = lpz(tone, y, k);
        x[i] = sat((y * gain) >> 15);
    }
    s.z[0] = tone;
}

// ---------------------------------------------------------------- LIMIT: GAIN CEIL REL
// Input gain 0 .. +24 dB, a peak follower with instant attack and RELEASE
// (10 ms .. 1 s), and a gain computed every 8 samples that holds the peak at
// CEIL (-24 .. 0 dB); a final clamp at the ceiling catches what the 8-sample
// step lets through. z: 0 envelope, 1 the applied gain (Q15).
inline void limit(SlotState& s, const u8* p, i32* x) {
    const i32 in = static_cast<i32>(exp2_q16(p[0] * 4 * 65536 / 127) >> 4);    // Q12: 4096 .. 65536
    const i32 ceil = static_cast<i32>(exp2_q16((p[1] - 127) * 4 * 65536 / 127) >> 1);   // Q15 level
    const i32 release = tables::decay[60 + p[2] * 60 / 127] >> 15;            // Q15 per sample
    i32 env = s.z[0], gain = s.z[1] ? s.z[1] : 32767;
    for (u32 i = 0; i < kChunk; i += 8) {
        for (u32 j = i; j < i + 8; ++j) {
            x[j] = clamp((x[j] * in) >> 12, -(1 << 20), 1 << 20);
            const i32 a = iabs(x[j]);
            env = a > env ? a : mul_sh(env, release, 15);
        }
        const i32 target = env > ceil ? static_cast<i32>((static_cast<u32>(ceil) << 15) / static_cast<u32>(env)) : 32767;
        gain = target < gain ? target : gain + ((target - gain) >> 3);
        for (u32 j = i; j < i + 8; ++j) x[j] = clamp(mul_sh(x[j], gain, 15), -ceil, ceil);
    }
    s.z[0] = env; s.z[1] = gain;
}

inline bool needs_line(i32 type) {
    return type == kTape || type == kReverb || type == kChorus || type == kReson || type == kFreeze || type == kShift;
}
// Quiet chunks before the slot sleeps: long enough for its tail to come out.
inline i32 sleep_need(const SlotState& s) {
    switch (s.type) {
    case kDrive: case kFold: case kCrunch: case kBits: case kRing: case kClip: return 16;
    case kEq: case kFilter: case kPhaser: case kChorus: case kLimit: return 64;
    case kTape: return s.z[8] > 0 ? static_cast<i32>(((kLine << 16) / static_cast<u32>(s.z[8])) / kChunk) + 8 : kSleepLine;
    default: return kSleepLine;
    }
}
}

// A fresh start for a new type. The delay line is cleared over the next blocks
// (clearing), while the slot passes its input through.
inline void reset(SlotState& s, i32 type, u32 variant) {
    s.type = type; s.pos = 0; s.seed = variant ? 0x7f4a7c15u : 0x9e3779b9u; s.random = s.seed; s.quiet = 0;
    for (u32 i = 0; i < 12; ++i) s.z[i] = 0;
    for (u32 i = 0; i < 4; ++i) s.ph[i] = 0;
    s.z[11] = kMixUnset;
    s.clearing = detail::needs_line(type) ? kLineWords : 0;
    s.variant = variant;
}

// Clears up to `words` of the line still dirty from an earlier type; true once clean.
inline bool clear_line(SlotState& s, i16* line, u32 words) {
    opxy::i32* w = reinterpret_cast<opxy::i32*>(line);
    while (s.clearing && words) { w[--s.clearing] = 0; --words; }
    return !s.clearing;
}

// Processes one chunk of a slot channel in place.
inline void chunk(SlotState& s, i16* line, const Slot& slot, i32* io, i32 bpm10) {
    using namespace detail;
    i32 dry[kChunk];
    const i32 need = sleep_need(s);
    u32 ipk = 0, wpk = 0;
    for (u32 i = 0; i < kChunk; ++i) {
        const i32 v = sat(io[i]);
        dry[i] = io[i] = v;
        ipk |= static_cast<u32>(iabs(v));
    }
    if (!ipk && s.quiet >= need) return;   // asleep: the silent input passes
    switch (s.type) {
    case kEq: eq(s, slot.p, io); break;
    case kDrive: drive(s, slot.p, io); break;
    case kFold: fold(s, slot.p, io); break;
    case kCrunch: crunch(s, slot.p, io); break;
    case kBits: bits(s, slot.p, io); break;
    case kFilter: filter(s, slot.p, io); break;
    case kTape: tape(s, line, slot.p, io, bpm10); break;
    case kReverb: reverb(s, line, slot.p, io); break;
    case kChorus: chorus(s, line, slot.p, io); break;
    case kPhaser: phaser(s, slot.p, io); break;
    case kRing: ring(s, slot.p, io); break;
    case kReson: reson(s, line, slot.p, io); break;
    case kFreeze: freeze(s, line, slot.p, io); break;
    case kShift: shift(s, line, slot.p, io); break;
    case kClip: clip(s, slot.p, io); break;
    case kLimit: limit(s, slot.p, io); break;
    default: break;
    }
    s.pos += kChunk;
    const i32 mix = slot.mix > 127 ? 127 : slot.mix, t1 = (mix >= 127 ? 256 : mix * 2) << 8;
    const i32 m0 = s.z[11] == kMixUnset ? t1 : s.z[11], d = t1 - m0;
    const i32 m1 = d > -1024 && d < 1024 ? t1 : m0 + d / 4;   // MIX glides (~3 ms)
    i32 mq = m0;
    const i32 dm = (m1 - m0) / static_cast<i32>(kChunk);
    for (u32 i = 0; i < kChunk; ++i) {
        const i32 w = io[i];
        wpk |= static_cast<u32>(iabs(w));
        mq += dm;
        const i32 m = m1 != m0 ? mq : m1;
        io[i] = sat(dry[i] + (((w - dry[i]) * (m >> 8)) >> 8));
    }
    s.z[11] = m1;
    if (!ipk && wpk <= static_cast<u32>(kQuietLevel)) {
        if (s.quiet < need && ++s.quiet >= need)
            for (u32 i = 0; i < kStates[s.type]; ++i) s.z[i] = 0;
    } else {
        s.quiet = 0;
    }
}
}
