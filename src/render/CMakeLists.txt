# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

add_library(openshape_render STATIC
    ViewportRenderer.cpp ViewportRenderer.h
)
# QRhi is a "limited compatibility" API exposed through GuiPrivate.
target_link_libraries(openshape_render PUBLIC openshape_interaction Qt6::Gui Qt6::GuiPrivate Qt6::Quick)
openshape_set_warnings(openshape_render)

# GLSL 440 sources compiled to .qsb (SPIR-V + HLSL + MSL + GLSL variants),
# embedded at :/openshape/shaders/<name>.qsb
qt_add_shaders(openshape_render "openshape_shaders"
    PREFIX "/openshape"
    FILES
        shaders/mesh.vert
        shaders/mesh.frag
        shaders/line.vert
        shaders/line.frag
        shaders/grid.vert
        shaders/grid.frag
        shaders/shadow.frag
)
