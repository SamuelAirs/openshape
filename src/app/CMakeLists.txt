# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Acceptance scenarios register themselves (one file per feature area).
file(GLOB OPENSHAPE_ACCEPTANCE_SCENARIOS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/acceptance/*.cpp)
qt_add_executable(openshape WIN32 MACOSX_BUNDLE
    main.cpp
    AcceptanceRunner.cpp AcceptanceRunner.h
    CrashLog.cpp CrashLog.h
    FaceContrast.cpp FaceContrast.h
    StoreScenes.cpp StoreScenes.h
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
# The Text tool's font, Noto Sans (SIL Open Font License 1.1; see
# resources/fonts/README.md and THIRD_PARTY.md), built into the executable:
# AppController registers it with the geometry layer at startup. Without it
# the app still builds, and the Text tool says that text is not available.
set(OPENSHAPE_FONT_FILES)
foreach(font NotoSans-Regular.ttf NotoSans-Bold.ttf)
    if(EXISTS ${PROJECT_SOURCE_DIR}/resources/fonts/${font})
        list(APPEND OPENSHAPE_FONT_FILES ${PROJECT_SOURCE_DIR}/resources/fonts/${font})
    endif()
endforeach()
if(OPENSHAPE_FONT_FILES)
    qt_add_resources(openshape "openshape_fonts"
        PREFIX "/openshape"
        BASE ${PROJECT_SOURCE_DIR}/resources
        FILES ${OPENSHAPE_FONT_FILES}
    )
else()
    message(WARNING "resources/fonts/NotoSans-Regular.ttf is missing (see resources/fonts/README.md): "
                    "the Text tool will say that text is not available in this build.")
endif()
set_target_properties(openshape PROPERTIES OUTPUT_NAME "OpenShape")
if(WIN32)
    # Icon and version information of OpenShape.exe (Explorer, shortcuts,
    # the installer). The .ico is made from the SVG by scripts/windows/make-icon.py.
    set(OPENSHAPE_ICON_FILE "${PROJECT_SOURCE_DIR}/resources/icons/openshape.ico")
    configure_file(openshape.rc.in ${CMAKE_CURRENT_BINARY_DIR}/openshape.rc @ONLY)
    target_sources(openshape PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/openshape.rc)
    set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/openshape.rc APPEND PROPERTY OBJECT_DEPENDS ${OPENSHAPE_ICON_FILE})
    # The "release" acceptance scenario reads the running exe's version resource.
    target_link_libraries(openshape PRIVATE version)
endif()
# With a static Qt (iOS), Qt scans the app's QML to link the plugins of the
# modules it imports; the QML lives in src/ui, not beside main.cpp. Without
# this the iPad app linked no QML plugins at all (the scan found nothing).
set_target_properties(openshape PROPERTIES
    QT_QML_IMPORT_SCANNER_EXTRA_ROOT_PATHS "${PROJECT_SOURCE_DIR}/src/ui/qml")

# macOS / iOS app bundle. The identifier is the App ID registered with
# Apple for TestFlight (docs/IPAD.md); someone rebuilding the app for their
# own devices (the LGPL's relinking, BUILDING.md) sets their own with
# -DOPENSHAPE_BUNDLE_ID=... (Apple ties an identifier to one team). Every
# TestFlight upload needs a new build number (OPENSHAPE_BUILD_NUMBER, top
# level): CI passes its run number.
set(OPENSHAPE_BUNDLE_ID "io.github.samuelairs.openshape" CACHE STRING "Bundle identifier of the macOS / iOS app")
if(APPLE)
    set_target_properties(openshape PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "${OPENSHAPE_BUNDLE_ID}"
        MACOSX_BUNDLE_BUNDLE_NAME "OpenShape"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${OPENSHAPE_BUILD_NUMBER}")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # App icon (one 1024 px image; Xcode derives the other sizes for iPhone
    # and iPad), the launch screen's color and the privacy manifest App Store
    # Connect requires.
    target_sources(openshape PRIVATE ios/Assets.xcassets ios/PrivacyInfo.xcprivacy)
    set_source_files_properties(ios/Assets.xcassets ios/PrivacyInfo.xcprivacy
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(openshape PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${OPENSHAPE_BUNDLE_ID}"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2" # universal: iPhone and iPad
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
        # Only the app goes into an archive (the libraries set SKIP_INSTALL).
        XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
        XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
        # The linker's map of every file linked into the app: the license
        # gate (scripts/ios/build-app.sh, scripts/licenses/licenses.py gate)
        # checks each one's origin against the Licenses view and the pinned
        # sources (docs/LICENSING.md).
        XCODE_ATTRIBUTE_LD_GENERATE_MAP_FILE "YES"
        XCODE_ATTRIBUTE_LD_MAP_FILE_PATH "${CMAKE_BINARY_DIR}/OpenShape-LinkMap.txt")

    # TD-36: Qt's QML import scan links every Qt Quick Controls style, because
    # QtQuick.Controls imports them optionally and QtQuick.Dialogs has
    # per-style variants. OpenShape only uses Basic (QQuickStyle::setStyle in
    # main.cpp), so unlink the other style plugins once Qt has finalized the
    # target (its finalizer runs deferred at the end of this directory; this
    # runs at the end of the top-level one). Lists what it dropped for the
    # build script's report.
    function(openshape_drop_unused_controls_styles target)
        get_target_property(libs ${target} LINK_LIBRARIES)
        set(kept "")
        set(dropped "")
        foreach(lib IN LISTS libs)
            if(lib MATCHES "^Qt6::qtquickcontrols2(material|fusion|universal|imagine|fluentwinui3|ios|macos|windows)style(impl)?plugin$")
                list(APPEND dropped "${lib}")
            else()
                list(APPEND kept "${lib}")
            endif()
        endforeach()
        set_property(TARGET ${target} PROPERTY LINK_LIBRARIES "${kept}")
        get_target_property(binary_dir ${target} BINARY_DIR)
        list(JOIN dropped " " dropped_text)
        file(WRITE "${binary_dir}/.qt/qml_imports/${target}_dropped_styles.txt" "${dropped_text}\n")
        message(STATUS "Controls style plugins not linked (TD-36): ${dropped_text}")
    endfunction()
    cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}" CALL openshape_drop_unused_controls_styles openshape)
endif()

if(OPENSHAPE_BUILD_TESTS)
    if(OPENSHAPE_OWN_OCCT)
        # Release builds: ctest runs the app with OpenShape's own OCCT first on PATH.
        set_target_properties(openshape PROPERTIES TEST_LAUNCHER "${OPENSHAPE_OCCT_TEST_LAUNCHER}")
    endif()
    # End-to-end GUI acceptance run (needs a desktop session; label "gui").
    add_test(NAME acceptance_gui
             COMMAND openshape --acceptance ${CMAKE_BINARY_DIR}/acceptance)
    # The timeout includes waiting for another automated GUI run to finish
    # (they take turns: see the automation lock in main.cpp).
    set_tests_properties(acceptance_gui PROPERTIES LABELS gui TIMEOUT 900)
endif()
