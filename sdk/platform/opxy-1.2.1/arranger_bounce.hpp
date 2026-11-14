#pragma once
#include <opxy/types.hpp>
#include "arranger_bounce_wire.hpp"
struct ArrangerBounceDiagnostic {
    opxy::u32 polls, dispatches, captures, flags, timeout_flags;
};
extern "C" {
void sdk_arranger_bounce_receive(const opxy::platform::bounce_wire::Message*)
    __asm__("sdk_arranger_bounce_receive");
// Best-effort GUI snapshot; capture count comes from the remote recorder.
void sdk_arranger_bounce_diagnostic(ArrangerBounceDiagnostic* out)
    __asm__("sdk_arranger_bounce_diagnostic");
// GUI: 0 accepted, 1 busy, 2 invalid destination/clip, 3 duration, 4 project unavailable, 5 muted clip.
opxy::u32 sdk_arranger_bounce_request(opxy::u32 slot, opxy::u32 engine, bool replace)
    __asm__("sdk_arranger_bounce_request");
bool sdk_arranger_bounce_busy() __asm__("sdk_arranger_bounce_busy");
// GUI (Veff): a Lab by Veff commit through the same capture, writer checks and
// result codes; lab_commit.cpp supplies its source, file and destination.
// With `tail`, the file ends 0.1 s after the last block above -60 dBFS.
opxy::u32 sdk_arranger_bounce_lab(opxy::u32 frames, bool post_master, bool tail) __asm__("sdk_arranger_bounce_lab");
// GUI, before a request (Veff): record after the master EQ and dynamics, as
// resampling hears, instead of before them (the default).
void sdk_arranger_bounce_post_master(bool post) __asm__("sdk_arranger_bounce_post_master");
// Audio: the mix engine's fetch of the master EQ's object (8d511b6c); copies
// the pre-master mix for a pre-master bounce first.
opxy::u32 sdk_arranger_bounce_master_tap() __asm__("sdk_arranger_bounce_master_tap");
void sdk_arranger_bounce_stopped(opxy::u32 recorder) __asm__("sdk_arranger_bounce_stopped");
void sdk_arranger_bounce_cancel() __asm__("sdk_arranger_bounce_cancel");
void sdk_arranger_bounce_poll() __asm__("sdk_arranger_bounce_poll");
// 0 idle, 1 arming, 2 playing, 3 saving, 4 done, 5 cancelled, 6 failed, 7 assigning.
opxy::u32 sdk_arranger_bounce_status() __asm__("sdk_arranger_bounce_status");
// 0 none, 1 capture/transport, 2 short capture, 3 write, 4 file verification, 5 destination.
opxy::u32 sdk_arranger_bounce_failure() __asm__("sdk_arranger_bounce_failure");
opxy::u32 sdk_arranger_bounce_progress() __asm__("sdk_arranger_bounce_progress");
// Dispatcher boundary and render-worker capture callbacks; no file I/O here.
void sdk_arranger_bounce_dispatch(opxy::u32 controller) __asm__("sdk_arranger_bounce_dispatch");
void sdk_arranger_bounce_capture(opxy::u32 recorder, const void* pcm, opxy::u32 words)
    __asm__("sdk_arranger_bounce_capture");
void sdk_arranger_bounce_save_range(const char* path, opxy::u32 begin, opxy::u32 end, opxy::i32 key)
    __asm__("sdk_arranger_bounce_save_range");
void sdk_arranger_bounce_saved(opxy::u32 editor, opxy::u32 frames) __asm__("sdk_arranger_bounce_saved");
opxy::u32 sdk_arranger_bounce_write(const char* path, opxy::u32 channels, opxy::u32 rate,
    const void* pcm, opxy::u32 samples, opxy::u32 format, opxy::i32 key)
    __asm__("sdk_arranger_bounce_write");
}
