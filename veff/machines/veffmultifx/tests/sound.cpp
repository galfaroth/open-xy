// MultiFX's host checks, with stand-ins for the platform's stock stages.
#include "../fx.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace opxy;
void require(bool value, const char* text) {
    if (!value) { std::fprintf(stderr, "FAIL %s\n", text); std::exit(1); }
}

// Two send records, as the audio side lays them out.
i32 records[2][::veffmultifx::kRecordWords];
// What the stand-in stages saw.
u32 inits, last_offsets[4], init_count;
i32 seen_params[4][4];

namespace opxy { namespace multifx {
u32 send_of(const i32* record) { return record == records[0] ? 14 : record == records[1] ? 15 : 0; }
// A stage object: its stock type, then its scale (wet = input * scale / 8).
bool stage_create(u32 type, void* storage) {
    u32* o = static_cast<u32*>(storage);
    o[0] = type; o[1] = type == 0 ? 2 : type == 5 ? 3 : 1;
    return true;
}
u32 stage_init(void* object, u32, u32 offset) {
    const u32 type = static_cast<u32*>(object)[0];
    const u32 bytes = type == 0 ? 352800 : type == 5 ? 237864 : type == 12 ? 65536 : 0;
    if (init_count < 4) last_offsets[init_count] = offset;
    ++init_count; ++inits;
    return offset + bytes > kScratchBytes ? 0xffffffffu : bytes;
}
void stage_process(void* object, i32* left, i32* right, const i32* record) {
    const u32* o = static_cast<u32*>(object);
    for (u32 i = 0; i < 64; ++i) { left[i] = left[i] / 8 * static_cast<i32>(o[1]); right[i] = right[i] / 8 * static_cast<i32>(o[1]); }
    // Remember the parameters the stage was given, by its scale's slot.
    static u32 calls = 0;
    std::memcpy(seen_params[calls++ % 4], record, 16);
}
i32 stage_dry(void*, const i32*) { return 0x40000000; }   // one half
}}

int main() {
    using namespace opxy::multifx;
    ::veffmultifx::State state;
    ::veffmultifx::initialize(state);
    i32 l[64], r[64];
    // No stage: the send returns silence.
    for (u32 i = 0; i < 64; ++i) l[i] = r[i] = 1 << 28;
    ::veffmultifx::process(state, l, r, records[0]);
    for (u32 i = 0; i < 64; ++i) require(!l[i] && !r[i], "an empty chain returns nothing");

    // Slot 1 delay (choice 1), slot 3 reverb (choice 2): each slot's own words reach its stage.
    records[0][kTypeWord] = 1 | (2 << 16);
    for (u32 k = 0; k < kSlots; ++k)
        for (u32 j = 0; j < 4; ++j) records[0][kParamWord[k][j]] = static_cast<i32>(100 * k + j);
    init_count = 0;
    for (u32 i = 0; i < 64; ++i) l[i] = r[i] = 1 << 28;
    ::veffmultifx::process(state, l, r, records[0]);
    require(init_count == 2, "two stages built");
    require(last_offsets[0] == 0 && last_offsets[1] == 352800, "the second stage's memory follows the first's");
    // Delay: wet = x * 2/8 (first stage: wet only). Reverb: wet * 3/8 + chain * 1/2.
    const i32 x = 1 << 28, a = x / 8 * 2, expect = a / 8 * 3 + (a >> 1);
    for (u32 i = 0; i < 64; ++i) require(l[i] >= expect - 2 && l[i] <= expect + 2 && r[i] == l[i], "stages run in series");
    require(seen_params[0][0] == 0 && seen_params[0][3] == 3, "slot 1 reads words 0..3");
    require(seen_params[1][0] == 200 && seen_params[1][3] == 203, "slot 3 reads its own words");

    // Same choices: nothing rebuilt. A change rebuilds the chain.
    inits = 0;
    ::veffmultifx::process(state, l, r, records[0]);
    require(inits == 0, "unchanged choices keep their stages");
    records[0][kTypeWord] = 1 | (1 << 8) | (1 << 16);   // three delays: the third does not fit
    init_count = 0;
    ::veffmultifx::process(state, l, r, records[0]);
    require(init_count == 3 && state.ready[0] && state.ready[1] && !state.ready[2], "a stage past the send's memory stays off");
    // An engine reset builds every stage again.
    ::veffmultifx::initialize(state);
    inits = 0;
    ::veffmultifx::process(state, l, r, records[0]);
    require(inits == 3, "an engine reset rebuilds the stages");
    // A record that is no send's: silence.
    i32 other[::veffmultifx::kRecordWords] = {0};
    for (u32 i = 0; i < 64; ++i) l[i] = r[i] = 1 << 28;
    ::veffmultifx::process(state, l, r, other);
    for (u32 i = 0; i < 64; ++i) require(!l[i] && !r[i], "outside a send: silence");
    std::puts("MultiFX host checks passed");
    return 0;
}
