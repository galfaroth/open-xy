// MultiFX: up to eight stock send effects in series on one send. The send
// page (the Veff fork's platform) picks each slot's effect and edits its
// parameters, which live in the send track's own words; this host builds the
// stages and runs them.
//
// Changing one slot's effect builds that slot's stage alone, in a free part
// of the send's memory, so the other stages keep their tails.
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
// The scratch bytes each choice's stage took when last built (unknown until
// then, except stock's delay, reverb and chorus, read from their inits).
const u32 kUnknown = 0xffffffffu;
u32 bytes_of[kChoices] = {0, 352800, 237864, 65536, kUnknown, kUnknown, kUnknown};

void* object(u32 send, u32 slot) { return objects[send - 14][slot]; }
u32 aligned(u32 at) { return (at + 31) & ~31u; }

// The free stretch of the send's memory, outside every ready stage but `slot`'s,
// that starts at or after `from`: [start, end).
void gap_after(const State& s, u32 slot, u32 from, u32& start, u32& end) {
    start = aligned(from);
    bool moved = true;
    while (moved) {   // skip past any stage that covers the start
        moved = false;
        for (u32 k = 0; k < kSlots; ++k)
            if (k != slot && s.ready[k] && s.size[k] && start >= s.at[k] && start < s.at[k] + s.size[k]) {
                start = aligned(s.at[k] + s.size[k]); moved = true;
            }
    }
    end = kScratchBytes;
    for (u32 k = 0; k < kSlots; ++k)
        if (k != slot && s.ready[k] && s.size[k] && s.at[k] >= start && s.at[k] < end) end = s.at[k];
}

// Builds slot `k`'s stage for `choice` in the first free stretch it fits,
// leaving every other stage (and its tail) as it is.
void build_slot(State& s, u32 k, u32 c) {
    s.type[k] = static_cast<u8>(c);
    s.ready[k] = 0; s.at[k] = s.size[k] = 0;
    if (c == kNone || !stage_create(kStockType[c], object(s.send, k))) return;
    const u32 need = bytes_of[c];
    u32 start = 0, end = 0, best = kScratchBytes, best_end = kScratchBytes;
    for (u32 from = 0; from < kScratchBytes; from = end + 1) {
        gap_after(s, k, from, start, end);
        if (start >= end) break;
        if (need != kUnknown ? end - start >= need : best == kScratchBytes || end - start > best_end - best) {
            best = start; best_end = end;
            if (need != kUnknown) break;   // the first stretch it fits
        }
    }
    if (best >= best_end) return;   // no room left in the send's memory
    const u32 bytes = stage_init(object(s.send, k), s.send, best, best_end);
    if (bytes == 0xffffffffu) return;
    bytes_of[c] = bytes;
    s.at[k] = best; s.size[k] = bytes; s.ready[k] = 1;
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
    mark_chain(send);
    if (!s.built || s.send != send) {
        // A new send or an engine reset: every stage from scratch, in slot order.
        s.send = send;
        for (u32 k = 0; k < kSlots; ++k) s.ready[k] = 0;
        for (u32 k = 0; k < kSlots; ++k) build_slot(s, k, choice(record[type_word(k)], k));
        s.built = 1;
    } else {
        // A changed slot alone: the others keep playing, tails and all. Then
        // a slot that found no room before tries again in what was freed.
        bool changed = false;
        for (u32 k = 0; k < kSlots; ++k) {
            const u32 c = choice(record[type_word(k)], k);
            if (c != s.type[k]) { s.ready[k] = 0; build_slot(s, k, c); changed = true; }
        }
        if (changed)
            for (u32 k = 0; k < kSlots; ++k)
                if (!s.ready[k] && s.type[k] != kNone) build_slot(s, k, s.type[k]);
    }

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
