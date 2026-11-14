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
u32 stage_init(void* object, u32, u32 offset, u32 limit) {
    const u32 type = static_cast<u32*>(object)[0];
    const u32 bytes = type == 0 ? 352800 : type == 5 ? 237864 : type == 12 ? 65536 : 0;
    if (init_count < 4) last_offsets[init_count] = offset;
    ++init_count; ++inits;
    return offset + bytes > limit ? 0xffffffffu : bytes;
}
void stage_process(void* object, i32* left, i32* right, const i32* record) {
    const u32* o = static_cast<u32*>(object);
    for (u32 i = 0; i < 64; ++i) { left[i] = left[i] / 8 * static_cast<i32>(o[1]); right[i] = right[i] / 8 * static_cast<i32>(o[1]); }
    // Remember the parameters the stage was given, by its scale's slot.
    static u32 calls = 0;
    std::memcpy(seen_params[calls++ % 4], record, 16);
}
i32 stage_dry(void*, const i32*) { return 0x40000000; }   // one half
u32 marked;
void mark_chain(u32 send) { marked = send; }
}}

int main() {
    using namespace opxy::multifx;
    // A slot's byte: 0 none, 1..6 stock's six as first stored, 0x40 | any effect's number
    // (0x80 | number from an earlier build still reads).
    require(choice(0, 0) == kNone && choice(1, 0) == 0 && choice(2 << 8, 1) == 5 && choice(6 << 24, 3) == 15,
            "the first prototype's bytes still name stock's effects");
    require(choice(0x80 | 44, 0) == 44 && choice(0xff, 0) == kNone && choice(7, 0) == kNone && slot_byte(44) == (0x40 | 44) &&
            choice(0x40 | 44, 0) == 44 && choice(static_cast<i32>(slot_byte(44) << 24), 3) == 44 && static_cast<i32>(slot_byte(62) << 24) > 0,
            "any installed effect by its number");
    // Scramble's range lives above the LFO's slot in word 45; 0 is the default 1..7.
    require(scramble_min(0) == 1 && scramble_max(0) == 7 && lfo_slot(with_range(5, 2, 4)) == 5 &&
            scramble_min(with_range(5, 2, 4)) == 2 && scramble_max(with_range(5, 2, 4)) == 4 &&
            scramble_max(with_range(0, 6, 3)) == 6 && scramble_min(with_lfo(with_range(0, 3, 8), 7)) == 3 &&
            lfo_slot(with_lfo(with_range(0, 3, 8), 7)) == 7 && scramble_max(with_lfo(with_range(0, 3, 8), 7)) == 8,
            "scramble's range and the LFO's slot share word 45");
    {
        // Every effect a delay: only two fit a send's memory, the rest fall back to lofi.
        const u8 delays[] = {0, 0, 0, 15};
        u32 rolls = 0;
        auto pick_first = [&rolls](u32 span) { ++rolls; return span ? 0u : 0u; };
        u8 chain[kSlots];
        const u32 n = scramble_plan(8, 8, delays, 4, pick_first, chain);
        u32 delay_count = 0;
        for (u32 k = 0; k < kSlots; ++k) delay_count += chain[k] == 0;
        require(n == 8 && delay_count == 2 && chain[7] == 15, "a scramble keeps within the send's memory");
        u32 x = 12345;
        auto rnd = [&x](u32 span) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return span ? ((x >> 16) * span) >> 16 : 0u; };
        const u8 all[] = {0, 5, 12, 13, 14, 15};
        for (u32 t = 0; t < 200; ++t) {
            const u32 m = scramble_plan(2, 5, all, 6, rnd, chain);
            u32 bytes = 0, filled = 0;
            for (u32 k = 0; k < kSlots; ++k) if (chain[k] != kNone) { bytes += stock_bytes(chain[k]); ++filled; if (k >= m) filled = 99; }
            require(m >= 2 && m <= 5 && filled == m && bytes <= kScratchBytes, "a scramble's count stays in its range, in slots 1..n");
        }
    }
    ::veffmultifx::State state;
    ::veffmultifx::initialize(state);
    i32 l[64], r[64];
    // No stage: the send returns silence.
    for (u32 i = 0; i < 64; ++i) l[i] = r[i] = 1 << 28;
    ::veffmultifx::process(state, l, r, records[0]);
    for (u32 i = 0; i < 64; ++i) require(!l[i] && !r[i], "an empty chain returns nothing");

    // Slot 1 delay (choice 1), slot 3 reverb (choice 2): each slot's own words reach its stage.
    records[0][kTypeWords[0]] = 1 | (2 << 16);
    for (u32 k = 0; k < kSlots; ++k)
        for (u32 j = 0; j < 4; ++j) records[0][kParamWord[k][j]] = static_cast<i32>(100 * k + j);
    init_count = 0;
    for (u32 i = 0; i < 64; ++i) l[i] = r[i] = 1 << 28;
    ::veffmultifx::process(state, l, r, records[0]);
    require(init_count == 2, "two stages built");
    require(marked == 14, "the send is marked as holding the chain");
    require(last_offsets[0] == 0 && last_offsets[1] == 352800, "the second stage's memory follows the first's");
    // Delay: wet = x * 2/8 (first stage: wet only). Reverb: wet * 3/8 + chain * 1/2.
    const i32 x = 1 << 28, a = x / 8 * 2, expect = a / 8 * 3 + (a >> 1);
    for (u32 i = 0; i < 64; ++i) require(l[i] >= expect - 2 && l[i] <= expect + 2 && r[i] == l[i], "stages run in series");
    require(seen_params[0][0] == 0 && seen_params[0][3] == 3, "slot 1 reads words 0..3");
    require(seen_params[1][0] == 200 && seen_params[1][3] == 203, "slot 3 reads its own words");

    // Same choices: nothing rebuilt.
    inits = 0;
    ::veffmultifx::process(state, l, r, records[0]);
    require(inits == 0, "unchanged choices keep their stages");
    // Slot 2 gets a chorus: built alone in free memory, slots 1 and 3 untouched.
    records[0][kTypeWords[0]] = 1 | (3 << 8) | (2 << 16);
    init_count = 0;
    ::veffmultifx::process(state, l, r, records[0]);
    require(init_count == 1 && state.ready[0] && state.ready[1] && state.ready[2], "a changed slot alone is built");
    require(state.at[0] == 0 && state.at[2] == 352800, "the other stages keep their memory");
    require(state.at[1] >= state.at[2] + state.size[2], "the new stage takes free memory");
    // Three delays: the third does not fit and stays off; the others play on.
    records[0][kTypeWords[0]] = 1 | (1 << 8) | (1 << 16);
    init_count = 0;
    ::veffmultifx::process(state, l, r, records[0]);
    require(state.ready[0] && state.ready[1] && !state.ready[2], "a stage past the send's memory stays off");
    require(state.at[0] == 0 && init_count == 1, "slot 1 is never rebuilt");
    // Slot 2 back to none frees its memory: slot 3's delay now fits.
    records[0][kTypeWords[0]] = 1 | (1 << 16);
    init_count = 0;
    ::veffmultifx::process(state, l, r, records[0]);
    require(state.ready[0] && !state.ready[1] && state.ready[2] && init_count == 1, "a slot that did not fit takes freed memory");
    // An engine reset builds every stage again, in slot order.
    records[0][kTypeWords[0]] = 1 | (1 << 8) | (1 << 16);
    ::veffmultifx::initialize(state);
    inits = 0;
    ::veffmultifx::process(state, l, r, records[0]);
    require(inits == 2 && state.ready[0] && state.ready[1] && !state.ready[2], "an engine reset rebuilds the stages");
    // A record that is no send's: silence.
    i32 other[::veffmultifx::kRecordWords] = {0};
    for (u32 i = 0; i < 64; ++i) l[i] = r[i] = 1 << 28;
    ::veffmultifx::process(state, l, r, other);
    for (u32 i = 0; i < 64; ++i) require(!l[i] && !r[i], "outside a send: silence");
    std::puts("MultiFX host checks passed");
    return 0;
}
