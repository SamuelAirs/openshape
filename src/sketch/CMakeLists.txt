# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# 2D sketch model and constraint solving. Only SketchSolver.cpp sees PlaneGCS.
add_library(openshape_sketch STATIC
    Sketch.cpp Sketch.h
    SketchSolver.cpp
)
target_link_libraries(openshape_sketch
    PUBLIC openshape_core nlohmann_json::nlohmann_json
    PRIVATE planegcs)
openshape_set_warnings(openshape_sketch)
