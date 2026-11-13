# MultiFX by Veff

MultiFX is built into the Veff fork's firmware, like Linear by Veff. It is a send
effect that runs a chain of up to four stock OP-XY effects in series on FX I or FX II.
Each send can hold its own chain.

## Use it

1. Choose **MultiFX** as the send's effect, as you would choose delay or reverb.
2. On the send page, press **Stop + 1–4** to pick the slot the encoders edit.
   **Shift + Stop + 1–8** does the same. Slots 5–8 are not available yet.
   A popup names the slot and its effect.
3. Turn **Shift + E1** to choose the slot's effect: none, delay, reverb, chorus,
   phaser, dist or lofi.
4. Turn **E1–E4** to set the slot's parameters. The titles are the stock effect's
   own names for them. With a step held, the turns lock the step, as on any send.

A track's send amount and the send's mute work as they do for any send effect.

## How the chain sounds

The first stage processes the send's input and returns only its wet signal, as a
lone send effect does. Each later stage processes the chain so far, then adds its
input back at that stage's dry level. A reverb after a distortion therefore keeps
the distortion and adds its tail.

Each send has 1 MiB of memory for delay lines. Stock gives all of it to one
effect. MultiFX gives each stage its own part instead, so a delay, a reverb and a
chorus fit together on one send. Changing one slot's effect builds that slot
alone in free memory, so the other slots keep playing, tails and all. A stage
that does not fit stays silent until another slot frees memory.

## The page

MultiFX draws its own send page: the edited slot's parameters in the header,
then the chain as four blocks from the send's input to its output, each named
after its effect. The edited slot is lit and underlined.

## Where it lives

| Part | Files |
| --- | --- |
| Chain host (the send effect) | `veff/machines/veffmultifx` |
| Layout shared with the platform | `sdk/include/opxy/multifx.hpp` |
| Stock effect stages and the send memory allocators | `sdk/platform/opxy-1.2.1/multifx_stock.cpp`, profile hooks at `8d50e048` and `8d50dfc0` |
| Send page controls and gestures | `sdk/platform/opxy-1.2.1/multifx_ui.cpp`, wired in `synth_controls.cpp` and `module_events.cpp` |
| The installer's always-included card | `builder/veff.json` (`features`, `machine`) |

Each slot's effect is stored in send track word 47, one byte per slot. Each slot's
four parameters are stored in words 0–3, 4–7, 38–41 and 28, 34, 35, 37. All these
words are below 42, so step locks reach them.

## Limits of this prototype

- Four slots per send.
- The send's LFO moves slot 1's parameters only.
