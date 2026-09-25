set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(openshape_ui STATIC)
qt_add_qml_module(openshape_ui
    URI OpenShape
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/OpenShape
    QML_FILES
        qml/Main.qml
        qml/ValueChip.qml
        qml/SketchOverlay.qml
        qml/HistoryPanel.qml
        qml/HelpOverlay.qml
        qml/Panel.qml
        qml/ActionButton.qml
        qml/Separator.qml
        qml/SectionLabel.qml
        qml/Theme.qml
    SOURCES
        AppController.cpp AppController.h
        ViewportItem.cpp ViewportItem.h
)
target_link_libraries(openshape_ui PUBLIC openshape_render openshape_io Qt6::Quick Qt6::QuickControls2)
target_include_directories(openshape_ui PUBLIC ${PROJECT_SOURCE_DIR}/src)
openshape_set_warnings(openshape_ui)
