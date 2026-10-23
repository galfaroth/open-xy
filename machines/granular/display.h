#pragma once
#include <opxy/draw.hpp>

namespace granular {
// One mutable outline (the grain window), a position marker, two spray
// brackets, 22 grain dots and two build marks; 16 grey palette entries.
constexpr unsigned kDisplayGrains = 22;
constexpr unsigned kBuildMarks = 2;
constexpr unsigned kBuild = 2;  // lit marks in the top-right corner identify the display revision
using Frame = opxy::draw::Frame<1, 64, 4 + kDisplayGrains + kBuildMarks, 16>;
struct DisplayGrain { opxy::i32 x, y, age, life; };  // Q10 pixels and Q10 seconds
struct DisplayState {
    opxy::u32 initialized, track, previous_event, rng;
    opxy::i32 active_age;      // Q10 seconds since the last note start
    opxy::i32 spawn_due;       // Q10 seconds until the next grain
    opxy::i32 scan_offset;     // Q10 pixels along the strip
    DisplayGrain grains[kDisplayGrains];
};
}
