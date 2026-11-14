// Lab Chain's sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>.
#include "../fx.cpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace opxy;
using labchain::kTypes;

void require(bool value, const char* text) {
    if (!value) { std::fprintf(stderr, "FAIL %s\n", text); std::exit(1); }
}

// A shift-row word choosing `c` among the types, as stock's enum buckets centre it.
i32 type_word(u32 c) { return static_cast<i32>((static_cast<i64>(2 * c + 1) * 0x7fffffff) / (2 * kTypes)); }

struct Rig {
    labchain::State state;
    std::vector<i32> memory;
    Buffer buffer;
    Transport transport;
    i32 p[12];
    Rig(i32 fill = 0x55555555) : memory(labchain::kBufferWords, fill) {
        buffer = Buffer{memory.data(), static_cast<u32>(memory.size())};
        transport = Transport();
        transport.bpm_x10 = 1200;
        for (u32 i = 0; i < 12; ++i) p[i] = i < 4 ? 0x3fffffff : 0;
        for (u32 k = 0; k < 4; ++k) p[8 + k] = type_word(labchain::kEq);
        labchain::initialize(state);
    }
    void run(i32* l, i32* r) { labchain::process(state, buffer, transport, l, r, p); }
};

int main() {
    // Four flat EQs pass the input unchanged (to Q15 precision), left and right
    // apart, even on a buffer full of an earlier user's data.
    {
        Rig rig;
        i32 l[64], r[64];
        for (u32 block = 0; block < 200; ++block) {
            i32 inl[64], inr[64];
            for (u32 i = 0; i < 64; ++i) {
                inl[i] = l[i] = static_cast<i32>(((block * 64 + i) * 7919u) % 50000u) * 65536 - 25000 * 65536;
                inr[i] = r[i] = (block & 1) ? 0 : 12345 * 65536;
            }
            rig.run(l, r);
            for (u32 i = 0; i < 64; ++i) require(l[i] == inl[i] && r[i] == inr[i], "flat EQs must be transparent");
        }
    }
    // The encoders reach the first slot through its type's roles; the shift
    // row picks each slot's type; the hidden slots keep their initial values.
    {
        i32 p[12] = {0x7fffffff, 0, 0x3fffffff, 0x1fffffff};
        p[8] = type_word(labchain::kReverb); p[9] = type_word(labchain::kDrive);
        p[10] = type_word(labchain::kOff); p[11] = type_word(labchain::kLimit);
        labchain::Patch patch;
        labchain::decode(p, nullptr, patch);
        require(patch.selected == 0, "without the platform's words slot 1 is edited");
        require(patch.slot[0].type == labchain::kReverb && patch.slot[1].type == labchain::kDrive &&
                patch.slot[2].type == labchain::kOff && patch.slot[3].type == labchain::kLimit, "shift row picks types");
        require(patch.slot[0].p[0] == 127 && patch.slot[0].p[1] == 0 && patch.slot[0].p[2] == 64 && patch.slot[0].mix == 32,
                "reverb encoders: SIZE DEC DAMP MIX");
        require(patch.slot[0].p[3] == 0, "reverb GATE keeps its initial value");
        require(patch.slot[1].p[0] == 50 && patch.slot[1].mix == 127, "a hidden slot keeps its type's initial values");
        // With the platform's words, every slot takes its own values and the window its selected slot.
        const i32 extra[5] = {2, 0, 0, 0x7f7f7f7f, 0};
        labchain::decode(p, extra, patch);
        require(patch.selected == 2, "the platform selects the slot the encoders edit");
        require(patch.slot[0].p[0] == 0 && patch.slot[0].mix == 0, "platform values reach unselected slots");
        require(patch.slot[2].type == labchain::kOff, "OFF stays off");
        for (u32 c = 0; c < kTypes; ++c) {
            i32 q[12] = {0}; q[8] = type_word(c);
            labchain::decode(q, nullptr, patch);
            require(patch.slot[0].type == c, "every shift-row choice decodes to its type");
        }
    }
    // Every type in slot 1: bounded on loud noise, silent after the input stops
    // once its tail ends, and dirty memory from an earlier user never sounds.
    for (u32 t = 0; t < kTypes; ++t) {
        Rig rig;
        rig.p[8] = type_word(t);
        for (u32 e = 0; e < 4; ++e) rig.p[e] = 0x5fffffff;
        i32 l[64], r[64];
        u32 seed = 1;
        // Silence first: the line is cleared before the type uses it.
        for (u32 block = 0; block < 16; ++block) {
            for (u32 i = 0; i < 64; ++i) l[i] = r[i] = 0;
            rig.run(l, r);
            for (u32 i = 0; i < 64; ++i) require(!l[i] && !r[i], "a fresh type on dirty memory must be silent");
        }
        bool sounded = false;
        for (u32 block = 0; block < 400; ++block) {
            for (u32 i = 0; i < 64; ++i) {
                seed = seed * 1664525u + 1013904223u;
                l[i] = static_cast<i32>(seed) >> 1;
                r[i] = -(l[i] >> 1);
            }
            rig.run(l, r);
            for (u32 i = 0; i < 64; ++i) {
                require(!(l[i] & 0xffff) && !(r[i] & 0xffff), "output stays on the Q15 grid");
                if (l[i] || r[i]) sounded = true;
            }
        }
        require(t == labchain::kOff || t == labchain::kFreeze || sounded, "every type passes or makes sound");
        bool quiet = false;
        for (u32 block = 0; block < 4000 && !quiet; ++block) {
            for (u32 i = 0; i < 64; ++i) l[i] = r[i] = 0;
            rig.run(l, r);
            quiet = true;
            for (u32 i = 0; i < 64; ++i) if (l[i] || r[i]) quiet = false;
        }
        // FREEZE with HOLD under 64 keeps re-reading its capture until it decays to silence too.
        if (!quiet) { std::fprintf(stderr, "type %s\n", labchain::types[t].name); }
        require(quiet, "every type's tail ends in exact silence");
    }
    // A full chain of the heaviest types stays bounded at full scale.
    {
        Rig rig;
        rig.p[8] = type_word(labchain::kReverb); rig.p[9] = type_word(labchain::kShift);
        rig.p[10] = type_word(labchain::kReson); rig.p[11] = type_word(labchain::kTape);
        for (u32 e = 0; e < 4; ++e) rig.p[e] = 0x7fffffff;
        i32 l[64], r[64];
        for (u32 block = 0; block < 3000; ++block) {
            for (u32 i = 0; i < 64; ++i) { l[i] = (i & 1) ? 0x7fffffff : -0x7fffffff - 1; r[i] = l[i] / 3; }
            rig.run(l, r);
        }
    }
    // No scratch memory (outside a send): silence.
    {
        labchain::State state; labchain::initialize(state);
        Transport transport = Transport();
        i32 l[64], r[64], p[12] = {0};
        for (u32 i = 0; i < 64; ++i) l[i] = r[i] = 1 << 30;
        labchain::process(state, Buffer{nullptr, 0}, transport, l, r, p);
        for (u32 i = 0; i < 64; ++i) require(!l[i] && !r[i], "missing scratch produces silence");
    }
    std::puts("Lab Chain sound checks passed");
    return 0;
}
