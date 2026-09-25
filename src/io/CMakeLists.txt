add_library(openshape_io STATIC
    ProjectFile.cpp ProjectFile.h
)
target_link_libraries(openshape_io PUBLIC openshape_document PRIVATE libzip::zip)
openshape_set_warnings(openshape_io)
