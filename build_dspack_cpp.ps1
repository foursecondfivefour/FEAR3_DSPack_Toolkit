param([string]$ZlibDllPath)
$ErrorActionPreference = 'Stop'

function Find-VcVarsAll {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($install) {
            $candidate = Join-Path $install 'VC\Auxiliary\Build\vcvarsall.bat'
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    foreach ($root in @((Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022'),(Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022'))) {
        foreach ($edition in @('Community','Professional','Enterprise','BuildTools')) {
            $candidate=Join-Path $root "$edition\VC\Auxiliary\Build\vcvarsall.bat"
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    throw 'Visual Studio x64 C++ tools not found. Install VS 2022/Build Tools with MSVC and Windows SDK.'
}

function Find-ZlibDll {
    if ($ZlibDllPath) {
        if (-not (Test-Path -LiteralPath $ZlibDllPath -PathType Leaf)) { throw "zlib1.dll not found: $ZlibDllPath" }
        return (Resolve-Path -LiteralPath $ZlibDllPath).Path
    }
    $py=Get-Command py.exe -ErrorAction SilentlyContinue
    if ($py) {
        $candidate=& $py.Source -3 -c "import sys; from pathlib import Path; print(Path(sys.prefix) / 'DLLs' / 'zlib1.dll')" 2>$null
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    foreach ($pattern in @((Join-Path $env:ProgramFiles 'Python*'),(Join-Path ${env:LocalAppData} 'Programs\Python\Python*'))) {
        foreach ($dir in Get-ChildItem -Path $pattern -Directory -ErrorAction SilentlyContinue) {
            $candidate=Join-Path $dir.FullName 'DLLs\zlib1.dll'
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        }
    }
    throw 'zlib1.dll not found. Install Python 3 or pass -ZlibDllPath <path>.'
}

$vcVars=Find-VcVarsAll
$zlibSource=Find-ZlibDll
$runtime=Join-Path $PSScriptRoot 'dspack_cpp_runtime'
$exe=Join-Path $PSScriptRoot 'dspack_cpp.exe'
$guiExe=Join-Path $PSScriptRoot 'dspack_gui.exe'
$source=Join-Path $PSScriptRoot 'dspack_cpp.cpp'
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
Copy-Item -LiteralPath $zlibSource -Destination (Join-Path $runtime 'zlib1.dll') -Force

$command='call "'+$vcVars+'" amd64 >nul 2>&1 && cl /nologo /std:c++20 /EHsc /O2 /W4 /DUNICODE /D_UNICODE /utf-8 "'+$source+'" /Fe:"'+$exe+'" /link Shell32.lib'
& $env:ComSpec /d /c $command
if ($LASTEXITCODE -ne 0) { throw "dspack_cpp build failed with exit code $LASTEXITCODE" }

$command='call "'+$vcVars+'" amd64 >nul 2>&1 && cl /nologo /std:c++20 /EHsc /O2 /W4 /DUNICODE /D_UNICODE /DDSPACK_GUI_BUILD /utf-8 "'+$source+'" /Fe:"'+$guiExe+'" /link /SUBSYSTEM:WINDOWS User32.lib Comctl32.lib Ole32.lib Shell32.lib'
& $env:ComSpec /d /c $command
if ($LASTEXITCODE -ne 0) { throw "dspack_gui build failed with exit code $LASTEXITCODE" }
Write-Output "Built $exe and $guiExe; zlib runtime copied to $runtime"
