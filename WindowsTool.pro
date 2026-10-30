QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
# win7下运行

# 源码里 #include <QtTest/QTest> 只用到 QTest::qWait，
# 该符号由 QtCore 导出，因此不需要 QT += test（否则会多链一个 Qt5Test.dll）

# 源文件为 UTF-8 无 BOM，MSVC 默认按 GBK 解码会导致中文字符串字面量断裂
win32-msvc: QMAKE_CXXFLAGS += /utf-8

# 头文件搜索路径（mainwindow.h 独立存放）
INCLUDEPATH += $$PWD/include

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0
RC_ICONS = resources/tool.ico
SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/processrunner.cpp \
    src/uiicons.cpp

HEADERS += \
    include/mainwindow.h \
    include/processrunner.h \
    include/netcheck.h \
    include/uiicons.h

FORMS += \
    src/mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    resources/ico.qrc

DISTFILES +=
