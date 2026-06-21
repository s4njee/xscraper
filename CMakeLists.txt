cmake_minimum_required(VERSION 3.21)
project(xscraper VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network)
qt_standard_project_setup(REQUIRES 6.5)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(assets/app-icon.icns PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
)

add_library(xscraper_core STATIC
    src/app/ScrapeController.h
    src/app/ScrapeController.cpp
)
target_include_directories(xscraper_core PUBLIC src)
target_link_libraries(xscraper_core PUBLIC Qt6::Core Qt6::Network)

qt_add_executable(xscraper MACOSX_BUNDLE
    src/main.cpp
    assets/app-icon.icns
)
set_target_properties(xscraper PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.s4njee.xscraper"
    MACOSX_BUNDLE_BUNDLE_NAME "Xscraper"
    MACOSX_BUNDLE_DISPLAY_NAME "Xscraper"
    MACOSX_BUNDLE_ICON_FILE "app-icon.icns"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)
qt_add_qml_module(xscraper
    URI XscraperUi
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Field.qml
        qml/FlatButton.qml
        qml/Chip.qml
)
target_link_libraries(xscraper PRIVATE
    xscraper_core
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
)
