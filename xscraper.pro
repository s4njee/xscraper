QT += core gui qml quick quickcontrols2 network
CONFIG += c++20

TARGET = xscraper
TEMPLATE = app
ICON = assets/app-icon.icns

INCLUDEPATH += src

SOURCES += \
    src/main.cpp \
    src/app/ScrapeController.cpp

HEADERS += \
    src/app/ScrapeController.h

RESOURCES += qml.qrc
