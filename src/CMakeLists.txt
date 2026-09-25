# Layered libraries. Dependencies only point downward:
#   app -> ui -> render -> interaction -> commands -> document -> geometry -> core
#   io -> document; selection -> geometry
add_subdirectory(core)
add_subdirectory(geometry)
add_subdirectory(document)
add_subdirectory(commands)
add_subdirectory(io)
