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

openshape_add_test(test_core test_units.cpp test_uuid.cpp test_math.cpp test_version.cpp test_lighting.cpp)
target_link_libraries(test_core PRIVATE openshape_core)
target_compile_definitions(test_core PRIVATE OPENSHAPE_EXPECTED_VERSION="${PROJECT_VERSION}")

openshape_add_test(test_geometry test_geometry.cpp test_profiles.cpp test_occt_build.cpp test_draft.cpp test_step.cpp
    test_text_geometry.cpp test_kernel_threads.cpp)
target_link_libraries(test_geometry PRIVATE openshape_geometry)
# Text tests read the bundled font from the source tree (tests/TestFonts.h).
target_compile_definitions(test_geometry PRIVATE OPENSHAPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
if(OPENSHAPE_OWN_OCCT)
    target_compile_definitions(test_geometry PRIVATE OPENSHAPE_OWN_OCCT_BIN="${OPENSHAPE_OCCT_PREFIX}/bin")
endif()

openshape_add_test(test_document test_document.cpp test_commands.cpp test_project_file.cpp test_sketch_features.cpp test_attachment.cpp test_datums.cpp test_3mf.cpp test_recovery.cpp test_holes.cpp test_extrude_draft.cpp test_import.cpp test_project_thumbnail.cpp test_home.cpp
    test_text.cpp)
target_link_libraries(test_document PRIVATE openshape_commands openshape_io libzip::zip)
target_compile_definitions(test_document PRIVATE OPENSHAPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

openshape_add_test(test_interaction test_camera.cpp test_picking.cpp test_interaction.cpp test_overlay_placement.cpp test_sketch_interaction.cpp test_sketch_drag_interaction.cpp test_history.cpp test_touch.cpp test_touch_wording.cpp test_bodies.cpp test_holes_interaction.cpp test_draft_interaction.cpp test_import_interaction.cpp test_thumbnail.cpp
    test_text_interaction.cpp test_async_preview.cpp test_contact_shadow.cpp test_datum_interaction.cpp
    test_cut_flow.cpp test_touch_selection.cpp)
target_link_libraries(test_interaction PRIVATE openshape_interaction openshape_io)
# test_touch_wording reads the QML hints and the help card; test_bodies a
# project saved by an older build (tests/data).
target_compile_definitions(test_interaction PRIVATE OPENSHAPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    OPENSHAPE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

openshape_add_test(test_sketch test_sketch.cpp test_sketch_drag.cpp)
target_link_libraries(test_sketch PRIVATE openshape_sketch)

if(OPENSHAPE_BUILD_APP)
    # Settings, recent files, window placement and recovery sessions (Qt Core).
    openshape_add_test(test_uistate test_uistate.cpp)
    target_link_libraries(test_uistate PRIVATE openshape_uistate)
    # The iOS Info.plist's document types are checked against what the app takes.
    target_compile_definitions(test_uistate PRIVATE OPENSHAPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
endif()
# Robustness: random modeling sessions (undo/redo, save/open), project-file
# fuzzing, failure messages and picking acceleration.
openshape_add_test(test_robustness test_stress.cpp test_kernel_crash.cpp test_project_fuzz.cpp test_failure_messages.cpp
    test_pick_accelerator.cpp)
target_link_libraries(test_robustness PRIVATE openshape_interaction openshape_io libzip::zip)
target_compile_definitions(test_robustness PRIVATE OPENSHAPE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

# The Windows release scripts (docs/CODE_SIGNING.md), which otherwise only
# run when a tag is released: the release notes template in both variants
# (signed, unsigned), and the check that a file SignPath returns is the sent
# program plus a signature, run on the OpenShape.exe built here. release.yml
# runs both itself as well. Windows only, like the release; a test whose
# interpreter is missing is left out, with a message.
if(WIN32)
    # The bash of the MSYS2 or Git shell, never WSL's launcher in System32.
    get_filename_component(openshape_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(openshape_msys_usr_bin "${openshape_compiler_bin}/../../usr/bin")
    find_program(OPENSHAPE_BASH bash PATHS "${openshape_msys_usr_bin}")
    if(OPENSHAPE_BASH MATCHES "[Ss]ystem32|WindowsApps")
        unset(OPENSHAPE_BASH CACHE)
        find_program(OPENSHAPE_BASH bash PATHS "${openshape_msys_usr_bin}" NO_DEFAULT_PATH)
    endif()
    if(OPENSHAPE_BASH)
        add_test(NAME release_notes_template
                 COMMAND "${OPENSHAPE_BASH}" "${PROJECT_SOURCE_DIR}/scripts/ci/test-install-notes.sh")
        # awk, grep and tr from beside that bash, whichever shell ctest runs in.
        get_filename_component(openshape_bash_dir "${OPENSHAPE_BASH}" DIRECTORY)
        set_tests_properties(release_notes_template PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${openshape_bash_dir}")
    else()
        message(STATUS "No bash found: the release notes test (release_notes_template) is left out")
    endif()

    if(OPENSHAPE_BUILD_APP)
        set(Python3_FIND_REGISTRY LAST) # prefer the MSYS2 Python on PATH
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME release_pe_signature_selftest
                     COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/windows/pe-signature.py"
                             self-test $<TARGET_FILE:openshape>)
        else()
            message(STATUS "No Python 3 found: the signature check test (release_pe_signature_selftest) is left out")
        endif()
    endif()
endif()
