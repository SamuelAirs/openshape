add_library(openshape_core STATIC
    Log.cpp Log.h
    Math.cpp Math.h
    Result.cpp Result.h
    Timer.h
    Units.cpp Units.h
    Uuid.cpp Uuid.h
)
target_include_directories(openshape_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
openshape_set_warnings(openshape_core)
