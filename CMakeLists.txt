cmake_minimum_required(VERSION 3.25)

project(OpenShape
    VERSION 0.1.0
    DESCRIPTION "Direct, precise solid CAD"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OPENSHAPE_BUILD_APP   "Build the Qt desktop application" ON)
option(OPENSHAPE_BUILD_TESTS "Build unit and integration tests" ON)
option(OPENSHAPE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(cmake/CompilerWarnings.cmake)

# ---- Third-party dependencies --------------------------------------------
# The modeling core (core/geometry/document/commands/selection/interaction/io)
# deliberately does NOT depend on Qt. Only render/ui/app do.
find_package(OpenCASCADE REQUIRED)
find_package(nlohmann_json 3.10 REQUIRED)
find_package(libzip REQUIRED)

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

add_subdirectory(src)

if(OPENSHAPE_BUILD_TESTS)
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()
