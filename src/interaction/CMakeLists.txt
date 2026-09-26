# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Framework-independent interaction logic (no Qt).
add_library(openshape_interaction STATIC
    InputEvents.h
    InteractionController.cpp InteractionController.h
    Manipulator.cpp Manipulator.h
    Operation.cpp Operation.h
    OverlayPlacement.cpp OverlayPlacement.h
    RenderScene.h
    SceneCache.cpp SceneCache.h
    SketchSession.cpp SketchSession.h
    Thumbnail.cpp Thumbnail.h
    TouchGestures.cpp TouchGestures.h
    TouchWording.cpp TouchWording.h
    ContextAction.h
)
target_link_libraries(openshape_interaction PUBLIC openshape_commands openshape_selection)
openshape_set_warnings(openshape_interaction)
