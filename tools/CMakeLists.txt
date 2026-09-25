# Developer tools (off by default: -DOPENSHAPE_BUILD_TOOLS=ON). Qt-free.
add_executable(bench_session bench/bench_session.cpp)
target_link_libraries(bench_session PRIVATE openshape_interaction)
openshape_set_warnings(bench_session)
