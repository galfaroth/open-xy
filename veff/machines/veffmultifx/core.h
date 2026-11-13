#pragma once
#include <opxy/effect.hpp>
#include <opxy/multifx.hpp>

namespace veffmultifx {
using opxy::u8; using opxy::u32; using opxy::i32;
// Words of a send track's parameter record that a stage reads: the whole
// record, with words 0..3 replaced by its slot's four parameters.
const u32 kRecordWords = 58;

struct State {
    u32 send;                              // 14 or 15 once known
    u8 type[opxy::multifx::kSlots];        // each slot's choice the stages were built for
    u8 ready[opxy::multifx::kSlots];       // the stage is built and fits its memory
    u8 built;                              // the stages match `type`
};
void initialize(State& state);
// `parameters` is the send's whole record: the slots' choices and parameters live in it.
void process(State& state, opxy::Channel left, opxy::Channel right, opxy::Parameters parameters);
}
