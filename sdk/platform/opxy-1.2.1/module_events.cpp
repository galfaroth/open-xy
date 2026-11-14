// Shared module dispatch: each optional service owns only its own messages
// and gestures. The arranger forwards all remaining traffic to stock.
#include "arranger_bounce_remote.hpp"
#include "multifx_ui.hpp"
#include "lab_ui.hpp"
extern "C" {
void sdk_arranger_raw_event(const void*) __asm__("sdk_arranger_raw_event");
void sdk_arranger_poll() __asm__("sdk_arranger_poll");
void sdk_module_raw_event(const void*) __asm__("sdk_module_raw_event");
void sdk_module_message(const void*, opxy::u32) __asm__("sdk_module_message");
void sdk_module_remote_poll() __asm__("sdk_module_remote_poll");
void sdk_module_gui_poll() __asm__("sdk_module_gui_poll");
}
void sdk_module_raw_event(const void* event) {
    // MultiFX's send-page gestures (Stop + track keys) first; the rest as before.
    if (sdk_multifx_raw_event && sdk_multifx_raw_event(event)) return;
    // Lab by Veff's commit panel (Stop + Sample) next.
    if (sdk_lab_raw_event && sdk_lab_raw_event(event)) return;
    sdk_arranger_raw_event(event);
}
bool (*sdk_multifx_raw_event)(const void*) = nullptr;
bool (*sdk_lab_raw_event)(const void*) = nullptr;
void sdk_module_message(const void* data, opxy::u32 bytes) { sdk_arranger_bounce_message(data, bytes); }
void sdk_module_remote_poll() { sdk_arranger_bounce_remote_poll(); }
void sdk_module_gui_poll() { sdk_arranger_poll(); }
