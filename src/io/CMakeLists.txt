# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

add_library(openshape_io STATIC
    Export3mf.cpp Export3mf.h
    ProjectFile.cpp ProjectFile.h
)
target_link_libraries(openshape_io PUBLIC openshape_document PRIVATE libzip::zip)
openshape_set_warnings(openshape_io)
