// Lab by Veff (Veff fork): a commit. It captures through Linear's bounce
// coordinator (arranger_bounce.cpp: prepare, record, stop, save, writer
// checks), with Lab's own source, file and destination:
// - source: the pattern from its start for N bars (native transport, Linear
//   off: arranger_transport.cpp counts the ticks), or the last note played
//   on a track (stock's last key per track, 0x080379f0 + track), held 1 s
//   with velocity 100 and recorded 2 s more for its tail;
// - file: /fat32/samples/user/vefflab/<project>-NN[-NN...].wav (lab_model.hpp);
// - destination: T7's Sampler in its current pattern always, and with the
//   palette chosen also T8's next free Drum Sampler pad (keys 53..76).
#include "lab_commit.hpp"
#include "lab_ui.hpp"
#include "arranger_bounce.hpp"
#include "arranger_project.hpp"
#include "arranger_transport.hpp"
#include "stock.hpp"
#include "barrier.hpp"

namespace {
using namespace opxy;
using namespace opxy::platform;
const u32 kController = 0x860c6adc, kProject = 0x840a0524;
const u32 kSampler = 2, kDrumSampler = 3, kFirstPad = 53;
const u32 kHoldMs = 1000, kTailMs = 2000;
const char kFolder[] = "/fat32/samples/user/vefflab/";

lab::Commit running;
u32 ticks, note_track, note_key, note_started, sort_track = 8, result;
bool note_on_now, note_done, owned;
char path[97], stem[lab::kStemCapacity];

u32 now_ms() { return stock<u32(u32, u32)>(0x8d5be290)(0, 0); }
u8* project() { return reinterpret_cast<u8*>(word(kProject)); }
// The track's current pattern and its engine (2 Sampler, 3 Drum Sampler).
u8* current_pattern(u32 track) {
    u8* p = project();
    if (!p) return nullptr;
    const u32 pattern = p[0x98 + 33u * p[6] + track];
    return pattern < 16 ? p + 0xd7c + track * 0x53f44 + pattern * 0x53f4 : nullptr;
}
u32 engine_of(u32 track) {
    const u8* pattern = current_pattern(track);
    return pattern ? *reinterpret_cast<const u32*>(pattern + 0x18) : 0;
}
bool locked(u32 track) {
    const u8* p = project();
    return !p || ((p[12] | (u32(p[13]) << 8)) & (1u << track));
}
const u8* region(u32 track, u32 key) { return stock<const u8*(u32, u32)>(0x8d589370)(track, key); }
i32 next_pad() {
    for (u32 pad = 0; pad < lab::kPads; ++pad) {
        const u8* r = region(lab::kPaletteTrack, kFirstPad + pad);
        if (!r || !r[0x20]) return static_cast<i32>(pad);
    }
    return -1;
}
bool file_exists(const char* full) { return stock<u32(const char*)>(0x8c040c24)(full) != 0; }
// The project's name (stock's active project, without ".xy").
void project_name(char (&out)[32]) {
    u32 text[7] = {};   // a native 28-byte string: data or pointer at +4, size +0x14, capacity +0x18
    stock<void(void*)>(0x8d4e19d8)(text);
    const u32 size = text[5], capacity = text[6];
    const char* chars = capacity >= 16 ? at<const char>(text[1]) : reinterpret_cast<const char*>(text + 1);
    u32 n = 0;
    for (; n < size && n < 31; ++n) out[n] = chars[n];
    out[n] = 0;
    if (capacity >= 16) stock<void(u32)>(0x8d4d2910)(text[1]);
}
// The stem of the palette pad's sample a note on T8 came from, if it is Lab's.
void parent_stem(u32 track, u32 key, char (&out)[lab::kStemCapacity]) {
    out[0] = 0;
    if (track != lab::kPaletteTrack) return;
    const u8* r = region(track, key);
    if (!r || !r[0x20]) return;
    const char* file = reinterpret_cast<const char*>(r + 0x20);
    const char* base = file;
    for (const char* c = file; *c; ++c) if (*c == '/') base = c + 1;
    u32 n = 0;
    for (; base[n] && base[n] != '.' && n + 1 < lab::kStemCapacity; ++n) out[n] = base[n];
    out[n] = 0;
}
bool name(const lab::Commit& c, char (&out)[lab::kStemCapacity]) {
    char project_text[32], parent[lab::kStemCapacity];
    project_name(project_text);
    const u32 key = byte(0x080379f0 + c.track);
    parent_stem(c.source == lab::LastNote ? c.track : 8, key ? key : 60, parent);
    return lab::next_stem(project_text, parent[0] ? parent : nullptr, [](const char* candidate) {
        char full[97]; u32 n = 0;
        for (const char* t = kFolder; *t; ++t) full[n++] = *t;
        for (const char* t = candidate; *t && n < 90; ++t) full[n++] = *t;
        const char* wav = ".wav"; for (; *wav; ++wav) full[n++] = *wav;
        full[n] = 0;
        return file_exists(full);
    }, out);
}
u32 bar_ticks() {
    const u32 meter = stock<u32()>(0x8d584418)();
    return 480 * word(0x840a9f70 + 4 * (meter < 5 ? meter : 5));
}
void note_off() {
    if (!note_on_now) return;
    stock<void(u32, u32, u32, u32, u32, const void*)>(0x8d595420)(kController, note_track, note_key, 1, 0, nullptr);
    note_on_now = false;
}
}

