@echo off
rem Copyright (c) 2023 ����
rem SPDX-License-Identifier: MIT
rem
rem This file is part of WindowsTool and is distributed under the MIT License.
rem See the LICENSE file in the project root for the full license text.

chcp 936 >nul 2>&1
setlocal

rem 用法：winSxsClean.bat <日志文件路径>
set "LOG=%~1"
if "%LOG%"=="" set "LOG=%TEMP%\wt_sxs.log"

rem 以管理员权限重新运行自身，透传日志路径，并等待提权实例结束
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$p = Start-Process -FilePath '%~f0' -ArgumentList @('%LOG%') -Verb RunAs -WorkingDirectory '%~dp0' -Wait -PassThru; exit $p.ExitCode"
    exit /b
)

echo 正在清理 Windows 更新组件存储……> "%LOG%"
echo 会删除已被新版取代的旧更新文件，通常能腾出几个 GB 空间。>> "%LOG%"
echo 这一步只处理系统组件缓存，不会动个人文件，耗时可能几分钟到十几分钟。>> "%LOG%"
echo.>> "%LOG%"

dism.exe /Online /Cleanup-Image /StartComponentCleanup>> "%LOG%" 2>&1
set "RC=%errorlevel%"

echo.>> "%LOG%"
if "%RC%"=="0" (
    echo 更新组件清理完成。>> "%LOG%"
    echo 更新组件清理完成。
) else (
    echo 更新组件清理未完成，退出码：%RC%。>> "%LOG%"
    echo 更新组件清理未完成，请查看日志。
)
exit /b %RC%
