add_library(openshape_commands STATIC
    Command.cpp Command.h
    DocumentCommands.cpp DocumentCommands.h
)
target_link_libraries(openshape_commands PUBLIC openshape_document)
openshape_set_warnings(openshape_commands)
