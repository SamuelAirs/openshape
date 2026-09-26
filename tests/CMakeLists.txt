# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

include(GoogleTest)

function(openshape_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE GTest::gtest_main)
    openshape_set_warnings(${name})
    gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST)
endfunction()

openshape_add_test(test_core test_units.cpp test_uuid.cpp test_math.cpp test_version.cpp)
target_link_libraries(test_core PRIVATE openshape_core)
target_compile_definitions(test_core PRIVATE OPENSHAPE_EXPECTED_VERSION="${PROJECT_VERSION}")

openshape_add_test(test_geometry test_geometry.cpp test_profiles.cpp)
target_link_libraries(test_geometry PRIVATE openshape_geometry)

openshape_add_test(test_document test_document.cpp test_commands.cpp test_project_file.cpp test_sketch_features.cpp test_attachment.cpp test_3mf.cpp)
target_link_libraries(test_document PRIVATE openshape_commands openshape_io libzip::zip)

openshape_add_test(test_interaction test_camera.cpp test_picking.cpp test_interaction.cpp test_sketch_interaction.cpp test_history.cpp test_touch.cpp)
target_link_libraries(test_interaction PRIVATE openshape_interaction openshape_io)

openshape_add_test(test_sketch test_sketch.cpp)
target_link_libraries(test_sketch PRIVATE openshape_sketch)
