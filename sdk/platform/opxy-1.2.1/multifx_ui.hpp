// MultiFX (Veff fork), GUI side: the send page of a MultiFX chain.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace multifx_ui {
// Whether send track `track` (14 or 15) holds the MultiFX chain.
bool on(u32 track);
// The send track word encoder position `position` edits on `track`'s page now.
u32 shown_word(u32 track, u32 position);
// An encoder turn on the send page (sdk_effect_turn).
u32 turn(u32 project, u32 track, u32 position, u32 event);
// The position's title (sdk_fx_title) and value caption (sdk_fx_value_text).
const char* title(u32 track, u32 position);
void caption(u32 track, u32 position, char* text);
// A raw key event; true when MultiFX took it (Stop + 1..4, Shift + Stop + 1..8).
bool raw_event(const void* event);
}

// What the shared units (synth_controls.cpp, module_events.cpp) call; empty
// until sdk_multifx_startup fills it on the GUI role.
struct MultiFxUi {
    bool (*on)(u32);
    u32 (*shown_word)(u32, u32);
    u32 (*turn)(u32, u32, u32, u32);
    const char* (*title)(u32, u32);
    void (*caption)(u32, u32, char*);
};
}}
extern opxy::platform::MultiFxUi sdk_multifx_ui;
extern bool (*sdk_multifx_raw_event)(const void*);
inline bool multifx_page(opxy::u32 track) { return sdk_multifx_ui.on && sdk_multifx_ui.on(track); }
