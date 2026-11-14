#include "display.h"

// MultiFX's send page: the edited slot's parameters in the header, then the
// chain as two rows of four blocks, read from the send's input on the left of
// the top row to its output on the right of the bottom row, each named after
// its effect (stock's, or any effect machine installed beside it). The edited slot (Stop + 1..8) is lit and underlined, the slot
// the send's LFO moves carries a mark, and a slot without an effect is a dim
// outline.
namespace veffmultifx {
namespace draw = opxy::draw;
using namespace opxy::multifx;
using opxy::u32; using opxy::i32;

const u32 kFirstBlock = 8, kMarker = kFirstBlock + kSlots, kLfoMark = kMarker + 1;
const u32 kFirstName = 16, kFirstNumber = kFirstName + kSlots;
// Pixels, as in panel.svg.fragment: each row's top, the blocks' left and step.
const i32 kRowTop[2] = {58, 143}, kLeft = 34, kStep = 108, kBlockHeight = 58;
enum Shade : u32 { kEmpty, kFilled, kEdited, kAccent };

inline i32 block_x(u32 k) { return kLeft + static_cast<i32>(k % 4) * kStep; }
inline i32 block_y(u32 k) { return kRowTop[k / 4]; }

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
        const i32 x = block_x(k) * unit, y = block_y(k) * unit;
        const bool edited = known && v.slot == k, empty = !known || v.type[k] == kNone;
        draw::place(frame, kFirstBlock + k, x, y, unit,
                    edited ? kEdited : empty ? kEmpty : kFilled, empty && !edited ? draw::stroke : draw::fill_and_stroke);
        draw::text(frame, kFirstName + k, x + 8 * unit, y + 36 * unit, unit, known ? v.name[k] : "-");
        const char number[2] = {static_cast<char>('1' + k), 0};
        draw::text(frame, kFirstNumber + k, x, y - 5 * unit, unit, number);
    }
    const u32 edited = known ? v.slot : 0, lfo = known ? v.lfo : 0;
    draw::place(frame, kMarker, block_x(edited) * unit, (block_y(edited) + kBlockHeight + 3) * unit, unit, kAccent,
                draw::fill_and_stroke);
    draw::place(frame, kLfoMark, (block_x(lfo) + 74) * unit, (block_y(lfo) + 6) * unit, unit, kAccent,
                draw::fill_and_stroke);
}
}
