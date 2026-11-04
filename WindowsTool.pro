# Copyright (c) 2023 素曜
# SPDX-License-Identifier: MIT
#
# This file is part of WindowsTool and is distributed under the MIT License.
# See the LICENSE file in the project root for the full license text.

QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
# win7下运行

# 主程序不依赖 QtTest；单元测试由 CMake 目标单独链接 Qt5::Test

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
    src/dnsprobe.cpp \
    src/netdiag.cpp \
    src/processrunner.cpp \
    src/sysmonitor.cpp \
    src/treemapwidget.cpp \
    src/uiicons.cpp

HEADERS += \
    include/mainwindow.h \
    include/processrunner.h \
    include/netcheck.h \
    include/dnsprobe.h \
    include/netdiag.h \
    include/sysmonitor.h \
    include/treemapwidget.h \
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
