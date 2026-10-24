#pragma once
// Script registry used by ported game code: frame scripts, onClipEvent()/on() handlers
// and button actions, keyed by where they live in the SWF. Kept free of display-list
// headers so the (large) generated code only depends on the runtime.

#include <cstdint>
#include <vector>

#include "as/Runtime.h"

namespace as {

using ScriptFn = void (*)(Activation& scope);

// onClipEvent(...) / on(...) flags, matching the SWF CLIPEVENTFLAGS bit order.
namespace clipevent {
enum : uint32_t {
    Load = 1u << 0, EnterFrame = 1u << 1, Unload = 1u << 2, MouseMove = 1u << 3,
    MouseDown = 1u << 4, MouseUp = 1u << 5, KeyDown = 1u << 6, KeyUp = 1u << 7,
    Data = 1u << 8, Initialize = 1u << 9, Press = 1u << 10, Release = 1u << 11,
    ReleaseOutside = 1u << 12, RollOver = 1u << 13, RollOut = 1u << 14, DragOver = 1u << 15,
    DragOut = 1u << 16, KeyPress = 1u << 17, Construct = 1u << 18,
    ButtonEvents = Press | Release | ReleaseOutside | RollOver | RollOut | DragOver | DragOut,
};
}

// on(...) conditions for button characters (BUTTONCONDACTION flags, little-endian).
namespace buttoncond {
enum : uint32_t {
    RollOver = 0x0001,        // IdleToOverUp
    RollOut = 0x0002,         // OverUpToIdle
    Press = 0x0004,           // OverUpToOverDown
    Release = 0x0008,         // OverDownToOverUp
    DragOut = 0x0010,         // OverDownToOutDown
    DragOver = 0x0020,        // OutDownToOverDown
    ReleaseOutside = 0x0040,  // OutDownToIdle
};
}

// ---------------------------------------------------------------- script registry

// Frame script: the `actionIndex`-th DoAction tag inside sprite `spriteId` (0 = root).
void registerFrameScript(uint16_t spriteId, uint32_t actionIndex, ScriptFn fn);
// onClipEvent handler on the `placeIndex`-th PlaceObject inside sprite `spriteId`.
void registerClipEvent(uint16_t spriteId, uint32_t placeIndex, uint32_t events, ScriptFn fn);
// on(...) handler of a button character.
void registerButtonAction(uint16_t buttonId, uint32_t conditions, ScriptFn fn);

ScriptFn findFrameScript(uint16_t spriteId, uint32_t actionIndex);
struct ClipHandler {
    uint32_t events;
    ScriptFn fn;
};
std::vector<ClipHandler> findClipEvents(uint16_t spriteId, uint32_t placeIndex);
std::vector<ClipHandler> findButtonActions(uint16_t buttonId);

}  // namespace as
