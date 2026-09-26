# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

add_library(openshape_selection STATIC
    PickAccelerator.cpp PickAccelerator.h
    Picking.cpp Picking.h
    Selection.cpp Selection.h
)
target_link_libraries(openshape_selection PUBLIC openshape_document)
openshape_set_warnings(openshape_selection)
