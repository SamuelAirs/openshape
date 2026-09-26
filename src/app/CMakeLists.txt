# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Acceptance scenarios register themselves (one file per feature area).
file(GLOB OPENSHAPE_ACCEPTANCE_SCENARIOS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/acceptance/*.cpp)
qt_add_executable(openshape WIN32 MACOSX_BUNDLE
    main.cpp
    AcceptanceRunner.cpp AcceptanceRunner.h
    ${OPENSHAPE_ACCEPTANCE_SCENARIOS}
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
# With a static Qt (iOS), Qt scans the app's QML to link the plugins of the
# modules it imports; the QML lives in src/ui, not beside main.cpp. Without
# this the iPad app linked no QML plugins at all (the scan found nothing).
set_target_properties(openshape PROPERTIES
    QT_QML_IMPORT_SCANNER_EXTRA_ROOT_PATHS "${PROJECT_SOURCE_DIR}/src/ui/qml")

# macOS / iPadOS app bundle. The identifier is the App ID registered with
# Apple for TestFlight (docs/IPAD.md). Every TestFlight upload needs a new
# build number: CI passes its run number.
set(OPENSHAPE_BUILD_NUMBER "${PROJECT_VERSION}" CACHE STRING "Bundle build number (CFBundleVersion)")
if(APPLE)
    set_target_properties(openshape PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.samuelairs.openshape"
        MACOSX_BUNDLE_BUNDLE_NAME "OpenShape"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${OPENSHAPE_BUILD_NUMBER}")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # App icon (one 1024 px image; Xcode derives the other sizes) and the
    # privacy manifest App Store Connect requires.
    target_sources(openshape PRIVATE ios/Assets.xcassets ios/PrivacyInfo.xcprivacy)
    set_source_files_properties(ios/Assets.xcassets ios/PrivacyInfo.xcprivacy
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(openshape PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "io.github.samuelairs.openshape"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "2" # iPad only
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
        # Only the app goes into an archive (the libraries set SKIP_INSTALL).
        XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
        XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)")
endif()

if(OPENSHAPE_BUILD_TESTS)
    # End-to-end GUI acceptance run (needs a desktop session; label "gui").
    add_test(NAME acceptance_gui
             COMMAND openshape --acceptance ${CMAKE_BINARY_DIR}/acceptance)
    # The timeout includes waiting for another automated GUI run to finish
    # (they take turns: see the automation lock in main.cpp).
    set_tests_properties(acceptance_gui PROPERTIES LABELS gui TIMEOUT 900)
endif()
