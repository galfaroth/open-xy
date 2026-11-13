// MultiFX (Veff fork), GUI side. On a send whose effect is the MultiFX chain:
// - Stop + track key 1..4 (or Shift + Stop + 1..8) selects the slot the four
//   encoders edit, and a popup names it and its effect;
// - E1..E4 edit the slot's four parameters through stock's editor (8d5ad5bc),
//   so stock steps, locks (with a step held) and publishes them as any
//   send parameter: the slots' words are all below 42 (opxy/multifx.hpp);
// - Shift + E1 steps the slot's effect (none, delay, reverb, chorus, phaser,
//   dist, lofi), stored in the send's type word through stock's set;
// - titles are the slot's effect's own parameter names.
// Stock does nothing with Stop + a track key on a send page.
#include "multifx_ui.hpp"
#include <opxy/multifx.hpp>
#include "stock.hpp"

namespace opxy { namespace platform { namespace multifx_ui {
namespace {
using namespace opxy::multifx;
const u32 kAuxController = 0x0803fb6c;   // the send page's controller vtable
const u32 kProject = 0x86bd027c;
const u32 kStop = 52, kShift = 55, kFirstTrackKey = 14;
const i32 kMiddle = 0x40000000;

u8 selected[2];             // the slot each send's encoders edit
bool blocked[72];           // track keys whose press MultiFX took; their release too

// Whether effect `id` is the chain: its record (relocated to 0x8d4cc380, 48
// bytes each) carries its stock name inline at byte 16. The builder links no
// weak symbols, so the chain is found by name, not by its effect number.
bool chain_effect(u32 id) {
    const u32 kEffectRecords = 0x8d4cc380, kRecordBytes = 48, kCount = 63;
    if (id >= kCount) return false;
    const char name[] = "veffmultifx";
    const volatile unsigned char* stored = at<const volatile unsigned char>(kEffectRecords + id * kRecordBytes + 16);
    for (u32 i = 0; i < sizeof name; ++i)   // with its terminator
        if (stored[i] != static_cast<unsigned char>(name[i])) return false;
    return true;
}
u32 top_controller_vtable() { return *stock<const u32*()>(0x8c0944b0)(); }
bool held(u32 key) { return stock<u32(u32)>(0x8d5b7d7c)(key) != 0; }
i32 current(u32 track, u32 index) {
    return *reinterpret_cast<const volatile i32*>(stock<u32(u32)>(0x8d587708)(track) + 0x3864 + index * 4);
}
void notify(const char* text) { stock<void(const char*, u32, u32, u32)>(0x8d549168)(text, 1, 0, 0x3f000000u); }
u32 slot_of(u32 track) { return selected[track - 14] < kSlots ? selected[track - 14] : 0; }
u32 slot_choice(u32 track, u32 slot) { return choice(current(track, kTypeWord), slot); }

// "SLOT 2 DELAY"
void announce(u32 track, u32 slot) {
    char text[24];
    const char* name = kChoiceName[slot_choice(track, slot)];
    u32 n = 0;
    for (const char* p = "SLOT "; *p; ++p) text[n++] = *p;
    text[n++] = static_cast<char>('1' + slot);
    text[n++] = ' ';
    for (const char* p = name; *p && n < sizeof(text) - 1; ++p) text[n++] = *p;
    text[n] = 0;
    notify(text);
}
}

bool on(u32 track) {
    return (track == 14 || track == 15) && chain_effect(track_engine(track));
}

u32 shown_word(u32 track, u32 position) { return kParamWord[slot_of(track)][position & 3]; }

u32 turn(u32 project, u32 track, u32 position, u32 event) {
    if (position >= 4) return stock<u32(u32, u32, u32, u32)>(0x8d5ad5bc)(project, track, position, event);
    const u32 slot = slot_of(track);
    if (shift_held()) {
        // Shift + E1: the slot's effect. The other encoders do nothing with Shift.
        if (position != 0 || !event) return 1;
        const i32 delta = reinterpret_cast<const i32*>(event)[0];
        const u32 now = slot_choice(track, slot);
        const u32 next = delta > 0 ? (now + 1 < kChoices ? now + 1 : now) : delta < 0 ? (now ? now - 1 : 0) : now;
        if (next == now) return 1;
        u32 packed = static_cast<u32>(current(track, kTypeWord));
        packed = (packed & ~(0xffu << (8 * slot))) | (next << (8 * slot));
        // A new effect starts from the middle of each of its parameters.
        for (u32 j = 0; j < 4; ++j) set_parameter(project, track, kParamWord[slot][j], kMiddle);
        set_parameter(project, track, kTypeWord, static_cast<i32>(packed));
        announce(track, slot);
        return 1;
    }
    return stock<u32(u32, u32, u32, u32)>(0x8d5ad5bc)(project, track, kParamWord[slot][position], event);
}

const char* title(u32 track, u32 position) {
    if (shift_held()) return position == 0 ? "effect" : "";
    const u32 c = slot_choice(track, slot_of(track));
    if (c == kNone) return position == 0 ? "none" : "";
    // The stock effect's own name for the parameter (its record's callback).
    return stock<const char*(u32, u32)>(0x8d506c90)(kStockType[c], position);
}

void caption(u32 track, u32 position, char* text) {
    const u32 slot = slot_of(track);
    if (slot_choice(track, slot) == kNone) { text[0] = '-'; text[1] = '-'; text[2] = 0; return; }
    const i32 value = current(track, kParamWord[slot][position & 3]);
    const u32 n = value < 0 ? 0 : (static_cast<u32>(value) >> 8) * 100 >> 23;
    text[0] = static_cast<char>('0' + n / 10 % 10);
    text[1] = static_cast<char>('0' + n % 10);
    text[2] = 0;
}

bool raw_event(const void* raw) {
    const u32* words = static_cast<const u32*>(raw);
    const u32 key = words[0];
    const bool down = words[1] != 0;
    if (key >= 72) return false;
    if (!down) {
        if (!blocked[key]) return false;
        blocked[key] = false;
        return true;
    }
    if (blocked[key]) return true;   // a repeat of a press MultiFX took
    if (key < kFirstTrackKey || key >= kFirstTrackKey + 8 || !held(kStop)) return false;
    const u32 track = selected_track();
    if (!on(track) || top_controller_vtable() != kAuxController) return false;
    const u32 slot = key - kFirstTrackKey;
    if (slot >= 4 && !held(kShift)) return false;
    blocked[key] = true;
    if (slot >= kSlots) { notify("4 SLOTS FOR NOW"); return true; }
    selected[track - 14] = static_cast<u8>(slot);
    announce(track, slot);
    return true;
}
}}}

extern "C" void sdk_multifx_startup() __asm__("sdk_multifx_startup");
void sdk_multifx_startup() {
    namespace ui = opxy::platform::multifx_ui;
    sdk_multifx_ui = opxy::platform::MultiFxUi{ui::on, ui::shown_word, ui::turn, ui::title, ui::caption};
    sdk_multifx_raw_event = ui::raw_event;
}
