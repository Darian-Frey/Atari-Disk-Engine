QT       += core gui widgets

TARGET = AtariDiskEngine
TEMPLATE = app

# Standard C++17 requirement for our engine logic
CONFIG += c++17

# The QT_CORE_LIB define enables our Qt Bridge in the header
DEFINES += QT_CORE_LIB

# Directory mapping to match our repository tree
INCLUDEPATH += include ui

HEADERS += \
    include/AtariDiskEngine.h \
    include/AtariFileSystemModel.h \
    ui/MainWindow.h \
    ui/HexViewWidget.h

SOURCES += \
    src/main.cpp \
    src/AtariDiskEngine.cpp \
    src/AtariDiskEngine_QtBridge.cpp \
    src/AtariFileSystemModel.cpp \
    ui/MainWindow.cpp \
    ui/HexViewWidget.cpp

# Output directories
DESTDIR = bin
OBJECTS_DIR = obj
MOC_DIR = obj
