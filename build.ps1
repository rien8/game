# gamev2 build script (PowerShell Core)
# 用法:
#   pwsh .\build.ps1            配置并编译
#   pwsh .\build.ps1 clean      清理 build 目录
#   pwsh .\build.ps1 rebuild    清理后重新配置并编译
#   pwsh .\build.ps1 run        编译后运行 gamev2.exe
#   pwsh .\build.ps1 -Config Release   指定配置（默认 Debug）

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('clean', 'rebuild', 'run', 'build')]
    [string]$Action = 'build',

    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug'
)

$ErrorActionPreference = 'Stop'

# --- 工具链路径（统一用正斜杠，避免反斜杠被 CMake 当转义符） ---
$ProjectDir   = $PSScriptRoot
$ClangCl      = 'C:/Program Files/LLVM/bin/clang-cl.exe'
$LldLink      = 'C:/Program Files/LLVM/bin/lld-link.exe'
$LlvmRc       = 'C:/Program Files/LLVM/bin/llvm-rc.exe'
$Ninja        = 'C:/Users/lenovo/vcpkg/downloads/tools/ninja-1.13.2-windows/ninja.exe'
$VcpkgTool    = 'C:/Users/lenovo/vcpkg/scripts/buildsystems/vcpkg.cmake'
$BuildDir     = Join-Path $ProjectDir 'build'
$ExePath      = Join-Path $BuildDir 'gamev2.exe'

function Invoke-Step {
    param([string]$Name, [scriptblock]$Block)
    Write-Host "=== $Name ===" -ForegroundColor Cyan
    & $Block
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERR] $Name failed (code $LASTEXITCODE)" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

function Configure {
    $cmakeArgs = @(
        '-B', $BuildDir,
        '-G', 'Ninja',
        "-DCMAKE_TOOLCHAIN_FILE=$VcpkgTool",
        "-DCMAKE_MAKE_PROGRAM=$Ninja",
        "-DCMAKE_C_COMPILER=$ClangCl",
        "-DCMAKE_CXX_COMPILER=$ClangCl",
        "-DCMAKE_LINKER=$LldLink",
        "-DCMAKE_RC_COMPILER=$LlvmRc",
        "-DCMAKE_BUILD_TYPE=$Config",
        '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'
    )
    & cmake @cmakeArgs
}

function Compile {
    cmake --build $BuildDir --config $Config
}

function Clean {
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
        Write-Host "removed $BuildDir"
    }
}

# --- 主流程 ---
Set-Location $ProjectDir

switch ($Action) {
    'clean' {
        Clean
    }
    'rebuild' {
        Clean
        Invoke-Step 'Configuring' { Configure }
        Invoke-Step 'Compiling'   { Compile }
        Write-Host "=== Build OK: $ExePath ===" -ForegroundColor Green
    }
    'run' {
        if (-not (Test-Path $ExePath)) {
            Invoke-Step 'Configuring' { Configure }
            Invoke-Step 'Compiling'   { Compile }
        }
        Write-Host "=== Running ===" -ForegroundColor Cyan
        & $ExePath
    }
    default {
        Invoke-Step 'Configuring' { Configure }
        Invoke-Step 'Compiling'   { Compile }
        Write-Host "=== Build OK: $ExePath ===" -ForegroundColor Green
    }
}