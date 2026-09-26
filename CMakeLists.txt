# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

cmake_minimum_required(VERSION 3.25)

project(OpenShape
    VERSION 0.1.0
    DESCRIPTION "Direct, precise solid CAD"
    LANGUAGES CXX)

if(WIN32)
    # The executable's icon and version information (src/app/openshape.rc.in).
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OPENSHAPE_BUILD_APP   "Build the Qt desktop application" ON)
option(OPENSHAPE_BUILD_TESTS "Build unit and integration tests" ON)
option(OPENSHAPE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(OPENSHAPE_BUILD_TOOLS "Build developer tools (benchmarks)" OFF)
option(OPENSHAPE_OWN_OCCT "Use OpenShape's own OpenCASCADE build (scripts/windows/build-occt.sh; release packages) instead of the system one" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(cmake/CompilerWarnings.cmake)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # An Xcode archive (TestFlight/App Store) must contain only the app, not
    # the static libraries it is built from; the app target overrides this.
    set(CMAKE_XCODE_ATTRIBUTE_SKIP_INSTALL "YES")
endif()

# ---- Third-party dependencies --------------------------------------------
# The modeling core (core/geometry/document/commands/selection/interaction/io)
# deliberately does NOT depend on Qt. Only render/ui/app do.
if(OPENSHAPE_OWN_OCCT)
    # Release builds (preset msys2-ucrt64-release): OCCT built without its GPL
    # media dependencies by scripts/windows/build-occt.sh, installed in
    # $OPENSHAPE_OCCT_PREFIX (default: <home>/opt/occt-7.9.3-openshape).
    # Only there: the system's OCCT must not be picked up by accident.
    if(DEFINED ENV{OPENSHAPE_OCCT_PREFIX})
        set(openshape_occt_default "$ENV{OPENSHAPE_OCCT_PREFIX}")
    elseif(DEFINED ENV{USERPROFILE})
        set(openshape_occt_default "$ENV{USERPROFILE}/opt/occt-7.9.3-openshape")
    else()
        set(openshape_occt_default "$ENV{HOME}/opt/occt-7.9.3-openshape")
    endif()
    file(TO_CMAKE_PATH "${openshape_occt_default}" openshape_occt_default)
    set(OPENSHAPE_OCCT_PREFIX "${openshape_occt_default}" CACHE PATH "Install prefix of scripts/windows/build-occt.sh")
    find_package(OpenCASCADE REQUIRED CONFIG
        PATHS "${OPENSHAPE_OCCT_PREFIX}/lib/cmake/opencascade" NO_DEFAULT_PATH)
    message(STATUS "OpenCASCADE ${OpenCASCADE_VERSION}: OpenShape's own build in ${OPENSHAPE_OCCT_PREFIX}")
else()
    find_package(OpenCASCADE REQUIRED)
endif()
find_package(nlohmann_json 3.10 REQUIRED)
find_package(libzip REQUIRED)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # OpenCASCADE's static libraries name FreeType just "freetype"; it is
    # installed beside them (scripts/ios/build-deps.sh).
    link_directories(${OpenCASCADE_LIBRARY_DIR})
endif()

if(OPENSHAPE_BUILD_APP)
    # The renderer uses QRhi, whose headers ship via GuiPrivate with limited
    # compatibility guarantees: builds are tied to the exact Qt version they
    # were compiled against. Accepted deliberately (see ARCHITECTURE.md).
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui GuiPrivate Qml Quick QuickControls2 ShaderTools)
    qt_standard_project_setup(REQUIRES 6.8)
endif()

if(OPENSHAPE_BUILD_TESTS)
    enable_testing() # before src/: the app registers its GUI acceptance test
endif()

add_subdirectory(third_party/planegcs)
add_subdirectory(src)

if(OPENSHAPE_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(OPENSHAPE_BUILD_TESTS)
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()
