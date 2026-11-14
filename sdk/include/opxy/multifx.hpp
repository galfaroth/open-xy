#pragma once
// MultiFX (Veff fork): a send effect that runs a chain of stock OP-XY effects
// in series. The platform owns the stock calls; the chain host machine
// (veff/machines/veffmultifx) and the send page's controls share this layout.
#include "types.hpp"

namespace opxy { namespace multifx {
const u32 kSlots = 8;
// The send track words that hold each slot's choice, one byte per slot:
// slots 1..4 in word 47, 5..8 in word 46. Stock reads words 42..47 only for
// instruments' modulation, never for a send, and saves them with the pattern.
const u32 kTypeWords[2] = {47, 46};
inline u32 type_word(u32 slot) { return kTypeWords[slot / 4]; }
// The send track word whose low byte names the slot the send's LFO moves.
const u32 kLfoWord = 45;
// The send track words that hold each slot's four parameters: every send word
// below 42 that stock leaves alone on a send, so step locks reach them all.
// Stock uses 16 and 19 (filter), 23 (FX I to FX II), 24..27 (LFO), 36 (the
// SDK's shift row), 40 (pan) and 41 (volume). The send LFO's targets are
// words 0..3 (a hook moves them to the LFO's slot), 16..19, 40 and 41; 17
// and 18 are its unnamed targets.
const u8 kParamWord[kSlots][4] = {
    {0, 1, 2, 3}, {4, 5, 6, 7}, {8, 9, 10, 11}, {12, 13, 14, 15},
    {20, 21, 22, 28}, {29, 30, 31, 32}, {33, 34, 35, 37}, {38, 39, 17, 18}};
// A slot's effect, as an effect record number (stock's and the SDK's share
// one table), or kNone. The slot's byte holds 0 for none, 0x40 | the number
// for any installed effect, or 1..6 for stock's six send effects as the first
// prototype stored them (delay, reverb, chorus, phaser, dist, lofi). Bit 7
// stays clear: slots 4 and 8 hold their word's top byte, and stock's setter
// clamps a negative word, which would clear all four slots in it. An earlier
// build's 0x80 | number still reads.
const u32 kNone = 0xff;
const u32 kEffects = 63;
const u32 kStockEffects = 6;
const u8 kStockEffect[kStockEffects] = {0, 5, 12, 13, 14, 15};
inline bool stock_effect(u32 effect) {
    for (u32 i = 0; i < kStockEffects; ++i) if (kStockEffect[i] == effect) return true;
    return false;
}
inline u32 slot_byte(u32 effect) { return effect < kEffects ? 0x40 | effect : 0; }
// A stock FX object's size: stock builds every effect in a 480-byte slot.
const u32 kObjectBytes = 480;
// Each send's scratch memory, which the chain divides among its stages.
const u32 kScratchBytes = 1048576;

// Slot `slot`'s effect from its type word (type_word(slot)), or kNone.
inline u32 choice(i32 word, u32 slot) {
    const u32 b = (static_cast<u32>(word) >> (8 * (slot & 3))) & 0xff;
    if (b & 0xc0) return (b & 0x3f) < kEffects ? b & 0x3f : kNone;
    return b >= 1 && b <= kStockEffects ? kStockEffect[b - 1] : kNone;
}
// The slot the send's LFO moves, from word kLfoWord.
inline u32 lfo_slot(i32 word) {
    const u32 slot = static_cast<u32>(word) & 0xff;
    return slot < kSlots ? slot : 0;
}
inline i32 with_lfo(i32 word, u32 slot) {
    return static_cast<i32>((static_cast<u32>(word) & ~0xffu) | (slot & 0xff));
}
// Scramble's range, the fewest and most effects it puts in the chain, in bits
// 8..11 and 12..15 of word kLfoWord (0 stands for the default, 1 and 7).
const u32 kScrambleMin = 1, kScrambleMax = 7;
inline u32 scramble_min(i32 word) {
    const u32 n = (static_cast<u32>(word) >> 8) & 0xf;
    return n >= 1 && n <= kSlots ? n : kScrambleMin;
}
inline u32 scramble_max(i32 word) {
    const u32 n = (static_cast<u32>(word) >> 12) & 0xf, low = scramble_min(word);
    const u32 high = n >= 1 && n <= kSlots ? n : kScrambleMax;
    return high < low ? low : high;
}
inline i32 with_range(i32 word, u32 low, u32 high) {
    return static_cast<i32>((static_cast<u32>(word) & ~0xff00u) | ((low & 0xf) << 8) | ((high & 0xf) << 12));
}
// The scratch bytes a stock effect's stage takes (delay, reverb, chorus); the
// others take none. A scramble keeps the chain within a send's memory.
inline u32 stock_bytes(u32 effect) {
    return effect == 0 ? 352800 : effect == 5 ? 237864 : effect == 12 ? 65536 : 0;
}
// A scramble's chain: `count` effects in slots 1..count, drawn from `list`
// (`size` installed effects, no kNone), within kScratchBytes; the rest none.
// `next(span)` returns 0 .. span - 1. Returns the number placed.
template <class Next>
u32 scramble_plan(u32 low, u32 high, const u8* list, u32 size, Next&& next, u8 (&out)[kSlots]) {
    for (u32 k = 0; k < kSlots; ++k) out[k] = static_cast<u8>(kNone);
    if (!size) return 0;
    if (high > kSlots) high = kSlots;
    if (low < 1) low = 1;
    if (low > high) low = high;
    const u32 count = low + next(high - low + 1);
    u32 left = kScratchBytes - kSlots * 32;   // each stage starts 32-byte aligned
    for (u32 k = 0; k < count; ++k) {
        u8 pick = list[next(size)];
        for (u32 tries = 0; stock_bytes(pick) > left && tries < 16; ++tries) pick = list[next(size)];
        if (stock_bytes(pick) > left)   // fall back to one that takes no scratch
            for (u32 i = 0; i < size; ++i) if (!stock_bytes(list[i])) { pick = list[i]; break; }
        if (stock_bytes(pick) > left) return k;
        left -= stock_bytes(pick);
        out[k] = pick;
    }
    return count;
}

// Audio side, implemented by the platform. `record` is the send track's
// parameter record a send effect's process receives.
// The send (14 or 15) whose record this is, or 0.
u32 send_of(const i32* record);
// Builds stock effect `type` in `storage` (kObjectBytes, word aligned); false if none.
bool stage_create(u32 type, void* storage);
// Initializes a built stage for send `slot`, its scratch memory starting
// `offset` bytes into the send's and ending before `limit`. Returns the
// scratch bytes it took, or 0xffffffff if they did not fit.
u32 stage_init(void* object, u32 slot, u32 offset, u32 limit);
// One 64-frame block: the stage's wet signal in place, and its dry level.
void stage_process(void* object, i32* left, i32* right, const i32* record);
i32 stage_dry(void* object, const i32* record);
// Marks send `send` (14 or 15) as holding the chain for the next few blocks:
// meanwhile its LFO moves the LFO slot's words instead of words 0..3.
void mark_chain(u32 send);

// GUI side, implemented by the platform: what send `track`'s page shows. The
// slot the encoders edit, the LFO's slot, each slot's choice, and the edited slot's four
// parameter titles and 0..99 values ("--" without an effect). False when the
// send does not hold the chain.
struct View {
    u8 slot;
    u8 lfo;   // the slot the send's LFO moves
    u8 type[kSlots];        // each slot's effect, or kNone
    char name[kSlots][10];  // each slot's effect's name, upper case, "-" for none
    const char* title[4];
    char value[4][8];
};
bool view(u32 track, View& out);
}}
