param(
    [ValidateSet('x64', 'x86', 'arm64')][string]$Architecture = 'x64',
    [switch]$NoPackage
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install the Visual Studio C++ desktop build tools.' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$clang = Join-Path $env:ProgramFiles 'LLVM\bin\clang-cl.exe'
if (!(Test-Path -LiteralPath $clang)) {
    $clang = Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang-cl.exe'
}
if (!(Test-Path -LiteralPath $clang)) { throw 'Install LLVM clang-cl or the Visual Studio LLVM component.' }
$target = @{ x64 = 'x86_64'; x86 = 'i686'; arm64 = 'aarch64' }[$Architecture]
$vcarch = @{ x64 = 'x64'; x86 = 'x64_x86'; arm64 = 'x64_arm64' }[$Architecture]
$out = Join-Path $PSScriptRoot "build\$Architecture"
$compilerSupport = if ($Architecture -eq 'x86') { 'libcmt.lib' } else { '' }
New-Item -ItemType Directory -Force -Path $out | Out-Null
& cmd /d /s /c "`"$vcvars`" $vcarch >nul && rc /nologo /fo `"$out\coreautoclicker.res`" coreautoclicker.rc"
if ($LASTEXITCODE) { throw 'Resource compilation failed. Check the Windows SDK and target C++ tools.' }
& cmd /d /s /c "`"$vcvars`" $vcarch >nul && `"$clang`" --target=$target-pc-windows-msvc /nologo /TC /W4 /WX /Os /clang:-Oz /Oi /GS- /Gy /Gw /Zl /D_WIN32_WINNT=0x0601 /DNDEBUG /Fo`"$out\clicker.obj`" clicker.c /link /subsystem:windows /entry:win_main_crt_startup /nodefaultlib kernel32.lib user32.lib gdi32.lib comctl32.lib dwmapi.lib msimg32.lib $compilerSupport `"$out\coreautoclicker.res`" /opt:ref /opt:icf /incremental:no /Brepro /out:`"$out\CoreAutoclicker.exe`""
if ($LASTEXITCODE) { throw 'Compilation failed.' }
if (!$NoPackage) {
    $release = Join-Path $PSScriptRoot "dist\CoreAutoclicker-win-$Architecture"
    New-Item -ItemType Directory -Force -Path $release | Out-Null
    Copy-Item -LiteralPath "$out\CoreAutoclicker.exe",'README.md','RELEASE_NOTES.md','LICENSE','NOTICE' -Destination $release
    New-Item -ItemType Directory -Force -Path "$release\assets" | Out-Null
    Copy-Item -LiteralPath 'assets\OFL-Lexend.txt','assets\README.md' -Destination "$release\assets"
    Compress-Archive -Path "$release\*" -DestinationPath "$release.zip" -Force
    Write-Host "Packaged $release.zip"
}
Get-Item -LiteralPath "$out\CoreAutoclicker.exe" | Select-Object FullName, Length
