#pragma once
// Host-facing hooks into built-in objects: input state, timers, save files.

#include <cstdint>
#include <string>

#include "as/Runtime.h"

namespace as {

// Flash key codes (Key.LEFT etc.). The platform layer maps its own codes to these.
namespace keycode {
enum : int {
    Backspace = 8, Tab = 9, Enter = 13, Shift = 16, Control = 17, Alt = 18, CapsLock = 20,
    Escape = 27, Space = 32, PageUp = 33, PageDown = 34, End = 35, Home = 36, Left = 37,
    Up = 38, Right = 39, Down = 40, Insert = 45, Delete = 46,
};
}

// Key state and Key/Mouse listener broadcasts (onKeyDown, onMouseMove...).
void keyEvent(int code, int ascii, bool down);
bool isKeyDown(int code);
void mouseEvent(const char* method);  // "onMouseDown", "onMouseUp", "onMouseMove"

// Runs setInterval callbacks that are due.
void runIntervals(double nowMs);

// Directory where SharedObject files are stored (default: current directory).
void setSaveDirectory(const std::string& dir);
// Writes every SharedObject that changed since loading (call on quit).
void flushSharedObjects();

// Mark roots owned by built-ins (listeners, intervals, shared objects).
void traceBuiltins(Gc& gc);

}  // namespace as
