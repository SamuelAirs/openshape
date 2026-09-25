qt_add_executable(openshape WIN32
    main.cpp
    AcceptanceRunner.cpp AcceptanceRunner.h
)
# GuiPrivate: QWindowSystemInterface for realistic input injection in --acceptance.
target_link_libraries(openshape PRIVATE openshape_ui openshape_uiplugin Qt6::Quick Qt6::QuickControls2 Qt6::GuiPrivate)
openshape_set_warnings(openshape)
qt_add_resources(openshape "openshape_icons"
    PREFIX "/openshape"
    BASE ${PROJECT_SOURCE_DIR}/resources
    FILES ${PROJECT_SOURCE_DIR}/resources/icons/openshape.svg
)
set_target_properties(openshape PROPERTIES OUTPUT_NAME "OpenShape")

if(OPENSHAPE_BUILD_TESTS)
    # End-to-end GUI acceptance run (needs a desktop session; label "gui").
    add_test(NAME acceptance_gui
             COMMAND openshape --acceptance ${CMAKE_BINARY_DIR}/acceptance)
    set_tests_properties(acceptance_gui PROPERTIES LABELS gui TIMEOUT 120)
endif()
