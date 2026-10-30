<#
.SYNOPSIS
    WindowsTool 一键构建脚本（CMake + Visual Studio 生成器）。

.DESCRIPTION
    自动探测 Qt5 与 MSVC，调用 CMake 生成 Visual Studio 工程文件（.sln / .vcxproj），
    并完成编译。可选部署 Qt 运行库到输出目录。

    生成器为 "Visual Studio 17 2022"（VS2022）。若本机仅有 VS2017 或其他版本，
    脚本会自动降级到可用的生成器。

.PARAMETER QtPath
    Qt5 安装根目录（含 bin/ lib/ include/），需为 msvc 套件。
    默认自动探测：优先 D:\Qt、Q:\Qt、C:\Qt 下形如 <ver>\msvc*_64 的目录。

.PARAMETER BuildDir
    构建输出目录，默认为仓库根下的 build/。

.PARAMETER Config
    构建配置：Release（默认）或 Debug。

.PARAMETER Arch
    目标架构：x64（默认）或 Win32。

.PARAMETER Generator
    覆盖自动探测的 CMake 生成器名称。

.PARAMETER Deploy
    部署 Qt 运行库到输出目录（默认已开启，此参数可显式指定）。

.PARAMETER NoDeploy
    跳过 Qt 运行库部署（默认会部署，保证输出目录可直接运行）。

.PARAMETER Test
    额外构建并运行单元测试（NetCheckTest + LogAppendTest）。

.PARAMETER Clean
    构建前清空构建目录。

.EXAMPLE
    .\build.ps1
    一键生成 VS 工程并编译 Release x64。

.EXAMPLE
    .\build.ps1 -Config Debug -Deploy
    编译 Debug 版本并部署 Qt 运行库。

.EXAMPLE
    .\build.ps1 -QtPath D:\Qt\5.15.2\msvc2019_64
    指定 Qt 路径构建。
#>
[CmdletBinding()]
param(
    [string]$QtPath,
    [string]$BuildDir = "build",
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [ValidateSet("x64", "Win32")]
    [string]$Arch = "x64",
    [string]$Generator,
    [switch]$Deploy,
    [switch]$NoDeploy,
    [switch]$Test,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# 让控制台正确输出中文（否则 Windows 控制台按 GBK 渲染，显示为乱码）
try {
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    $OutputEncoding = [System.Text.Encoding]::UTF8
} catch { }

$RepoRoot = $PSScriptRoot
if (-not $RepoRoot) { $RepoRoot = (Get-Location).Path }

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Info($msg) { Write-Host "    $msg" -ForegroundColor Gray }
function Write-Ok($msg)   { Write-Host "[OK] $msg" -ForegroundColor Green }
function Write-Warn2($msg){ Write-Host "[WARN] $msg" -ForegroundColor Yellow }
function Write-Err2($msg) { Write-Host "[ERR] $msg" -ForegroundColor Red }

# ---------------------------------------------------------------- 工具探测
function Find-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($p in @("D:\cmake-gui\bin\cmake.exe", "D:\cmake\bin\cmake.exe",
                     "C:\Program Files\CMake\bin\cmake.exe")) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

function Find-QtPath {
    if ($env:QT_ROOT) { return $env:QT_ROOT }

    $roots = @("D:\Qt", "Q:\Qt", "C:\Qt", "$env:USERPROFILE\Qt")
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        # 形如 <root>\<version>\msvc2019_64
        $cands = Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
                 ForEach-Object {
                     Get-ChildItem -Path $_.FullName -Directory -ErrorAction SilentlyContinue |
                     Where-Object { $_.Name -like "msvc*" } |
                     Sort-Object Name -Descending
                 }
        foreach ($c in $cands) {
            if (Test-Path (Join-Path $c.FullName "lib\cmake\Qt5")) {
                return $c.FullName
            }
        }
    }
    return $null
}

# 探测可用的 Visual Studio 生成器
# 用 vswhere 的 -version 范围查询，比解析 json 更可靠
function Find-VSGenerator {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { $vswhere = "D:\Microsoft Visual Studio\Installer\vswhere.exe" }
    if (-not (Test-Path $vswhere)) { return $null }

    $candidates = @(
        @{ Gen = "Visual Studio 17 2022"; Ver = 17 },
        @{ Gen = "Visual Studio 16 2019"; Ver = 16 },
        @{ Gen = "Visual Studio 15 2017"; Ver = 15 }
    )

    foreach ($c in $candidates) {
        $major = $c.Ver
        $range = "[$major.0,$($major + 1).0)"
        $path = & $vswhere -all -products * -version $range -property installationPath 2>$null
        if ($path) { return $c.Gen }
    }
    return $null
}

# 调用原生命令：临时放宽 ErrorActionPreference，
# 避免 stderr 输出被当作终止性错误（PowerShell 5.1 的已知行为）
function Invoke-Native {
    param([string]$Exe, [string[]]$Arguments)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $Exe @Arguments 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prev
    }
    foreach ($line in $output) {
        $text = if ($line -is [System.Management.Automation.ErrorRecord]) { $line.ToString() }
                else { [string]$line }
        Write-Host $text
    }
    return $code
}

