# PlaneGCS: FreeCAD's 2D geometric constraint solver, vendored unmodified
# (see README.md) and built as a SHARED library so the LGPL-2.1-or-later
# code stays separately replaceable whatever OpenShape's own license becomes.
find_package(Eigen3 REQUIRED NO_MODULE)

set(PLANEGCS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Sketcher/App/planegcs)
# iOS apps cannot ship plain dynamic libraries: static there. The LGPL's
# relinking option is then met by the complete source code of the app and
# its build scripts at each release tag (docs/LICENSING.md, "The iOS app and
# the App Store"; BUILDING.md, "Rebuilding the iOS app with modified libraries").
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(PLANEGCS_LIBRARY_TYPE STATIC)
else()
    set(PLANEGCS_LIBRARY_TYPE SHARED)
endif()
add_library(planegcs ${PLANEGCS_LIBRARY_TYPE}
    ${PLANEGCS_DIR}/Constraints.cpp
    ${PLANEGCS_DIR}/GCS.cpp
    ${PLANEGCS_DIR}/Geo.cpp
    ${PLANEGCS_DIR}/SubSystem.cpp
    ${PLANEGCS_DIR}/qp_eq.cpp
)
# Upstream FreeCAD uses C++23 (e.g. std::unreachable); OpenShape itself stays C++20.
set_target_properties(planegcs PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON CXX_STANDARD 23)
# Shims come first so they win over any system Boost/FreeCAD headers.
target_include_directories(planegcs BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shim)
target_include_directories(planegcs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Sketcher/App)
# Geo.h (a public header) includes an unused Boost.Math header; consumers get a stub.
target_include_directories(planegcs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shim-public)
target_compile_definitions(planegcs PRIVATE _USE_MATH_DEFINES)
# FreeCAD builds these files with a precompiled header that pulls in common
# standard headers; the sources rely on it. Reproduce it (force-included).
# <iterator> etc.: libc++ (Apple) includes less transitively than libstdc++.
target_precompile_headers(planegcs PRIVATE
    <algorithm> <cassert> <cmath> <cstdlib> <fstream> <future> <iostream> <iterator> <limits>
    <map> <memory> <numbers> <optional> <set> <sstream> <string> <utility> <vector>)
target_link_libraries(planegcs PUBLIC Eigen3::Eigen)
# Third-party code: do not apply the project warning policy.
if(NOT MSVC)
    target_compile_options(planegcs PRIVATE -w)
endif()
