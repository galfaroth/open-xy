// Lab by Veff (Veff fork), GUI side: the commit panel's gestures.
#pragma once
#include <opxy/types.hpp>
#include "lab_model.hpp"

// What the shared units call; empty until sdk_lab_startup fills them on the GUI role.
extern bool (*sdk_lab_raw_event)(const void*);           // module_events.cpp
extern bool (*sdk_lab_encoder)(opxy::u32, const void*);  // arranger_ui.cpp's encoder dispatch
extern void (*sdk_lab_poll)();                           // module_events.cpp, each GUI frame
// The commit itself (lab_commit.cpp): 0 started, else a reason shown as a popup.
const char* sdk_lab_commit_start(const opxy::platform::lab::Commit& commit);
bool sdk_lab_commit_busy();
void sdk_lab_commit_cancel();
// The selected track, for the panel's default.
opxy::u32 sdk_lab_last_track();
// The palette's next free pad (0..23), or -1 when full or T8 is no Drum Sampler.
opxy::i32 sdk_lab_next_pad();
// The file stem the commit would write now.
bool sdk_lab_preview_stem(const opxy::platform::lab::Commit& commit, char (&out)[opxy::platform::lab::kStemCapacity]);
// A finished commit's outcome, once; null while none is new.
const char* sdk_lab_commit_outcome();
// The panel's page (drawn on Linear's screen, arranger_ui.cpp): true while
// it is open, filling `out` when given.
extern bool (*sdk_lab_screen)(opxy::platform::lab::Screen* out);