# 修复 MSBuild 的环境变量大小写冲突。
# 若环境块中同时存在 HTTP_PROXY / http_proxy 这类仅大小写不同的同名变量，
# MSBuild 把环境变量灌入大小写不敏感的 Hashtable 时会抛
# "已添加项目。字典中的关键字: HTTPS_PROXY"，导致 cl.exe 无法启动、
# CMake 报 "No CMAKE_CXX_COMPILER could be found"。
function Remove-DuplicateCaseEnvVars {
    $block = [System.Environment]::GetEnvironmentVariables()
    $seen = @{}
    foreach ($key in @($block.Keys)) {
        $name = [string]$key
        $lower = $name.ToLowerInvariant()
        if ($seen.ContainsKey($lower)) {
            # 已有同名（小写形式）变量，删除当前这一个，保留首个
            [System.Environment]::SetEnvironmentVariable($name, $null, "Process")
            Write-Info "已移除重复环境变量：$name"
        } else {
            $seen[$lower] = $name
        }
    }
}

# ---------------------------------------------------------------- 主流程
Write-Step "WindowsTool 构建"

$cmake = Find-CMake
if (-not $cmake) {
    Write-Err2 "未找到 cmake。请安装 CMake 并加入 PATH，或安装到 D:\cmake-gui。"
    exit 1
}
Write-Info "cmake : $cmake"

if (-not $QtPath) { $QtPath = Find-QtPath }
if (-not $QtPath) {
    Write-Err2 "未找到 Qt5 msvc 套件。请用 -QtPath 指定，例如 -QtPath D:\Qt\5.15.2\msvc2019_64"
    exit 1
}
if (-not (Test-Path (Join-Path $QtPath "lib\cmake\Qt5"))) {
    Write-Err2 "QtPath 不含 lib\cmake\Qt5，不是有效的 Qt5 根目录：$QtPath"
    exit 1
}
Write-Info "Qt    : $QtPath"

if (-not $Generator) {
    $Generator = Find-VSGenerator
    if (-not $Generator) {
        Write-Err2 "未找到 Visual Studio 安装。请安装 VS2022/2019/2017，或用 -Generator 手动指定。"
        exit 1
    }
}
Write-Info "生成器 : $Generator  ($Arch)"

# Qt 5.15 的 cmake 配置文件仍带 cmake_minimum_required(VERSION 2.8)，
# 而 CMake 4.x 已移除 <3.5 兼容，因此需要放开策略版本下限。
$env:CMAKE_POLICY_VERSION_MINIMUM = "3.5"

# 清理仅大小写不同的重复环境变量（否则 MSBuild 无法启动 cl.exe）
Remove-DuplicateCaseEnvVars

$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir }
             else { Join-Path $RepoRoot $BuildDir }

# ---------------------------------------------------------------- 配置
Write-Step "配置 CMake（生成 Visual Studio 工程文件）"

$configureArgs = @(
    "-S", $RepoRoot,
    "-B", $buildPath,
    "-G", $Generator,
    "-A", $Arch,
    "-DCMAKE_PREFIX_PATH=$QtPath"
)

# Visual Studio 生成器是多配置（Multi-Config），构建类型由 --config 决定，
# 此时传 CMAKE_BUILD_TYPE 会被 CMake 报 "Manually-specified variables were not used"
if ($Generator -notlike "Visual Studio*") {
    $configureArgs += "-DCMAKE_BUILD_TYPE=$Config"
}

# 单元测试需要单独开启
if ($Test) {
    $configureArgs += "-DBUILD_TESTING=ON"
}

# 生成器变化时必须清掉旧缓存，否则 CMake 会报
# "Does not match the generator used previously"。
# 使用 CMake 自带的 --fresh（>=3.24）来重建缓存，避免手工删除目录。
$cache = Join-Path $buildPath "CMakeCache.txt"
if (Test-Path $cache) {
    $cachedGen = (Select-String -Path $cache -Pattern "^CMAKE_GENERATOR:INTERNAL=(.+)$" -ErrorAction SilentlyContinue |
                  Select-Object -First 1).Matches.Groups[1].Value
    if ($cachedGen -and $cachedGen -ne $Generator) {
        Write-Warn2 "生成器由 '$cachedGen' 变为 '$Generator'，重建缓存"
        $configureArgs = @("--fresh") + $configureArgs
    }
}

# -Clean：同样借助 --fresh 重建（增量产物由编译器自行覆盖）
if ($Clean) {
    Write-Step "重建构建目录（--fresh）"
    $configureArgs = @("--fresh") + $configureArgs
}

$rc = Invoke-Native -Exe $cmake -Arguments $configureArgs
if ($rc -ne 0) {
    Write-Err2 "CMake 配置失败（退出码 $rc）"
    exit $rc
}

$sln = Get-ChildItem -Path $buildPath -Filter *.sln -ErrorAction SilentlyContinue | Select-Object -First 1
if ($sln) { Write-Ok "已生成 VS 工程：$($sln.FullName)" }

