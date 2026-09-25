#pragma once

#include "core/Math.h"

// Application-level input vocabulary. UI toolkits translate their native
// events (mouse, touch, stylus, trackpad, keyboard) into these, so the
// interaction logic never depends on a particular framework.
namespace os::interact {

enum class PointerDevice { Mouse, Touch, Pen };
enum class PointerButton { None, Left, Middle, Right };

struct Modifiers {
    bool shift = false;
    bool control = false;
    bool alt = false;
};

struct PointerEvent {
    PointerDevice device = PointerDevice::Mouse;
    PointerButton button = PointerButton::Left; // button that changed (press/release) or is held (move)
    Vec2 position;                               // logical pixels in the viewport
    Modifiers modifiers;
};

enum class Key { Escape, Enter, Delete, Backspace, Other };

// Device-dependent tolerances. Touch targets are much larger than mouse ones.
struct InputProfile {
    double pickTolerance = 6;      // edge picking, px
    double handleTolerance = 10;   // manipulator grab radius, px
    double dragThreshold = 4;      // px before a press becomes a drag
    bool additiveSelection = false; // tap adds to selection instead of replacing

    static InputProfile forDevice(PointerDevice device)
    {
        switch (device) {
        case PointerDevice::Touch: return {18, 28, 10, true};
        case PointerDevice::Pen: return {9, 16, 6, true};
        case PointerDevice::Mouse: break;
        }
        return {};
    }
};

} // namespace os::interact
