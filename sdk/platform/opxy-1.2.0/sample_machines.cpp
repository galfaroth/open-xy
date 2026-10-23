// Stock sample dispatch for every registered SDK sample engine. A matching
// engine number is not enough: the machine's registration must still be ours.
// The stubs in sample_hooks.s call these from inside stock functions.
#include "sample_machines.hpp"

using opxy::u32;
using opxy::platform::SampleMachine;

extern "C" {
u32 sdk_sample_note_route(u32) __asm__("sdk_sample_note_route");
u32 sdk_sample_alias(u32) __asm__("sdk_sample_alias");
u32 sdk_sample_classify_engine(u32, u32) __asm__("sdk_sample_classify_engine");
}

const SampleMachine* sdk_sample_machine(u32 engine) {
    for (u32 i = 0; i < sdk_sample_machine_count; ++i) {
        const SampleMachine& m = sdk_sample_machines[i];
        if (m.engine == engine) return m.ready() == 1 ? &m : nullptr;
    }
    return nullptr;
}

// Note-region hook, decoded by extensions_sample_note_region (sample_hooks.s).
// An owned engine borrows the multisampler payload and keeps stock's
// nearest-region test, except on the drum page (7), where notes map to its
// canonical pads.
enum NoteRoute : u32 { kStockRoute = 0, kOwnedNearest = 1, kOwnedDrumPads = 2 };
u32 sdk_sample_note_route(u32 engine) {
    const u32 page = opxy::platform::sample_page(engine);
    return !page ? kStockRoute : page == 7 ? kOwnedDrumPads : kOwnedNearest;
}

// Stock's sample predicates know only stock engine IDs. Owned SDK engines
// borrow the multisampler's (30) payload, or the sampler's (2) GUI flows when
// they declare singleSample; the track keeps the real engine.
u32 sdk_sample_alias(u32 engine) {
    if (!opxy::platform::sample_page(engine)) return engine;
    return opxy::platform::single_sample(engine) ? 2 : 30;
}

// Stock's sample predicate (8d5b5b38), with its caller. The parameter editor
// (8d5ab258, returning to 8d5ab278) stores a sample engine's controls on a
// path neither its screen nor audio reads; a machine drawing its own page edits
// the parameter vector like a synth, so that one caller sees the real engine.
const u32 kParameterEditorReturn = 0x8d5ab278;
u32 sdk_sample_classify_engine(u32 engine, u32 caller) {
    if (caller == kParameterEditorReturn && opxy::platform::own_sample_page(engine)) return engine;
    return sdk_sample_alias(engine);
}
