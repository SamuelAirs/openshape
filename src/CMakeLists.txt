# Layered libraries. Dependencies only point downward:
#   app -> ui -> render -> interaction -> commands -> document -> geometry -> core
add_subdirectory(core)
add_subdirectory(geometry)
