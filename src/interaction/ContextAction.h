#pragma once

#include <string>

namespace os::interact {

// A contextual command offered for the current selection.
struct ContextAction {
    std::string id;     // stable identifier passed back to triggerAction()
    std::string label;  // user-facing
    bool active = false;
};

} // namespace os::interact
