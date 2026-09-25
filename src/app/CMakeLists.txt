# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

qt_add_executable(openshape WIN32 MACOSX_BUNDLE
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

# macOS / iPadOS app bundle. The identifier can be changed in Xcode when signing.
if(APPLE)
    set_target_properties(openshape PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.samuelairs.openshape"
        MACOSX_BUNDLE_BUNDLE_NAME "OpenShape"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set_target_properties(openshape PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "io.github.samuelairs.openshape"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "2") # iPad only
endif()

if(OPENSHAPE_BUILD_TESTS)
    # End-to-end GUI acceptance run (needs a desktop session; label "gui").
    add_test(NAME acceptance_gui
             COMMAND openshape --acceptance ${CMAKE_BINARY_DIR}/acceptance)
    set_tests_properties(acceptance_gui PROPERTIES LABELS gui TIMEOUT 120)
endif()
