# Framework-independent interaction logic (no Qt).
add_library(openshape_interaction STATIC
    InputEvents.h
    InteractionController.cpp InteractionController.h
    Manipulator.cpp Manipulator.h
    Operation.cpp Operation.h
    RenderScene.h
    SceneCache.cpp SceneCache.h
    SketchSession.cpp SketchSession.h
    TouchGestures.cpp TouchGestures.h
    ContextAction.h
)
target_link_libraries(openshape_interaction PUBLIC openshape_commands openshape_selection)
openshape_set_warnings(openshape_interaction)
