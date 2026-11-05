#include "../../sdk/platform/opxy-1.2.1/module_events.cpp"
#include <cstdlib>
namespace { unsigned calls[8], count; }
void require(bool value) { if (!value) std::abort(); }
void append(unsigned id) { calls[count++] = id; }
void sdk_arranger_raw_event(const void*) { append(2); }
void sdk_arranger_bounce_message(const void*, opxy::u32) { append(4); }
void sdk_arranger_bounce_remote_poll() { append(5); }
void sdk_arranger_poll() { append(6); }
int main() {
    // Every module event reaches the arranger, which forwards the rest to stock.
    sdk_module_raw_event(nullptr); require(count == 1 && calls[0] == 2);
    count = 0; sdk_module_message(nullptr, 0); require(count == 1 && calls[0] == 4);
    count = 0; sdk_module_remote_poll(); require(count == 1 && calls[0] == 5);
    count = 0; sdk_module_gui_poll(); require(count == 1 && calls[0] == 6);
}
