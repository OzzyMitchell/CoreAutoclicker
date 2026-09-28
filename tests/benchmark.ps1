param([switch]$Long)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$clang = Join-Path $env:ProgramFiles 'LLVM\bin\clang-cl.exe'
New-Item -ItemType Directory -Force -Path build | Out-Null
# Pin the original worker; git is local and no remote source is executed.
git show a6a32fc885217c5ca30850b344707d4fd6914ed7:clicker.c | Set-Content -Encoding utf8 build\baseline.c
if ($LASTEXITCODE) { throw 'Baseline revision is missing.' }
foreach ($variant in @('baseline', 'current')) {
    $define = if ($variant -eq 'baseline') { '/DBASELINE' } else { '' }
    & cmd /d /s /c "`"$vcvars`" x64 >nul && `"$clang`" /nologo /W4 /WX /Os /clang:-Oz /Oi /Gy /Gw $define /D_WIN32_WINNT=0x0601 /Fo:build\timing-$variant.obj /Fe:build\timing-$variant.exe tests\timing.c /link user32.lib gdi32.lib comctl32.lib dwmapi.lib msimg32.lib /opt:ref /opt:icf"
    if ($LASTEXITCODE) { throw "Benchmark $variant failed to compile." }
    if ($Long) {
        & ".\build\timing-$variant.exe" long | Tee-Object -FilePath "build\timing-long-$variant.jsonl"
    } else {
        & ".\build\timing-$variant.exe" | Tee-Object -FilePath "build\timing-$variant.jsonl"
    }
    if ($LASTEXITCODE) { throw "Benchmark $variant failed." }
}
