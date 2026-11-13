#include "core.h"

namespace labchain {
namespace {
// An encoder's word as 0..127, rounded so the halfway default is 64.
u8 knob(i32 word) {
    const u32 v = ((static_cast<u32>(word < 0 ? 0 : word) >> 23) + 1) >> 1;
    return static_cast<u8>(v > 127 ? 127 : v);
}
// A shift-row word's choice among kTypes.
u32 choice(i32 word) {
    const u32 c = ((static_cast<u32>(word < 0 ? 0 : word) >> 16) * kTypes) >> 15;
    return c < kTypes ? c : kTypes - 1;
}
// Puts the encoders' values on a slot through its type's roles.
void apply_window(Slot& slot, const u8* values) {
    const TypeInfo& t = types[slot.type];
    for (u32 e = 0; e < 4; ++e) {
        if (t.role[e] < 4) slot.p[t.role[e]] = values[e];
        else slot.mix = values[e];
    }
}
}

void decode(opxy::Parameters p, const i32* extra, Patch& patch) {
    patch.selected = 0;
    for (u32 k = 0; k < kSlots; ++k) patch.slot[k] = initial_slot(choice(p[8 + k]));
    if (extra) {
        patch.selected = static_cast<u32>(extra[0]) & 3;
        for (u32 k = 0; k < kSlots; ++k) {
            u8 values[4];
            for (u32 e = 0; e < 4; ++e) values[e] = static_cast<u8>((static_cast<u32>(extra[1 + k]) >> (8 * e)) & 127);
            apply_window(patch.slot[k], values);
        }
    }
    u8 window[4];
    for (u32 e = 0; e < 4; ++e) window[e] = knob(p[e]);
    apply_window(patch.slot[patch.selected], window);
}

void initialize(State& state) { state.formatted = 0; }

void process(State& state, opxy::Buffer buffer, const opxy::Transport& transport,
             opxy::Channel left, opxy::Channel right, opxy::Parameters parameters) {
    if (!buffer.words || buffer.count < kBufferWords) {
        state.formatted = 0;
        for (u32 i = 0; i < opxy::block_frames; ++i) left[i] = right[i] = 0;
        return;
    }
    SlotState* states = reinterpret_cast<SlotState*>(buffer.words);
    if (!state.formatted) {
        // The buffer holds whatever its last user left: every slot starts over.
        for (u32 i = 0; i < kSlots * kChannels; ++i) states[i].type = kTypes;
        state.formatted = 1;
    }
    Patch patch;
    decode(parameters, nullptr, patch);
    const i32 bpm10 = transport.bpm_x10 ? static_cast<i32>(transport.bpm_x10) : 1200;

    i32 audio[kChannels][opxy::block_frames];
    for (u32 i = 0; i < opxy::block_frames; ++i) { audio[0][i] = left[i] >> 16; audio[1][i] = right[i] >> 16; }
    for (u32 k = 0; k < kSlots; ++k) {
        const Slot& slot = patch.slot[k];
        // A flat EQ (every slot's initial type) is an exact bypass: no work at all.
        if (slot.type == kEq && slot.p[0] == 64 && slot.p[1] == 64 && slot.p[3] == 64 && slot.mix >= 127) {
            for (u32 c = 0; c < kChannels; ++c) if (states[k * kChannels + c].type != kEq) reset(states[k * kChannels + c], kEq, c);
            continue;
        }
        for (u32 c = 0; c < kChannels; ++c) {
            SlotState& s = states[k * kChannels + c];
            i16* line = reinterpret_cast<i16*>(buffer.words + kLinesAt + (k * kChannels + c) * kLineWords);
            if (s.type != slot.type) reset(s, slot.type, c);
            if (slot.type == kOff || !clear_line(s, line, kClearPerBlock)) continue;
            for (u32 at = 0; at < opxy::block_frames; at += kChunk) chunk(s, line, slot, audio[c] + at, bpm10);
        }
    }
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        left[i] = dsp::sat(audio[0][i]) * 65536;
        right[i] = dsp::sat(audio[1][i]) * 65536;
    }
}
}
