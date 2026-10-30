cmake_minimum_required(VERSION 3.16)

project(WindowsTool
    VERSION 1.0.0
    DESCRIPTION "Windows 小工具箱 - Qt Widgets + bat 脚本"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 代码生成工具：moc / uic / rcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Widgets 提供 GUI。
# 注意：源码里 #include <QtTest/QTest> 只用到 QTest::qWait，
# 该符号由 QtCore 导出（qtestsupport_core.h 中 Q_CORE_EXPORT），
# 因此无需链接 Qt5::Test —— 链接后 exe 也不会导入 Qt5Test.dll，
# 反而让输出目录多背一个 DLL。
# QtTest 头文件仍可通过 Qt5::Core 的 include 根目录找到。
find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)

set(APP_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/processrunner.cpp
    src/uiicons.cpp
    include/mainwindow.h
    include/processrunner.h
    include/netcheck.h
    include/uiicons.h
    src/mainwindow.ui
    resources/ico.qrc
)

# 资源脚本（程序图标）。此处写入绝对路径，避免 rc.exe 对相对路径的解析歧义。
set(APP_RC "${CMAKE_CURRENT_BINARY_DIR}/app_resources.rc")
file(WRITE "${APP_RC}"
     "IDI_ICON1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/resources/tool.ico\"\n")

add_executable(WindowsTool WIN32 ${APP_SOURCES} "${APP_RC}")

target_include_directories(WindowsTool PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(WindowsTool PRIVATE
    Qt5::Widgets)

# 源文件为 UTF-8 无 BOM，MSVC 默认按代码页 936(GBK) 解码，
# 会导致中文字符串字面量断裂（error C2001: 常量中有换行符）。
# /utf-8 显式指定源文件与执行字符集为 UTF-8。
if(MSVC)
    target_compile_options(WindowsTool PRIVATE /utf-8)

    # 禁用 vcpkg 的 app-local 部署：本机 vcpkg 里另有一份 Qt，
    # 若不关闭，MSBuild 会把 vcpkg 的 Qt5*.dll 拷到输出目录，
    # 与实际链接的 Qt 版本不一致。运行库统一由 windeployqt 部署。
    set_property(TARGET WindowsTool PROPERTY VS_GLOBAL_VcpkgApplocalDeps false)
endif()

# bat 脚本必须在 exe 同级目录：程序按 applicationDirPath()/bat/xxx.bat 查找
add_custom_command(TARGET WindowsTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/bat"
        "$<TARGET_FILE_DIR:WindowsTool>/bat"
    COMMENT "Copying bat/ next to the executable")

# ---------------------------------------------------------------- 单元测试
# 仅在显式请求时构建：cmake --build build --target NetCheckTest
option(BUILD_TESTING "Build unit tests" OFF)
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt5 5.15 REQUIRED COMPONENTS Test)

    add_executable(NetCheckTest
        tests/netchecktest.cpp
        include/netcheck.h
        include/processrunner.h
        src/processrunner.cpp
    )
    target_include_directories(NetCheckTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(NetCheckTest PRIVATE Qt5::Test Qt5::Core)
    if(MSVC)
        target_compile_options(NetCheckTest PRIVATE /utf-8)
        # 与主程序一致：禁用 vcpkg 的 app-local 部署，
        # 否则会把 vcpkg 里的 Qt 拷进输出目录，与链接的 Qt 版本不一致
        set_property(TARGET NetCheckTest PROPERTY VS_GLOBAL_VcpkgApplocalDeps false)
    endif()

    add_test(NAME NetCheckTest COMMAND NetCheckTest)

    # 日志追加的纯文本安全性测试
    # QTextBrowser::append() 会把"像 HTML"的文本交给 HTML 解析器，
    # 导致命令输出里的尖括号被当成标签吃掉，日志内容失真。
    add_executable(LogAppendTest
        tests/logappendtest.cpp
    )
    target_link_libraries(LogAppendTest PRIVATE Qt5::Test Qt5::Widgets)
    if(MSVC)
        target_compile_options(LogAppendTest PRIVATE /utf-8)
        set_property(TARGET LogAppendTest PROPERTY VS_GLOBAL_VcpkgApplocalDeps false)
    endif()

    add_test(NAME LogAppendTest COMMAND LogAppendTest)
endif()

