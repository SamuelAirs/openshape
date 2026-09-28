# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

add_library(openshape_core STATIC
    Camera.cpp Camera.h
    Lighting.cpp Lighting.h
    Log.cpp Log.h
    Math.cpp Math.h
    Result.cpp Result.h
    Timer.h
    Units.cpp Units.h
    Uuid.cpp Uuid.h
)
# core/Version.h is generated from the project version (CMakeLists.txt),
# with the release tag of App Store / public beta builds
# (scripts/ios/build-app.sh). The git commit (About -> Licenses, the LGPL's
# relinking notice) is in core/SourceCommit.h, which SourceCommit.cmake
# rewrites on every build: a commit made after configuring still gets named,
# and a build with uncommitted changes names none.
set(OPENSHAPE_SOURCE_COMMIT "" CACHE STRING "Git commit the build is made from (empty: git's HEAD at build time when no tracked file is changed)")
set(OPENSHAPE_RELEASE_TAG "" CACHE STRING "Release tag the build is made from (v<version>...; empty: not a release build)")
if(NOT OPENSHAPE_SOURCE_COMMIT MATCHES "^[0-9a-f]*$")
    message(FATAL_ERROR "OPENSHAPE_SOURCE_COMMIT must be a git commit id (hex), not '${OPENSHAPE_SOURCE_COMMIT}'")
endif()
if(NOT OPENSHAPE_RELEASE_TAG MATCHES "^(v[0-9][0-9A-Za-z._-]*)?$")
    message(FATAL_ERROR "OPENSHAPE_RELEASE_TAG must be empty or v<version>, not '${OPENSHAPE_RELEASE_TAG}'")
endif()
find_package(Git QUIET)
add_custom_target(openshape_source_commit
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT=${PROJECT_BINARY_DIR}/generated/core/SourceCommit.h
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DCOMMIT=${OPENSHAPE_SOURCE_COMMIT}
        -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/SourceCommit.cmake
    BYPRODUCTS ${PROJECT_BINARY_DIR}/generated/core/SourceCommit.h
    COMMENT "Naming the source commit (core/SourceCommit.h)"
    VERBATIM)
add_dependencies(openshape_core openshape_source_commit)
configure_file(Version.h.in ${PROJECT_BINARY_DIR}/generated/core/Version.h @ONLY)
target_sources(openshape_core PRIVATE ${PROJECT_BINARY_DIR}/generated/core/Version.h)
target_include_directories(openshape_core PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/generated)
openshape_set_warnings(openshape_core)
