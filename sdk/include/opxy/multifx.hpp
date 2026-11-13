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
// A slot's choices: none, then stock's send effects by their stock type IDs.
const u32 kChoices = 7;
const u32 kNone = 0;
const u32 kStockType[kChoices] = {19, 0, 5, 12, 13, 14, 15};
const char* const kChoiceName[kChoices] = {"NONE", "DELAY", "REVERB", "CHORUS", "PHASER", "DIST", "LOFI"};
// A stock FX object's size: stock builds every effect in a 480-byte slot.
const u32 kObjectBytes = 480;
// Each send's scratch memory, which the chain divides among its stages.
const u32 kScratchBytes = 1048576;

// Slot `slot`'s choice from its type word (type_word(slot)).
inline u32 choice(i32 word, u32 slot) {
    const u32 c = (static_cast<u32>(word) >> (8 * (slot & 3))) & 0xff;
    return c < kChoices ? c : kNone;
}
// The slot the send's LFO moves, from word kLfoWord.
inline u32 lfo_slot(i32 word) {
    const u32 slot = static_cast<u32>(word) & 0xff;
    return slot < kSlots ? slot : 0;
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
    u8 type[kSlots];
    const char* title[4];
    char value[4][8];
};
bool view(u32 track, View& out);
}}
