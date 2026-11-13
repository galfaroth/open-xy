#include "display.h"

// MultiFX's send page: the edited slot's parameters in the header, then the
// chain as a row of blocks from the send's input to its output, each named
// after its effect. The edited slot (Stop + 1..4) is lit and marked; a slot
// without an effect is a dim outline.
namespace veffmultifx {
namespace draw = opxy::draw;
using namespace opxy::multifx;
using opxy::u32; using opxy::i32;

const u32 kFirstBlock = 8, kMarker = kFirstBlock + kSlots;
const u32 kFirstName = 16, kFirstNumber = kFirstName + kSlots;
const i32 kLeft = 34, kStep = 108, kTop = 84, kWidth = 88;   // pixels, as in panel.svg.fragment
enum Shade : u32 { kEmpty, kFilled, kEdited, kAccent };

inline void display_reset(DisplayState& state) { state.header.reset(); }

inline void display(DisplayState& state, const draw::Input& input, Frame& frame) {
    const i32 unit = draw::unit;
    View v{};
    const bool known = view(input.track, v);
    const char* titles[8] = {"", "", "", "", "", "", "", ""};
    char values[8][draw::header_capacity] = {};
    for (u32 j = 0; j < 4; ++j) {
        titles[j] = known ? v.title[j] : "";
        draw::copy(values[j], draw::header_capacity, known ? v.value[j] : "");
    }
    draw::page_header(state.header, input, frame, titles, values, 0);

    for (u32 k = 0; k < kSlots; ++k) {
        const i32 x = (kLeft + static_cast<i32>(k) * kStep) * unit;
        const bool edited = known && v.slot == k, empty = !known || v.type[k] == kNone;
        draw::place(frame, kFirstBlock + k, x, kTop * unit, unit,
                    edited ? kEdited : empty ? kEmpty : kFilled, empty && !edited ? draw::stroke : draw::fill_and_stroke);
        draw::text(frame, kFirstName + k, x + 8 * unit, (kTop + 52) * unit, unit,
                   empty ? "-" : kChoiceName[v.type[k]]);
        const char number[2] = {static_cast<char>('1' + k), 0};
        draw::text(frame, kFirstNumber + k, x, (kTop - 10) * unit, unit, number);
    }
    draw::place(frame, kMarker, (kLeft + static_cast<i32>(known ? v.slot : 0) * kStep) * unit,
                (kTop + 100) * unit, unit, kAccent, draw::fill_and_stroke);
}
}
