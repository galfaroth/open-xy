// Lab by Veff (Veff fork), GUI side. In a project with lab mode on:
// - Stop + Sample opens the commit panel, except on Linear's timeline, where
//   Shift + Sample is Linear's own bounce;
// - while it is open, E1..E4 set its choices (lab_model.hpp), and the panel
//   draw it on Linear's page (arranger_ui.cpp): a row per encoder, why, and
//   the file it will write; an E4 click starts the commit and the page shows
//   its progress; Stop closes the panel, or cancels a running commit; the
//   panel then returns to the page it opened from with a popup of the outcome;
// - turning lab mode on sets T7 to a Sampler and T8 to a Drum Sampler, one
//   after the other through stock's own instrument change (8d4df688, the
//   chooser's call), which runs on a worker and merges back-to-back changes;
// - consumed presses swallow their release.
#include "lab_ui.hpp"
#include "arranger_project.hpp"
#include "arranger_bounce.hpp"
#include "stock.hpp"

namespace {
using namespace opxy;
using namespace opxy::platform;
const u32 kStop = 52, kSample = 23, kEncoderFourClick = 13, kArrangeController = 40;
lab::Commit commit;
bool blocked[72], committing;
u32 previous_controller;
char stem[lab::kStemCapacity];

bool held(u32 key) { return stock<u32(u32)>(0x8d5b7d7c)(key) != 0; }
void notify(const char* text) { stock<void(const char*, u32, u32, u32)>(0x8d549168)(text, 1, 0, 0x3f000000u); }
u32 top_controller() {
    const u32* controller = stock<const u32*()>(0x8c0944b0)();
    return controller ? controller[1] : 0;
}
void select_controller(u32 id) { stock<void(u32)>(0x8c094178)(id); stock<void()>(0x8c093f50)(); }
bool linear_timeline() { return sdk_arranger_project_linear() && top_controller() == kArrangeController; }
void refresh_stem() { if (!sdk_lab_preview_stem(commit, stem)) stem[0] = 0; }

// Lab mode turned on: T7 a Sampler, then T8 a Drum Sampler. Stock's change is
// asynchronous and drops one of two changes posted together, so each waits
// for the one before (or gives up after kSetupFrames GUI frames).
const u32 kSampler = 2, kDrumSampler = 3, kSetupFrames = 240;
bool was_lab;
u32 setup_step, setup_frames, lab_generation;
void post_engine(u32 engine, u32 track) { stock<void(u32, u32)>(0x8d4df688)(engine, track); }
void poll() {
    const bool lab_now = sdk_arranger_project_lab();
    const u32 generation = stock<u32()>(0x8d584224)();
    // Only a change made in this project (not opening a project in lab mode).
    if (lab_now && !was_lab && generation == lab_generation && !setup_step) setup_step = 1;
    if (!lab_now) setup_step = 0;
    was_lab = lab_now; lab_generation = generation;
    if (!setup_step || sdk_arranger_bounce_busy()) return;
    const u32 track = setup_step == 1 ? lab::kNewestTrack : lab::kPaletteTrack;
    const u32 engine = setup_step == 1 ? kSampler : kDrumSampler;
    if (track_engine(track) == engine) {
        setup_frames = 0;
        if (++setup_step > 2) { setup_step = 0; notify("LAB: T7 SAMPLER / T8 DRUM SAMPLER"); }
        return;
    }
    if (!setup_frames) post_engine(engine, track);
    if (++setup_frames >= kSetupFrames) { setup_frames = 0; setup_step = 0; notify("LAB: SET T7 / T8 BY HAND"); }
}

// The panel draws on Linear's page, which the Arrange controller shows.
void open() {
    commit.begin(sdk_lab_last_track());
    committing = false;
    refresh_stem();
    previous_controller = top_controller();
    if (previous_controller != kArrangeController) select_controller(kArrangeController);
}
void close() {
    commit.cancel();
    committing = false;
    if (previous_controller && previous_controller != kArrangeController) select_controller(previous_controller);
    previous_controller = 0;
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
    if (blocked[key]) return true;
    if (committing) {
        if (key == kStop) { blocked[key] = true; sdk_lab_commit_cancel(); return true; }
        return false;
    }
    if (commit.open) {
        if (key == kStop) { blocked[key] = true; close(); return true; }
        if (key == kEncoderFourClick) {
            blocked[key] = true;
            const char* refused = sdk_lab_commit_start(commit);
            if (refused) notify(refused);
            else committing = true;
            return true;
        }
        return false;
    }
    if (key != kSample || !held(kStop) || !sdk_arranger_project_lab() || linear_timeline()) return false;
    blocked[key] = true;
    open();
    return true;
}

bool encoder(u32 index, const void* event) {
    if (!commit.open) return false;
    const i32 delta = static_cast<const i32*>(event)[1];
    if (committing || !delta) return true;
    commit.turn(index, delta);
    refresh_stem();
    return true;
}

// "RECORDING 45% / STOP CANCELS"
void progress(char (&out)[48]) {
    const u32 status = sdk_arranger_bounce_status();
    const char* what = status == 2 ? "RECORDING " : status == 3 ? "SAVING" : status == 7 ? "ASSIGNING" : "STARTING";
    u32 n = 0;
    for (const char* t = what; *t; ++t) out[n++] = *t;
    if (status == 2) {
        const u32 percent = sdk_arranger_bounce_progress() / 10;
        if (percent >= 100) out[n++] = '1';
        if (percent >= 10) out[n++] = static_cast<char>('0' + percent / 10 % 10);
        out[n++] = static_cast<char>('0' + percent % 10);
        out[n++] = '%';
    }
    for (const char* t = " / STOP CANCELS"; *t; ++t) out[n++] = *t;
    out[n] = 0;
}

bool screen(lab::Screen* out) {
    if (committing && !sdk_lab_commit_busy()) {
        const char* outcome = sdk_lab_commit_outcome();
        close();
        if (outcome) notify(outcome);
        return false;
    }
    if (!commit.open) return false;
    if (out) {
        char status[48] = {};
        if (committing) progress(status);
        lab::describe(commit, commit.palette ? sdk_lab_next_pad() : -1, committing ? "" : stem, status, *out);
    }
    return true;
}
}

extern "C" void sdk_lab_startup() __asm__("sdk_lab_startup");
void sdk_lab_startup() {
    sdk_lab_raw_event = raw_event;
    sdk_lab_encoder = encoder;
    sdk_lab_poll = poll;
    sdk_lab_screen = screen;
}
