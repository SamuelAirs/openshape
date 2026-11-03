# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The sample projects' build steps (OpenShape --write-samples, tests/test_samples).
add_library(openshape_samples STATIC
    Samples.cpp Samples.h
)
target_link_libraries(openshape_samples PUBLIC openshape_commands)
openshape_set_warnings(openshape_samples)
