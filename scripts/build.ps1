# Configure and build gamev2 with the toolchain this machine actually has.
#
# Two things are not on PATH by default and both are required:
#   - clang++  : CMakeLists.txt pins CMAKE_CXX_COMPILER to clang++ and aborts
#     on any other compiler, so it must resolve by name.
#   - make.exe : the generator is "Unix Makefiles" and the make program is
#     D:/mingw64/bin/make.exe. The VS generator would select cl.exe before
#     the pin is read, which CMakeLists.txt rejects.
#
# 用法：
#   pwsh -File scripts/build.ps1                  # configure + build
#   pwsh -File scripts/build.ps1 -Clean           # wipe build/ first
#   pwsh -File scripts/build.ps1 -BuildType Release
#   pwsh -File scripts/build.ps1 -Asan            # GAMEV2_ENABLE_ASAN=ON
#   pwsh -File scripts/build.ps1 -Test            # build, then run all 8 suites
#   pwsh -File scripts/build.ps1 -Gui             # 加 Qt 演示程序 demo_gui
#   pwsh -File scripts/build.ps1 -Gui -QtPrefix D:\path\to\Qt\6.12.0\msvc2022_64

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string] $BuildType = 'Debug',

    [switch] $Clean,
    [switch] $Asan,
    [switch] $Test,
    [switch] $Gui,

    # Qt 6 的 msvc2022_64 目录。Qt 是可选依赖，默认构建不碰它。
    [string] $QtPrefix = 'D:\Qt\Qt5.15.2Windows\6.12.0\msvc2022_64'
)

$ErrorActionPreference = 'Stop'

$repoRoot    = Split-Path -Parent $PSScriptRoot
$buildDir    = Join-Path $repoRoot 'build'
$clangBin    = 'C:\Program Files\LLVM\bin'
$makeExe     = 'D:\mingw64\bin\make.exe'
$generator   = 'Unix Makefiles'

if (-not (Test-Path (Join-Path $clangBin 'clang++.exe'))) {
    throw "clang++ not found in '$clangBin'. Install LLVM or edit `$clangBin in this script."
}
if (-not (Test-Path $makeExe)) {
    throw "make.exe not found at '$makeExe'. Install MinGW/make or edit `$makeExe in this script."
}

# CMakeLists.txt resolves the compiler by name, so it has to be on PATH before
# configure -- not merely passed as -DCMAKE_CXX_COMPILER.
$env:PATH = "$clangBin;$(Split-Path -Parent $makeExe);$env:PATH"

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Removing stale $buildDir ..."
    Remove-Item -Recurse -Force $buildDir
}

$configureArgs = @(
    '-S', $repoRoot,
    '-B', $buildDir,
    '-G', $generator,
    '-DCMAKE_BUILD_TYPE=' + $BuildType,
    "-DCMAKE_MAKE_PROGRAM=$($makeExe -replace '\\', '/')"
)
if ($Asan) {
    $configureArgs += '-DGAMEV2_ENABLE_ASAN=ON'
}
if ($Gui) {
    if (-not (Test-Path $QtPrefix)) {
        throw "Qt prefix not found: '$QtPrefix'. Pass -QtPrefix <dir>."
    }
    $configureArgs += '-DGAMEV2_BUILD_GUI=ON'
    $configureArgs += "-DCMAKE_PREFIX_PATH=$($QtPrefix -replace '\\', '/')"
}

Write-Host "=== configure ($BuildType$(if ($Asan) { ', ASan' })$(if ($Gui) { ', GUI' })) ==="
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed with exit code $LASTEXITCODE" }

Write-Host '=== build ==='
& cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw "cmake build failed with exit code $LASTEXITCODE" }

if (-not $Test) { return }

# Every executable in the tree is a test suite; there is no separate run target.
$suites = @(
    'ecs_tests'
    'genetics_tests'
    'environment_tests'
    'ecology_tests'
    'worldgen_tests'
    'history_tests'
    'faction_tests'
    'life_tests'
)
Write-Host '=== test ==='
$failed = @()
foreach ($suite in $suites) {
    $exe = Join-Path $buildDir "$suite.exe"
    if (-not (Test-Path $exe)) { $failed += "$suite (not built)"; continue }
    & $exe
    if ($LASTEXITCODE -ne 0) { $failed += $suite }
}
if ($failed.Count -gt 0) {
    throw "test failure(s): $($failed -join ', ')"
}
Write-Host 'all suites passed'
