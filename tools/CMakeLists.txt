# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Developer tools (off by default: -DOPENSHAPE_BUILD_TOOLS=ON). Qt-free.
add_executable(bench_session bench/bench_session.cpp)
target_link_libraries(bench_session PRIVATE openshape_interaction)
openshape_set_warnings(bench_session)
