// MultiFX (Veff fork), GUI side. On a send whose effect is the MultiFX chain:
// - Stop + track key 1..8 selects the slot the four
//   encoders edit, and a popup names it and its effect;
// - E1..E4 edit the slot's four parameters through stock's editor (8d5ad5bc),
//   so stock steps, locks (with a step held) and publishes them as any
//   send parameter: the slots' words are all below 42 (opxy/multifx.hpp);
// - Shift + E1 steps the slot's effect (none, delay, reverb, chorus, phaser,
//   dist, lofi), stored in the send's type word through stock's set;
// - titles are the slot's effect's own parameter names;
// - with Stop and Shift held, E3 and E4 set scramble's range (the fewest and
//   most effects), an E4 click scrambles the chain and an E3 click goes back
//   to the state before the last scramble or effect change.
// Stock does nothing with Stop + a track key on a send page.
#include "multifx_ui.hpp"
#include <opxy/multifx.hpp>
#include "stock.hpp"

namespace opxy { namespace platform { namespace multifx_ui {
namespace {
using namespace opxy::multifx;
const u32 kAuxController = 0x0803fb6c;   // the send page's controller vtable
const u32 kProject = 0x86bd027c;
const u32 kStop = 52, kShift = 55, kFirstTrackKey = 14, kUndoClick = 12, kScrambleClick = 13;
const i32 kMiddle = 0x40000000;

u8 selected[2];             // the slot each send's encoders edit
bool blocked[72];           // track keys whose press MultiFX took; their release too

// Whether effect `id` is the chain: its record (relocated to 0x8d4cc380, 48
// bytes each) carries its stock name inline at byte 16. The builder links no
// weak symbols, so the chain is found by name, not by its effect number.
const u32 kEffectRecords = 0x8d4cc380, kRecordBytes = 48;
const volatile unsigned char* record_name(u32 id) {
    return at<const volatile unsigned char>(kEffectRecords + id * kRecordBytes + 16);
}
bool chain_effect(u32 id) {
    if (id >= kEffects) return false;
    const char name[] = "veffmultifx";
    const volatile unsigned char* stored = record_name(id);
    for (u32 i = 0; i < sizeof name; ++i)   // with its terminator
        if (stored[i] != static_cast<unsigned char>(name[i])) return false;
    return true;
}
// An effect a slot may hold: one of stock's six send effects, or an effect
// machine this firmware installed (its factory lies in the SDK module, which
// every layout places between 0x8d0d0380 and 0x8d4d0380), but not MultiFX.
bool installed(u32 id) {
    if (id >= kEffects) return false;
    if (stock_effect(id)) return true;
    const u32 factory = word(kEffectRecords + id * kRecordBytes);
    return factory >= 0x8d0d0380u && factory < 0x8d4d0380u && record_name(id)[0] && !chain_effect(id);
}
// The effects in Shift + E1's order: none, stock's six, then the installed ones.
u32 effect_list(u8 (&list)[kEffects + 1]) {
    u32 n = 0;
    list[n++] = static_cast<u8>(kNone);
    for (u32 i = 0; i < kStockEffects; ++i) list[n++] = kStockEffect[i];
    for (u32 id = 0; id < kEffects; ++id) if (!stock_effect(id) && installed(id)) list[n++] = static_cast<u8>(id);
    return n;
}
// The effect's name in upper case, at most `capacity` - 1 characters.
void effect_name(u32 id, char* out, u32 capacity) {
    const char* none = "-";
    u32 n = 0;
    if (id >= kEffects) { while (none[n] && n + 1 < capacity) { out[n] = none[n]; ++n; } out[n] = 0; return; }
    const volatile unsigned char* stored = record_name(id);
    for (; stored[n] && n + 1 < capacity && n < 31; ++n) {
        const char c = static_cast<char>(stored[n]);
        out[n] = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
    }
    out[n] = 0;
}
u32 top_controller_vtable() { return *stock<const u32*()>(0x8c0944b0)(); }
bool held(u32 key) { return stock<u32(u32)>(0x8d5b7d7c)(key) != 0; }
i32 current(u32 track, u32 index) {
    return *reinterpret_cast<const volatile i32*>(stock<u32(u32)>(0x8d587708)(track) + 0x3864 + index * 4);
}
void notify(const char* text) { stock<void(const char*, u32, u32, u32)>(0x8d549168)(text, 1, 0, 0x3f000000u); }
u32 slot_of(u32 track) { return selected[track - 14] < kSlots ? selected[track - 14] : 0; }
u32 slot_choice(u32 track, u32 slot) { return choice(current(track, type_word(slot)), slot); }
u32 lfo_of(u32 track) { return lfo_slot(current(track, kLfoWord)); }
bool scramble_layer() { return shift_held() && held(kStop); }

// Back: the chain before each scramble or effect change, per send, the newest
// last; the oldest goes when it is full. Scramble's range is not part of it.
const u32 kHistory = 8, kSaved = kSlots * 4 + 3;
u32 saved_word(u32 i) { return i < kSlots * 4 ? kParamWord[i / 4][i % 4] : i == kSlots * 4 + 2 ? kLfoWord : kTypeWords[i - kSlots * 4]; }
i32 history[2][kHistory][kSaved];
u8 depth[2];
void remember(u32 track) {
    const u32 send = track - 14;
    if (depth[send] == kHistory) {
        for (u32 h = 1; h < kHistory; ++h) for (u32 i = 0; i < kSaved; ++i) history[send][h - 1][i] = history[send][h][i];
        --depth[send];
    }
    for (u32 i = 0; i < kSaved; ++i) history[send][depth[send]][i] = current(track, saved_word(i));
    ++depth[send];
}
void go_back(u32 project, u32 track) {
    const u32 send = track - 14;
    if (!depth[send]) { notify("NOTHING TO UNDO"); return; }
    const i32* old = history[send][--depth[send]];
    // Parameters first, then the effects and the LFO's slot (keeping the range).
    for (u32 i = 0; i < kSaved; ++i) {
        const u32 w = saved_word(i);
        i32 value = old[i];
        if (w == kLfoWord) value = with_lfo(current(track, w), lfo_slot(value));
        if (current(track, w) != value) set_parameter(project, track, w, value);
    }
    notify("UNDO");
}

u32 seed;
u32 next_random(u32 span) {
    if (!seed) seed = static_cast<u32>(stock<i32(u32, u32)>(0x8c0b5230)(0, 0)) | 1u;
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return span ? ((seed >> 16) * span) >> 16 : 0;   // 0 .. span - 1, without a divide
}
i32 random_value() {
    next_random(0);
    return static_cast<i32>(seed >> 1);
}

// "SLOT 2 DELAY"
void announce(u32 track, u32 slot) {
    char text[24];
    char name[12];
    effect_name(slot_choice(track, slot), name, sizeof name);
    if (name[0] == '-' && !name[1]) { name[0] = 'N'; name[1] = 'O'; name[2] = 'N'; name[3] = 'E'; name[4] = 0; }
    u32 n = 0;
    for (const char* p = "SLOT "; *p; ++p) text[n++] = *p;
    text[n++] = static_cast<char>('1' + slot);
    text[n++] = ' ';
    for (const char* p = name; *p && n < sizeof(text) - 1; ++p) text[n++] = *p;
    text[n] = 0;
    notify(text);
}
// "LFO SLOT 3"
void announce_lfo(u32 slot) {
    char text[] = "LFO SLOT 1";
    text[9] = static_cast<char>('1' + slot);
    notify(text);
}
// "SCRAMBLE 1-7"
void announce_range(u32 track) {
    char text[] = "SCRAMBLE 1-7";
    const i32 w = current(track, kLfoWord);
    text[9] = static_cast<char>('0' + scramble_min(w));
    text[11] = static_cast<char>('0' + scramble_max(w));
    notify(text);
}
// A new chain: scramble's range of effects in slots 1.., each with random
// parameters, the rest none; the LFO on one of them.
void scramble(u32 project, u32 track) {
    u8 list[kEffects + 1];
    const u32 size = effect_list(list) - 1;   // without none, first in the list
    u8 chain[kSlots];
    const i32 range = current(track, kLfoWord);
    const u32 count = scramble_plan(scramble_min(range), scramble_max(range), list + 1, size, next_random, chain);
    if (!count) return;
    remember(track);
    u32 packed[2] = {0, 0};
    for (u32 k = 0; k < kSlots; ++k) {
        if (chain[k] == kNone) continue;
        for (u32 j = 0; j < 4; ++j) set_parameter(project, track, kParamWord[k][j], random_value());
        packed[k / 4] |= slot_byte(chain[k]) << (8 * (k & 3));
    }
    for (u32 t = 0; t < 2; ++t)
        if (static_cast<u32>(current(track, kTypeWords[t])) != packed[t])
            set_parameter(project, track, kTypeWords[t], static_cast<i32>(packed[t]));
    set_parameter(project, track, kLfoWord, with_lfo(range, next_random(count)));
    char text[] = "SCRAMBLE 0 FX";
    text[9] = static_cast<char>('0' + count);
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
    if (scramble_layer()) {
        // Stop + Shift + E3 / E4: scramble's fewest and most effects.
        if (position < 2 || !event) return 1;
        const i32 delta = reinterpret_cast<const i32*>(event)[0];
        if (!delta) return 1;
        const i32 w = current(track, kLfoWord);
        u32 low = scramble_min(w), high = scramble_max(w);
        u32& n = position == 2 ? low : high;
        n = delta > 0 ? (n < kSlots ? n + 1 : n) : (n > 1 ? n - 1 : n);
        if (position == 2 && high < low) high = low;
        if (position == 3 && low > high) low = high;
        const i32 next = with_range(w, low, high);
        if (next != w) set_parameter(project, track, kLfoWord, next);
        announce_range(track);
        return 1;
    }
    if (shift_held()) {
        // Shift + E1: the slot's effect. Shift + E2: the slot the send's LFO
        // moves. The other encoders do nothing with Shift.
        if (position > 1 || !event) return 1;
        const i32 delta = reinterpret_cast<const i32*>(event)[0];
        const auto step = [delta](u32 now, u32 count) {
            return delta > 0 ? (now + 1 < count ? now + 1 : now) : delta < 0 ? (now ? now - 1 : 0) : now;
        };
        if (position == 1) {
            const u32 now = lfo_of(track), next = step(now, kSlots);
            if (next == now) return 1;
            remember(track);
            set_parameter(project, track, kLfoWord, with_lfo(current(track, kLfoWord), next));
            announce_lfo(next);
            return 1;
        }
        u8 list[kEffects + 1];
        const u32 count = effect_list(list), now = slot_choice(track, slot);
        u32 at = 0;
        while (at < count && list[at] != now) ++at;
        if (at == count) at = 0;
        const u32 next = list[step(at, count)];
        if (next == now) return 1;
        remember(track);
        const u32 shift = 8 * (slot & 3);
        u32 packed = static_cast<u32>(current(track, type_word(slot)));
        packed = (packed & ~(0xffu << shift)) | (slot_byte(next) << shift);
        // A new effect starts from the middle of each of its parameters.
        for (u32 j = 0; j < 4; ++j) set_parameter(project, track, kParamWord[slot][j], kMiddle);
        set_parameter(project, track, type_word(slot), static_cast<i32>(packed));
        announce(track, slot);
        return 1;
    }
    return stock<u32(u32, u32, u32, u32)>(0x8d5ad5bc)(project, track, kParamWord[slot][position], event);
}

const char* title(u32 track, u32 position) {
    if (scramble_layer()) return position == 2 ? "min" : position == 3 ? "max" : "";
    if (shift_held()) return position == 0 ? "effect" : position == 1 ? "lfo" : "";
    const u32 c = slot_choice(track, slot_of(track));
    if (c == kNone) return position == 0 ? "none" : "";
    // The stock effect's own name for the parameter (its record's callback).
    return stock<const char*(u32, u32)>(0x8d506c90)(c, position);
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
    if ((key == kUndoClick || key == kScrambleClick) && scramble_layer()) {
        const u32 track = selected_track();
        if (!on(track) || top_controller_vtable() != kAuxController) return false;
        blocked[key] = true;
        if (key == kScrambleClick) scramble(kProject, track); else go_back(kProject, track);
        return true;
    }
    if (key < kFirstTrackKey || key >= kFirstTrackKey + 8 || !held(kStop)) return false;
    const u32 track = selected_track();
    if (!on(track) || top_controller_vtable() != kAuxController) return false;
    const u32 slot = key - kFirstTrackKey;
    blocked[key] = true;
    selected[track - 14] = static_cast<u8>(slot);
    announce(track, slot);
    return true;
}
}}}

namespace opxy { namespace multifx {
bool view(u32 track, View& out) {
    namespace ui = platform::multifx_ui;
    if (!ui::on(track)) return false;
    out.slot = static_cast<u8>(ui::slot_of(track));
    out.lfo = static_cast<u8>(ui::lfo_of(track));
    for (u32 k = 0; k < kSlots; ++k) {
        out.type[k] = static_cast<u8>(ui::slot_choice(track, k));
        ui::effect_name(out.type[k], out.name[k], sizeof out.name[k]);
    }
    for (u32 j = 0; j < 4; ++j) {
        const char* t = ui::title(track, j);
        out.title[j] = t ? t : "";
        ui::caption(track, j, out.value[j]);
    }
    if (ui::scramble_layer()) {
        // Stop + Shift: scramble's range under E3 and E4.
        const i32 w = ui::current(track, kLfoWord);
        out.value[0][0] = out.value[1][0] = 0;
        out.value[2][0] = static_cast<char>('0' + scramble_min(w)); out.value[2][1] = 0;
        out.value[3][0] = static_cast<char>('0' + scramble_max(w)); out.value[3][1] = 0;
    } else if (platform::shift_held()) {
        // Shift + E1 chooses the slot's effect: its name in place of a value.
        // The header's value fields are 60 pixels wide: the name's first five
        // letters (the block below shows it whole) and the LFO slot's number.
        ui::effect_name(out.type[out.slot], out.value[0], 6);
        out.value[1][0] = static_cast<char>('1' + out.lfo); out.value[1][1] = 0;
        for (u32 j = 2; j < 4; ++j) out.value[j][0] = 0;
    }
    return true;
}
}}

extern "C" void sdk_multifx_startup() __asm__("sdk_multifx_startup");
void sdk_multifx_startup() {
    namespace ui = opxy::platform::multifx_ui;
    sdk_multifx_ui = opxy::platform::MultiFxUi{ui::on, ui::shown_word, ui::turn, ui::title, ui::caption};
    sdk_multifx_raw_event = ui::raw_event;
}
