# Configure and build ecs-module with the toolchain this machine actually has.
#
# Two things are not on PATH by default and both are required:
#   - clang++  : CMakeLists.txt pins CMAKE_CXX_COMPILER to clang++ and aborts
#                on any other compiler, so it must resolve by name.
#   - ninja.exe: no Makefile generator on this box (no make / mingw32-make),
#                and the VS generator would select cl.exe before the pin is
#                read, which CMakeLists.txt rejects.
# Both live outside PATH; Ninja additionally ships only inside VS BuildTools.
#
# Usage:
#   pwsh -File scripts/build.ps1                  # configure + build
#   pwsh -File scripts/build.ps1 -Clean           # wipe build/ first
#   pwsh -File scripts/build.ps1 -BuildType Release
#   pwsh -File scripts/build.ps1 -Asan            # GAMEV2_ENABLE_ASAN=ON
#   pwsh -File scripts/build.ps1 -Test            # build, then run all 8 suites
#   pwsh -File scripts/build.ps1 -Test -Asan      # ASan build + run suites

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string] $BuildType = 'Debug',

    [switch] $Clean,
    [switch] $Asan,
    [switch] $Test
)

$ErrorActionPreference = 'Stop'

$repoRoot    = Split-Path -Parent $PSScriptRoot
$buildDir    = Join-Path $repoRoot 'build'
$clangBin    = 'C:\Program Files\LLVM\bin'
$ninjaRoot   = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$ninjaExe    = Join-Path $ninjaRoot 'ninja.exe'

if (-not (Test-Path (Join-Path $clangBin 'clang++.exe'))) {
    throw "clang++ not found in '$clangBin'. Install LLVM or edit `$clangBin in this script."
}
if (-not (Test-Path $ninjaExe)) {
    throw "ninja.exe not found at '$ninjaExe'. Install VS BuildTools or edit `$ninjaRoot in this script."
}

# CMakeLists.txt resolves the compiler by name, so it has to be on PATH before
# configure -- not merely passed as -DCMAKE_CXX_COMPILER.
$env:PATH = "$clangBin;$ninjaRoot;$env:PATH"

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Removing stale $buildDir ..."
    Remove-Item -Recurse -Force $buildDir
}

$configureArgs = @(
    '-S', $repoRoot,
    '-B', $buildDir,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=' + $BuildType,
    "-DCMAKE_MAKE_PROGRAM=$($ninjaExe -replace '\\', '/')"
)
if ($Asan) {
    $configureArgs += '-DGAMEV2_ENABLE_ASAN=ON'
}

Write-Host "=== configure ($BuildType$(if ($Asan) { ', ASan' })) ==="
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
