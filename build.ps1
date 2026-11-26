param(
    [string]$QtRoot = "$PSScriptRoot\.toolchain\Qt\6.8.3\mingw_64",
    [string]$CompilerBin = "$PSScriptRoot\.toolchain\Qt\Tools\mingw1310_64\bin",
    [switch]$Deploy
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$buildDir = Join-Path $root 'build'
$localTools = Join-Path $root '.toolchain\venv\Scripts'
$env:Path = "$CompilerBin;$QtRoot\bin;$localTools;$env:Path"

function Invoke-BuildTool {
    param([string]$Tool, [string[]]$ToolArguments)
    & $Tool @ToolArguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Tool failed with exit code $LASTEXITCODE"
    }
}

Invoke-BuildTool cmake @('-S', $root, '-B', $buildDir, '-G', 'Ninja',
    "-DCMAKE_PREFIX_PATH=$QtRoot", "-DCMAKE_CXX_COMPILER=$CompilerBin\g++.exe",
    '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=ON')
Invoke-BuildTool cmake @('--build', $buildDir, '--parallel', '4')
Invoke-BuildTool ctest @('--test-dir', $buildDir, '--output-on-failure')

if ($Deploy) {
    $destination = Join-Path $root 'dist\Virtual-Lab'
    $executable = 'Virtual-Lab.exe'
    $deployedExecutable = Join-Path $destination $executable
    New-Item -ItemType Directory -Force $destination | Out-Null
    Copy-Item -LiteralPath (Join-Path $buildDir $executable) -Destination $deployedExecutable -Force
    Copy-Item -LiteralPath (Join-Path $root 'engine-tags.json') -Destination $destination -Force
    Invoke-BuildTool (Join-Path $QtRoot 'bin\windeployqt.exe') @('--release', '--no-translations', '--verbose', '0',
        '--include-plugins', 'qoffscreen,qsvg',
        '--qmldir', (Join-Path $root 'qml'), $deployedExecutable)
    Write-Output $deployedExecutable
}
