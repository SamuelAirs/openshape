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
)
