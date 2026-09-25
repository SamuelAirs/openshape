# The only library allowed to include OpenCASCADE headers.
add_library(openshape_geometry STATIC
    Exchange.cpp Exchange.h
    Mesh.h
    Modeling.cpp Modeling.h
    Profiles.cpp Profiles.h
    Shape.cpp Shape.h
    Tessellation.cpp Tessellation.h
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
)
target_include_directories(openshape_geometry SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
# OCCT headers use M_PI, which strict ISO mode hides on MinGW/MSVC.
target_compile_definitions(openshape_geometry PRIVATE _USE_MATH_DEFINES)
openshape_set_warnings(openshape_geometry)
