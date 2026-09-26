// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <string>
#include <string_view>

namespace os::interact {

// Rewrites a hint, prompt or message written for mouse and keyboard for the
// touch layout: taps instead of clicks, and none of Shift-click, Esc, Enter,
// Tab, Ctrl, Alt, hovering, the scroll wheel or the middle button (touch has
// none of them; the on-screen ✓ / ✕, − / + and action buttons do their
// jobs). Known sentences get a hand-written touch version; anything else
// gets the word-level rules ("click" -> "tap", "Double-click" ->
// "Double-tap", ...). Text without such words comes back unchanged.
std::string touchWording(std::string_view text);

// True if `text` still speaks of the mouse or keyboard (for tests: every
// touch hint must pass).
bool mentionsMouseOrKeyboard(std::string_view text);

} // namespace os::interact
