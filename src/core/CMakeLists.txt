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
# with the source the build is made from (About -> Licenses, the LGPL's
# relinking notice): the git commit at configure time unless given, and the
# release tag of App Store / public beta builds (scripts/ios/build-app.sh).
set(OPENSHAPE_SOURCE_COMMIT "" CACHE STRING "Git commit the build is made from (empty: git rev-parse HEAD at configure time)")
set(OPENSHAPE_RELEASE_TAG "" CACHE STRING "Release tag the build is made from (v<version>...; empty: not a release build)")
if(OPENSHAPE_SOURCE_COMMIT STREQUAL "")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            OUTPUT_VARIABLE openshape_git_commit OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE openshape_git_result ERROR_QUIET)
        if(openshape_git_result EQUAL 0 AND openshape_git_commit MATCHES "^[0-9a-f]+$")
            set(OPENSHAPE_SOURCE_COMMIT "${openshape_git_commit}")
        endif()
    endif()
endif()
if(NOT OPENSHAPE_SOURCE_COMMIT MATCHES "^[0-9a-f]*$")
    message(FATAL_ERROR "OPENSHAPE_SOURCE_COMMIT must be a git commit id (hex), not '${OPENSHAPE_SOURCE_COMMIT}'")
endif()
if(NOT OPENSHAPE_RELEASE_TAG MATCHES "^(v[0-9][0-9A-Za-z._-]*)?$")
    message(FATAL_ERROR "OPENSHAPE_RELEASE_TAG must be empty or v<version>, not '${OPENSHAPE_RELEASE_TAG}'")
endif()
configure_file(Version.h.in ${PROJECT_BINARY_DIR}/generated/core/Version.h @ONLY)
target_sources(openshape_core PRIVATE ${PROJECT_BINARY_DIR}/generated/core/Version.h)
target_include_directories(openshape_core PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/generated)
openshape_set_warnings(openshape_core)
