#pragma once
#include <opxy/page_header.hpp>
#include <opxy/multifx.hpp>

namespace veffmultifx {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// Nodes 0..7 and texts 0..15 are the SDK's two-page header; then each slot's
// block, the edited slot's marker and the LFO slot's mark; each slot's effect
// name, then its number.
const opxy::u32 kNodes = 8 + opxy::multifx::kSlots + 2, kTexts = 16 + 2 * opxy::multifx::kSlots;
using Frame = opxy::draw::Frame<0, 0, kNodes, kTexts, 4>;

struct DisplayState {
    opxy::draw::PageHeader header;
};
}