// ---- GUI: the panel's commit ----
const char* sdk_lab_commit_start(const lab::Commit& c) {
    if (owned && sdk_arranger_bounce_busy()) return "LAB BUSY";
    if (!sdk_arranger_project_lab()) return "LAB MODE IS OFF";
    if (engine_of(lab::kNewestTrack) != kSampler) return "T7 MUST BE A SAMPLER";
    if (locked(lab::kNewestTrack)) return "UNLOCK T7";
    if (c.palette && engine_of(lab::kPaletteTrack) != kDrumSampler) return "T8 MUST BE A DRUM SAMPLER";
    if (c.palette && locked(lab::kPaletteTrack)) return "UNLOCK T8";
    if (c.palette && next_pad() < 0) return "PALETTE FULL";
    u32 frames;
    if (c.source == lab::Sequence) {
        if (sdk_arranger_project_linear()) return "SEQUENCE NEEDS LINEAR OFF";
        const u32 bpm = project_bpm_x10();
        if (bpm < 200 || bpm > 3000) return "TEMPO UNAVAILABLE";
        ticks = lab::kBars[c.bars & 3] * bar_ticks();
        // As Linear's duration_frames: 32-bit arithmetic, 44.1 kHz.
        const u32 denominator = bpm * 4, whole = ticks / denominator;
        frames = whole * 55125 + ((ticks % denominator) * 55125 + denominator - 1) / denominator;
        if (frames > 882000 - 4096) return "TOO LONG: FEWER BARS";
    } else {
        note_track = c.track & 7;
        const u32 key = byte(0x080379f0 + note_track);
        note_key = key && key < 128 ? key : 60;
        frames = (kHoldMs + kTailMs) * 441 / 10;
    }
    if (!name(c, stem)) return "NO FREE FILE NAME";
    u32 n = 0;
    for (const char* t = kFolder; *t; ++t) path[n++] = *t;
    for (const char* t = stem; *t && n < 90; ++t) path[n++] = *t;
    for (const char* t = ".wav"; *t; ++t) path[n++] = *t;
    path[n] = 0;
    running = c;
    note_on_now = note_done = false;
    result = 0;
    const u32 refused = sdk_arranger_bounce_lab(frames, c.post_master);
    if (refused) return refused == 1 ? "BUSY: STOP REC / PLAY" : refused == 3 ? "TOO SHORT / LONG" : "PROJECT BUSY";
    owned = true;
    return nullptr;
}
bool sdk_lab_commit_busy() { return owned && sdk_arranger_bounce_busy(); }
void sdk_lab_commit_cancel() { if (sdk_lab_commit_busy()) sdk_arranger_bounce_cancel(); }
u32 sdk_lab_last_track() { return stock<u32()>(0x8d4d6f14)(); }
i32 sdk_lab_next_pad() { return engine_of(lab::kPaletteTrack) == kDrumSampler ? next_pad() : -1; }
bool sdk_lab_preview_stem(const lab::Commit& c, char (&out)[lab::kStemCapacity]) { return name(c, out); }
// The finished commit's outcome once, as a popup: null while none is new.
const char* sdk_lab_commit_outcome() {
    if (!owned || sdk_arranger_bounce_busy()) return nullptr;
    owned = false;
    const u32 status = sdk_arranger_bounce_status();
    if (status == 4) return running.palette ? "SAMPLE ON T7 + PALETTE" : "SAMPLE ON T7";
    if (status == 5) return "LAB CANCELLED";
    switch (sdk_arranger_bounce_failure()) {
    case 2: return "LAB: CAPTURE TOO SHORT";
    case 3: return "LAB: WRITE FAILED";
    case 4: return "LAB: FILE CHECK FAILED";
    case 5: return "LAB: ASSIGN FAILED";
    default: return "LAB: CAPTURE FAILED";
    }
}

