add_library(openshape_document STATIC
    Body.cpp Body.h
    Document.cpp Document.h
    Feature.cpp Feature.h
    JsonHelpers.cpp JsonHelpers.h
)
target_link_libraries(openshape_document PUBLIC openshape_geometry nlohmann_json::nlohmann_json)
openshape_set_warnings(openshape_document)
