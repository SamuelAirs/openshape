# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Layered libraries. Dependencies only point downward:
#   app -> ui -> render -> interaction -> {commands, selection} -> document -> geometry -> core
#   io -> document
# Everything below render/ui/app is Qt-free and unit tested headlessly.
add_subdirectory(core)
add_subdirectory(geometry)
add_subdirectory(sketch)
add_subdirectory(document)
add_subdirectory(commands)
add_subdirectory(selection)
add_subdirectory(interaction)
add_subdirectory(io)

if(OPENSHAPE_BUILD_APP)
    add_subdirectory(render)
    add_subdirectory(ui)
    add_subdirectory(app)
endif()
