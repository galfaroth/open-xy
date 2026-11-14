// MultiFX (Veff fork), audio side: stock send effects as stages of a chain.
//
// Stock builds a send's effect in the track's 480-byte FX slot through the
// effect record's factory (record word 0, records relocated to 0x8d4cc380,
// 48 bytes each) and calls its vtable: +0x10 send process (self, left, right,
// record), +0x14 init (self, slot), +0x1c dry (self, record) -> Q31.
//
// Each send owns 1 MiB of scratch memory (bases at 0x84a50b90: [1] for send
// 14, [0] for send 15, 1 MiB apart), which stock's two allocators hand out
// whole, only from an effect's init: 8d50e048 (slot, bytes <= 512 KiB) ->
// base, and 8d50dfc0 (slot, &a, &b, words <= 131072) -> a = base, b = base +
// 4 * words. Delay takes 352800 bytes, reverb 237864, chorus 2 x 32 KiB. Both
// allocators are replaced here (profile hooks at their entries) with versions
// that add an offset while the chain initializes a stage, so several stages
// of one send each get their own part of its memory. Outside a chain stage's
// init the offset is 0 and they answer as stock does.
//
// A track's LFO (8d50f3a4, before the sends' effects run) modulates one word
// of the audio record through 8d518ec4(records, track, index, amount), which
// keeps the word's own value and index for stock's restore after the effects
// (8d518d36). A send LFO aimed at "param 1..4" names words 0..3, slot 1's;
// the call (hooked at 8d50f412) is sent on to the LFO slot's words instead
// while the send holds the chain.
#include <opxy/multifx.hpp>
#include "stock.hpp"

extern "C" {
opxy::u32 sdk_multifx_scratch(opxy::u32, opxy::u32) __asm__("sdk_multifx_scratch");
void sdk_multifx_scratch_pair(opxy::u32, opxy::u32*, opxy::u32*, opxy::u32) __asm__("sdk_multifx_scratch_pair");
void sdk_multifx_lfo(opxy::u32, opxy::u32, opxy::u32, opxy::i32) __asm__("sdk_multifx_lfo");
}

namespace {
using opxy::u32;
using opxy::i32;
using namespace opxy::platform;
using opxy::multifx::kScratchBytes;
const u32 kScratchBases = 0x84a50b90;
const u32 kEffectRecords = 0x8d4cc380, kRecordBytes = 48, kEffectRecordCount = 63;
const u32 kAudioRecords = 0x84eab39c, kRecordStride = 0xe8;
const u32 kMaxRequest = 524288;

// The stage being initialized: its send's offset and what it took.
bool initializing;
u32 offset, limit, taken;
bool overflow;
// Blocks for which each send still counts as holding the chain (mark_chain).
volatile opxy::u8 chain_blocks[2];

u32 base(u32 slot) { return word(kScratchBases + 4 * (slot == 14 ? 1 : 0)); }
bool send(u32 slot) { return slot == 14 || slot == 15; }

// The address a request of `bytes` gets: stock's base, or a chain stage's part.
u32 place(u32 slot, u32 bytes) {
    if (!initializing) return base(slot);
    const u32 at = (offset + taken + 31) & ~31u;
    if (at + bytes > limit) {
        // Never hand out memory past the stage's part: it overlaps the send's
        // start instead, and the chain disables it.
        overflow = true;
        return base(slot);
    }
    taken = at + bytes - offset;
    return base(slot) + at;
}
using Method = void (*)();
const Method* vtable(void* object) { return *static_cast<const Method* const*>(object); }
}

u32 sdk_multifx_scratch(u32 slot, u32 bytes) {
    // Stock asserts on another slot or a larger request; give it the base.
    if (!send(slot) || bytes > kMaxRequest) return send(slot) ? base(slot) : base(15);
    return place(slot, bytes);
}

void sdk_multifx_scratch_pair(u32 slot, u32* a, u32* b, u32 words) {
    const u32 at = send(slot) && words <= kMaxRequest / 4 ? place(slot, 8 * words) : base(send(slot) ? slot : 15);
    *a = at;
    *b = at + 4 * words;
}

void sdk_multifx_lfo(u32 records, u32 track, u32 index, i32 amount) {
    if ((track == 14 || track == 15) && index < 4 && chain_blocks[track - 14]) {
        chain_blocks[track - 14] = static_cast<opxy::u8>(chain_blocks[track - 14] - 1);
        const i32* record = at<const i32>(records + track * kRecordStride);
        index = opxy::multifx::kParamWord[opxy::multifx::lfo_slot(record[opxy::multifx::kLfoWord])][index];
    }
    stock<void(u32, u32, u32, i32)>(0x8d518ec4)(records, track, index, amount);
}

namespace opxy { namespace multifx {
void mark_chain(u32 send) { if (send == 14 || send == 15) chain_blocks[send - 14] = 4; }

u32 send_of(const i32* record) {
    const u32 address = static_cast<u32>(reinterpret_cast<Address>(record));
    for (u32 slot = 14; slot <= 15; ++slot)
        if (address == kAudioRecords + slot * kRecordStride) return slot;
    return 0;
}

bool stage_create(u32 type, void* storage) {
    if (type >= kEffectRecordCount || !storage) return false;
    void* (*factory)(void*) = at<void*(void*)>(word(kEffectRecords + type * kRecordBytes));
    if (!factory) return false;
    return factory(storage) == storage;
}

u32 stage_init(void* object, u32 slot, u32 from, u32 end) {
    initializing = true; offset = from; taken = 0; overflow = false;
    limit = end < kScratchBytes ? end : kScratchBytes;
    reinterpret_cast<void (*)(void*, u32)>(vtable(object)[5])(object, slot);
    initializing = false;
    return overflow ? 0xffffffffu : taken;
}

void stage_process(void* object, i32* left, i32* right, const i32* record) {
    reinterpret_cast<void (*)(void*, i32*, i32*, const i32*)>(vtable(object)[4])(object, left, right, record);
}

i32 stage_dry(void* object, const i32* record) {
    return reinterpret_cast<i32 (*)(void*, const i32*)>(vtable(object)[7])(object, record);
}
}}
