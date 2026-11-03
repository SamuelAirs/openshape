// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "commands/Command.h"
#include "core/Result.h"
#include "document/Document.h"

#include <string>
#include <string_view>
#include <vector>

namespace os::samples {

// The sample projects that come with the app (Home's Samples row). Each is
// built here step by step, through the undo stack as a user's steps are:
// sized sketches, construction planes and features, so every step stays
// editable in the Model panel. `OpenShape --write-samples <dir>` saves them
// (with their thumbnails) as resources/samples/<name>.openshape, the files
// the app ships; tests/test_samples.cpp checks those files against a fresh
// build and their sizes.
struct SampleInfo {
    std::string name;    // the file name and the title on Home ("Lamp shade")
    std::string summary; // what it shows, one line for Home
};
const std::vector<SampleInfo>& sampleList();

// Builds the sample called `name` into `document` (empty), every step pushed
// on `stack`. Text needs the bundled fonts registered (geom::registerFont).
// Fails with the first step that failed.
Status buildSample(std::string_view name, doc::Document& document, cmd::UndoStack& stack);

} // namespace os::samples
