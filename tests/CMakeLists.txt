# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

include(GoogleTest)

function(openshape_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE GTest::gtest_main)
    openshape_set_warnings(${name})
    if(OPENSHAPE_OWN_OCCT)
        # Release builds: run (and list) the tests against OpenShape's own
        # OCCT build, not whichever OCCT comes first on PATH.
        set_target_properties(${name} PROPERTIES TEST_LAUNCHER "${OPENSHAPE_OCCT_TEST_LAUNCHER}")
    endif()
    gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST)
endfunction()

openshape_add_test(test_core test_units.cpp test_uuid.cpp test_math.cpp test_version.cpp)
target_link_libraries(test_core PRIVATE openshape_core)
target_compile_definitions(test_core PRIVATE OPENSHAPE_EXPECTED_VERSION="${PROJECT_VERSION}")

openshape_add_test(test_geometry test_geometry.cpp test_profiles.cpp test_occt_build.cpp)
target_link_libraries(test_geometry PRIVATE openshape_geometry)
if(OPENSHAPE_OWN_OCCT)
    target_compile_definitions(test_geometry PRIVATE OPENSHAPE_OWN_OCCT_BIN="${OPENSHAPE_OCCT_PREFIX}/bin")
endif()

openshape_add_test(test_document test_document.cpp test_commands.cpp test_project_file.cpp test_sketch_features.cpp test_attachment.cpp test_3mf.cpp test_recovery.cpp)
target_link_libraries(test_document PRIVATE openshape_commands openshape_io libzip::zip)

openshape_add_test(test_interaction test_camera.cpp test_picking.cpp test_interaction.cpp test_sketch_interaction.cpp test_history.cpp test_touch.cpp test_bodies.cpp)
target_link_libraries(test_interaction PRIVATE openshape_interaction openshape_io)

openshape_add_test(test_sketch test_sketch.cpp)
target_link_libraries(test_sketch PRIVATE openshape_sketch)

if(OPENSHAPE_BUILD_APP)
    # Settings, recent files, window placement and recovery sessions (Qt Core).
    openshape_add_test(test_uistate test_uistate.cpp)
    target_link_libraries(test_uistate PRIVATE openshape_uistate)
endif()
# Robustness: random modeling sessions (undo/redo, save/open), project-file
# fuzzing, failure messages and picking acceleration.
openshape_add_test(test_robustness test_stress.cpp test_kernel_crash.cpp test_project_fuzz.cpp test_failure_messages.cpp
    test_pick_accelerator.cpp)
target_link_libraries(test_robustness PRIVATE openshape_interaction openshape_io libzip::zip)
target_compile_definitions(test_robustness PRIVATE OPENSHAPE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
