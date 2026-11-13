#pragma once
// MultiFX (Veff fork): a send effect that runs a chain of stock OP-XY effects
// in series. The platform owns the stock calls; the chain host machine
// (veff/machines/veffmultifx) and the send page's controls share this layout.
#include "types.hpp"

namespace opxy { namespace multifx {
const u32 kSlots = 4;
// The send track word that holds each slot's choice, one byte per slot.
const u32 kTypeWord = 47;
// The send track words that hold each slot's four parameters. All are below
// 42, so stock's parameter locks reach them.
const u8 kParamWord[kSlots][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {38, 39, 40, 41}, {28, 34, 35, 37}};
// A slot's choices: none, then stock's send effects by their stock type IDs.
const u32 kChoices = 7;
const u32 kNone = 0;
const u32 kStockType[kChoices] = {19, 0, 5, 12, 13, 14, 15};
const char* const kChoiceName[kChoices] = {"NONE", "DELAY", "REVERB", "CHORUS", "PHASER", "DIST", "LOFI"};
// A stock FX object's size: stock builds every effect in a 480-byte slot.
const u32 kObjectBytes = 480;
// Each send's scratch memory, which the chain divides among its stages.
const u32 kScratchBytes = 1048576;

inline u32 choice(i32 type_word, u32 slot) {
    const u32 c = (static_cast<u32>(type_word) >> (8 * slot)) & 0xff;
    return c < kChoices ? c : kNone;
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

// GUI side, implemented by the platform: what send `track`'s page shows. The
// slot the encoders edit, each slot's choice, and the edited slot's four
// parameter titles and 0..99 values ("--" without an effect). False when the
// send does not hold the chain.
struct View {
    u8 slot;
    u8 type[kSlots];
    const char* title[4];
    char value[4][8];
};
bool view(u32 track, View& out);
}}
