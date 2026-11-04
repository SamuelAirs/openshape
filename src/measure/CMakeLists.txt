# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Measuring real things with the device (docs/MEASURE.md): the screen's true
# scale, a photo straightened onto a known rectangle, a traced stroke fitted
# to lines, arcs and circles, the fold's angle, the level, and the list of
# measurements kept for designing. Pure math, Qt-free: the UI and the
# platform's sensors and camera (src/ui) feed it numbers.
add_library(openshape_measure STATIC
    Fold.cpp Fold.h
    Homography.cpp Homography.h
    Level.cpp Level.h
    Measurement.cpp Measurement.h
    Outline.cpp Outline.h
    ScanStereo.cpp ScanStereo.h
    ScanTypes.cpp ScanTypes.h
    ScreenScale.cpp ScreenScale.h
    StrokeFit.cpp StrokeFit.h
)
find_package(Threads REQUIRED)
target_link_libraries(openshape_measure PUBLIC openshape_core Threads::Threads PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(openshape_measure PUBLIC ${PROJECT_SOURCE_DIR}/src)
openshape_set_warnings(openshape_measure)
