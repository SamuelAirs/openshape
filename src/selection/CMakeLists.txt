add_library(openshape_selection STATIC
    Picking.cpp Picking.h
    Selection.cpp Selection.h
)
target_link_libraries(openshape_selection PUBLIC openshape_document)
openshape_set_warnings(openshape_selection)
