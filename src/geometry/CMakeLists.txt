# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The only library allowed to include OpenCASCADE headers.
add_library(openshape_geometry STATIC
    DraftExtrude.cpp
    Exchange.cpp Exchange.h
    Holes.cpp Holes.h
    Mesh.h
    Modeling.cpp Modeling.h KernelSignals.h
    Profiles.cpp Profiles.h
    Shape.cpp Shape.h
    Tessellation.cpp Tessellation.h
    Text.cpp Text.h
    TopoSignature.cpp TopoSignature.h
    internal/KernelUtil.h
    internal/ShapeData.h
)
target_link_libraries(openshape_geometry
    PUBLIC openshape_core
    PRIVATE
        TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep
        TKGeomAlgo TKTopAlgo TKPrim TKBO TKBool TKShHealing TKFillet TKOffset TKFeat TKMesh
        TKDE TKXSBase TKDESTEP TKDESTL
        TKCDF TKLCAF TKCAF TKXCAF # STEP product names (XCAF document)
        TKService TKV3d # text: Font_FTFont (FreeType) and StdPrs_BRepFont (glyphs as faces)
)
target_include_directories(openshape_geometry SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
# OCCT headers use M_PI, which strict ISO mode hides on MinGW/MSVC.
target_compile_definitions(openshape_geometry PRIVATE _USE_MATH_DEFINES)
# An OCCT built with OCC_CONVERT_SIGNALS (MSYS2, Homebrew, Linux packages)
# turns crashes inside the kernel into exceptions at OCC_CATCH_SIGNALS (see
# internal/KernelUtil.h). Its package config exports the definition for the
# Release configuration only; match it in every configuration.
get_directory_property(OPENSHAPE_OCCT_DEFINITIONS COMPILE_DEFINITIONS)
if(OPENSHAPE_OCCT_DEFINITIONS MATCHES "OCC_CONVERT_SIGNALS")
    target_compile_definitions(openshape_geometry PRIVATE OCC_CONVERT_SIGNALS)
endif()
openshape_set_warnings(openshape_geometry)
