// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <string>

namespace os::interact {

// A contextual command offered for the current selection.
struct ContextAction {
    std::string id;     // stable identifier passed back to triggerAction()
    std::string label;  // user-facing
    bool active = false;
    bool operator==(const ContextAction&) const = default;
};

} // namespace os::interact