// ---- Dispatch: the source ----
bool sdk_lab_source_begin() {
    if (running.source == lab::Sequence) return sdk_lab_transport_begin(ticks);
    note_on_now = note_done = false;
    return true;
}
bool sdk_lab_source_start() {
    if (running.source == lab::Sequence) return sdk_lab_transport_start();
    // Stock's live note-on as a keyboard note (track_control.cpp's call).
    const __UINT64_TYPE__ now = stock<__UINT64_TYPE__(u32)>(0x8d59357c)(kController);
    stock<void(u32, u32, u32, u32, u32, u32, u32, u32, u32, u32, u32, const void*)>(0x8d595738)(
        kController, note_track, note_key, 0xf0, 0, 100, 0, 1, static_cast<u32>(now), static_cast<u32>(now >> 32), 0, nullptr);
    note_on_now = true; note_started = now_ms();
    return true;
}
bool sdk_lab_source_running() {
    if (running.source == lab::Sequence) return sdk_lab_transport_running();
    if (note_done) return false;
    const u32 elapsed = now_ms() - note_started;
    if (elapsed >= kHoldMs) note_off();
    if (elapsed >= kHoldMs + kTailMs) note_done = true;
    return !note_done;
}
void sdk_lab_source_stop() {
    if (running.source == lab::Sequence) sdk_lab_transport_stop();
    else { note_off(); note_done = true; }
}
void sdk_lab_source_end() {
    if (running.source == lab::Sequence) sdk_lab_transport_end();
    else note_off();
}

// ---- GUI: the file and where it goes ----
const char* sdk_lab_path() { return path; }
u32 sdk_lab_sort_track() { return sort_track; }
bool sdk_lab_destination_begin(const char* file, u32 frames) {
    result = 2;
    if (engine_of(lab::kNewestTrack) != kSampler || locked(lab::kNewestTrack)) return false;
    // T7: stock's own sampler completion (8c0a43d4), on its current pattern.
    sort_track = lab::kNewestTrack; barrier();
    stock<void(u32, u32, const char*)>(0x8d5895dc)(lab::kNewestTrack, 0, file);
    stock<void(u32, u32, u32, u32)>(0x8d5acf28)(0x86bd027c, lab::kNewestTrack, 52, 0x3ffffff8);
    stock<void(u32, u32, u32, i32, i32, u32, u32)>(0x8d589b8c)(lab::kNewestTrack, 60, 60, -1, -1, frames, 0);
    bool ok = true;
    if (running.palette) {
        const i32 pad = next_pad();
        if (pad < 0 || engine_of(lab::kPaletteTrack) != kDrumSampler || locked(lab::kPaletteTrack)) ok = false;
        else {
            const u32 key = kFirstPad + static_cast<u32>(pad);
            sort_track = lab::kPaletteTrack; barrier();
            stock<void(u32, u32, const char*)>(0x8d5893bc)(lab::kPaletteTrack, key, file);
            stock<void(u32, u32, u32, u32)>(0x8d589ac0)(lab::kPaletteTrack, key, frames, 0);
            const u8* r = region(lab::kPaletteTrack, key);
            ok = r && r[0x20] && *reinterpret_cast<const u32*>(r) == frames;
        }
    }
    sort_track = 8; barrier();
    result = ok ? 1 : 2;
    return true;
}
u32 sdk_lab_destination_status() { return result; }