# ---------------------------------------------------------------- 编译
Write-Step "编译 $Config"
$rc = Invoke-Native -Exe $cmake -Arguments @("--build", $buildPath, "--config", $Config)
if ($rc -ne 0) {
    Write-Err2 "编译失败（退出码 $rc）"
    exit $rc
}

$exe = Join-Path $buildPath "$Config\WindowsTool.exe"
if (-not (Test-Path $exe)) {
    $exe = Get-ChildItem -Path $buildPath -Recurse -Filter "WindowsTool.exe" -ErrorAction SilentlyContinue |
           Select-Object -First 1 -ExpandProperty FullName
}
if ($exe) { Write-Ok "产物：$exe" } else { Write-Warn2 "未找到 WindowsTool.exe，请检查输出目录" }

# 默认随构建自动部署 Qt 运行库，保证输出目录（Debug / Release）可直接运行。
#
# 注意：这一步必须在"构建单元测试"之前执行！
# windeployqt 以目录方式扫描时，如果目录里已有单元测试 exe
# （LogAppendTest / NetCheckTest，依赖 Qt5Test），它可能挑中测试 exe，
# 导致 platforms/ 等插件子目录没被部署，主程序启动时报
# "no Qt platform plugin could be initialized"。
if (-not $NoDeploy) {
    $outDir = Join-Path $buildPath $Config
    $windeployqt = Join-Path $QtPath "bin\windeployqt.exe"
    $isDebug = ($Config -eq "Debug")

    if (Test-Path $windeployqt) {
        Write-Step "部署 Qt 运行库（$Config）"

        # 必须与构建配置匹配：
        # Debug 构建要用 --debug，否则只会拷入 release 的 Qt5*.dll，
        # 缺少 Qt5Widgetsd.dll，程序启动时报
        # "由于找不到 Qt5Widgetsd.dll，无法继续执行代码"。
        $deployArgs = @("--no-translations", "--no-system-d3d-compiler", "--no-opengl-sw")
        if ($isDebug) { $deployArgs += "--debug" } else { $deployArgs += "--release" }
        $deployArgs += $outDir

        $rc = Invoke-Native -Exe $windeployqt -Arguments $deployArgs
        if ($rc -ne 0) {
            Write-Warn2 "windeployqt 返回非零退出码 $rc"
        } else {
            Write-Ok "运行库已部署到 $Config 目录"
        }

        # windeployqt 对同名文件会跳过（提示 "is up to date"），
        # 这里强制用构建所用 Qt 的 DLL 覆盖，保证运行库与链接的 Qt 版本一致。
        # Debug 版 DLL 名为 Qt5Xxx d.dll（如 Qt5Widgetsd.dll）。
        # 不含 Qt5Test：exe 并未导入它（QTest::qWait 由 QtCore 导出）。
        $suffix = if ($isDebug) { "d" } else { "" }
        $qtBin = Join-Path $QtPath "bin"
        foreach ($dll in @("Qt5Core$suffix.dll", "Qt5Gui$suffix.dll", "Qt5Widgets$suffix.dll")) {
            $src = Join-Path $qtBin $dll
            if (Test-Path $src) {
                Copy-Item -Path $src -Destination (Join-Path $outDir $dll) -Force
                Write-Info "同步 $dll"
            }
        }
        Write-Ok "已强制同步构建所用 Qt 的核心 DLL"
    } else {
        Write-Warn2 "未找到 windeployqt.exe：$windeployqt"
    }
} else {
    Write-Info "已跳过 Qt 运行库部署（-NoDeploy）"
}

# ---------------------------------------------------------------- 单元测试
# 放在部署之后：windeployqt 以目录方式扫描时若目录里已有测试 exe，
# 可能挑中它，导致 platforms/ 等插件子目录漏部署。
if ($Test) {
    Write-Step "构建单元测试"

    $testTargets = @("NetCheckTest", "LogAppendTest")
    foreach ($target in $testTargets) {
        $rc = Invoke-Native -Exe $cmake -Arguments @(
            "--build", $buildPath, "--config", $Config, "--target", $target
        )
        if ($rc -ne 0) {
            Write-Err2 "$target 构建失败（退出码 $rc）"
            exit $rc
        }
    }

    # 测试目标需要自己的运行库（Qt5Test），单独补齐
    $suffix = if ($Config -eq "Debug") { "d" } else { "" }
    $qtBin = Join-Path $QtPath "bin"
    foreach ($dll in @("Qt5Test$suffix.dll", "Qt5Core$suffix.dll",
                       "Qt5Gui$suffix.dll", "Qt5Widgets$suffix.dll")) {
        $src = Join-Path $qtBin $dll
        if (Test-Path $src) {
            Copy-Item -Path $src -Destination (Join-Path $buildPath $Config) -Force
        }
    }

    foreach ($target in $testTargets) {
        $testExe = Join-Path $buildPath "$Config\$target.exe"
        Write-Step "运行 $target"
        $rc = Invoke-Native -Exe $testExe -Arguments @()
        if ($rc -ne 0) {
            Write-Err2 "$target 失败（退出码 $rc）"
            exit $rc
        }
    }
    Write-Ok "单元测试全部通过"
}

Write-Step "完成"
exit 0
