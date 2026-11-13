#pragma once
#include <opxy/effect.hpp>
#include "slots.h"

namespace labchain {
// Four slots in series, each a stereo pair of slot channels.
const u32 kSlots = 4, kChannels = 2;
const u32 kStateWords = sizeof(SlotState) / 4;
// The scratch buffer: every slot channel's state, then every slot channel's line.
const u32 kLinesAt = kSlots * kChannels * kStateWords;
const u32 kBufferWords = kLinesAt + kSlots * kChannels * kLineWords;
// Words of dirty line a slot channel clears per block after a type change.
const u32 kClearPerBlock = 1024;

// What the chain plays: each slot's type, parameters and MIX, and the slot
// the encoders edit.
struct Patch { Slot slot[kSlots]; u32 selected; };

// The twelve words stock passes: the four encoders (the selected slot's
// window), four unused, then the shift row's four choices (each slot's type).
// `extra`, when not null, carries every slot's values (the Veff fork's
// platform keeps them; see docs); until then the slots the encoders do not
// show keep their types' initial values and slot 1 is the one edited.
void decode(opxy::Parameters parameters, const i32* extra, Patch& patch);

struct State { u32 formatted; };
void initialize(State& state);
void process(State& state, opxy::Buffer buffer, const opxy::Transport& transport,
             opxy::Channel left, opxy::Channel right, opxy::Parameters parameters);
}
