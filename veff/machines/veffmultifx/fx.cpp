// MultiFX: up to four stock send effects in series on one send. The send
// page (the Veff fork's platform) picks each slot's effect and edits its
// parameters, which live in the send track's own words; this host builds the
// stages and runs them.
//
// Stage 1 gets the send's input and contributes its wet signal only, as on a
// send of its own. Each later stage gets the chain so far and returns its wet
// signal plus its input at its dry level, so a delay or reverb after a
// distortion keeps the distortion beside its echoes.
#include "core.h"
#include <opxy/q31.hpp>

namespace veffmultifx {
namespace {
using namespace opxy::multifx;
// The stages' stock objects, per send and slot. Stock objects are never destroyed.
u32 objects[2][kSlots][kObjectBytes / 4];

void* object(u32 send, u32 slot) { return objects[send - 14][slot]; }

// Builds every stage again, dividing the send's scratch memory in slot order.
void build(State& s, const u8* choices) {
    u32 offset = 0;
    for (u32 k = 0; k < kSlots; ++k) {
        s.type[k] = choices[k];
        s.ready[k] = 0;
        if (choices[k] == kNone || !stage_create(kStockType[choices[k]], object(s.send, k))) continue;
        const u32 bytes = stage_init(object(s.send, k), s.send, offset);
        if (bytes == 0xffffffffu) continue;   // no room left in the send's memory
        offset += bytes;
        s.ready[k] = 1;
    }
    s.built = 1;
}
}

void initialize(State& state) {
    // Also an engine reset: every stage is built again on the next block.
    state.built = 0;
}

void process(State& s, opxy::Channel left, opxy::Channel right, opxy::Parameters record) {
    const u32 send = send_of(record);
    if (!send) {
        for (u32 i = 0; i < opxy::block_frames; ++i) left[i] = right[i] = 0;
        return;
    }
    u8 choices[kSlots];
    bool same = s.built && s.send == send;
    for (u32 k = 0; k < kSlots; ++k) {
        choices[k] = static_cast<u8>(choice(record[kTypeWord], k));
        same = same && choices[k] == s.type[k];
    }
    if (!same) { s.send = send; build(s, choices); }

    i32 stage_record[kRecordWords];
    for (u32 i = 0; i < kRecordWords; ++i) stage_record[i] = record[i];
    i32 wet_left[opxy::block_frames], wet_right[opxy::block_frames];
    bool first = true;
    for (u32 k = 0; k < kSlots; ++k) {
        if (!s.ready[k]) continue;
        for (u32 j = 0; j < 4; ++j) stage_record[j] = record[kParamWord[k][j]];
        for (u32 i = 0; i < opxy::block_frames; ++i) { wet_left[i] = left[i]; wet_right[i] = right[i]; }
        void* stage = object(send, k);
        stage_process(stage, wet_left, wet_right, stage_record);
        const i32 dry = first ? 0 : stage_dry(stage, stage_record);
        for (u32 i = 0; i < opxy::block_frames; ++i) {
            left[i] = opxy::q31::add(wet_left[i], opxy::q31::multiply(left[i], dry));
            right[i] = opxy::q31::add(wet_right[i], opxy::q31::multiply(right[i], dry));
        }
        first = false;
    }
    // No stage: the send returns nothing, as a send without an effect.
    if (first) for (u32 i = 0; i < opxy::block_frames; ++i) left[i] = right[i] = 0;
}
}
