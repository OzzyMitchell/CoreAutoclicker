$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$clang = Join-Path $env:ProgramFiles 'LLVM\bin\clang-cl.exe'
if (!(Test-Path -LiteralPath $clang)) { $clang = Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang-cl.exe' }
New-Item -ItemType Directory -Force -Path build | Out-Null
& cmd /d /s /c "`"$vcvars`" x64 >nul && `"$clang`" /nologo /W4 /WX /Os /clang:-Oz /Oi /Gy /Gw /D_WIN32_WINNT=0x0601 /Fo:build\scaling-test.obj /Fe:build\scaling-test.exe tests\scaling.c /link user32.lib gdi32.lib comctl32.lib dwmapi.lib msimg32.lib /opt:ref /opt:icf"
if ($LASTEXITCODE) { throw 'Scaling check failed to compile.' }
& .\build\scaling-test.exe
if ($LASTEXITCODE) { throw 'Scaling check failed.' }
