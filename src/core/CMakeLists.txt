# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

add_library(openshape_core STATIC
    Camera.cpp Camera.h
    Log.cpp Log.h
    Math.cpp Math.h
    Result.cpp Result.h
    Timer.h
    Units.cpp Units.h
    Uuid.cpp Uuid.h
)
# core/Version.h is generated from the project version (CMakeLists.txt).
configure_file(Version.h.in ${PROJECT_BINARY_DIR}/generated/core/Version.h @ONLY)
target_sources(openshape_core PRIVATE ${PROJECT_BINARY_DIR}/generated/core/Version.h)
target_include_directories(openshape_core PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/generated)
openshape_set_warnings(openshape_core)
