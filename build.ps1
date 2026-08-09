# Builds Simple Markdown Viewer into a single self-contained EXE.
#
#   .\build.ps1                 # run tests, then build build\SimpleMarkdownViewer.exe
#   .\build.ps1 -SkipTests      # build only
#   .\build.ps1 -DebugBuild     # unoptimised build with debug info
#   .\build.ps1 -Output C:\Tools\smv.exe
[CmdletBinding()]
param(
    [switch]$SkipTests,
    [switch]$DebugBuild,
    [string]$Output = '',
    [switch]$Run
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

. (Join-Path $root 'scripts\vcenv.ps1')

if (-not $SkipTests) {
    & (Join-Path $root 'test\run-tests.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed; build aborted.' }
    Write-Host ''
}

Enter-MsvcEnvironment

$buildDir = Join-Path $root 'build'
$objDir = Join-Path $buildDir 'obj'
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

if (-not $Output) { $Output = Join-Path $buildDir 'SimpleMarkdownViewer.exe' }
$outputDir = Split-Path -Parent $Output
if ($outputDir -and -not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
}

# md_debug.cpp only backs the test suite's structural dumps, so it stays out of
# the shipped binary.
$sources = @(
    'src\main.cpp',
    'src\win_viewer.cpp',
    'src\win_chrome.cpp',
    'src\win_outline.cpp',
    'src\win_settings.cpp',
    'src\win_theme.cpp',
    'src\win_text.cpp',
    'src\layout.cpp',
    'src\search.cpp',
    'src\highlight.cpp',
    'src\md_parser.cpp'
) | ForEach-Object { Join-Path $root $_ }

$common = @(
    '/nologo', '/std:c++17', '/EHsc', '/W4', '/WX', '/permissive-',
    '/DUNICODE', '/D_UNICODE', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX',
    '/DWINVER=0x0A00', '/D_WIN32_WINNT=0x0A00',
    "/Fo:$objDir\", "/Fe:$Output"
)

if ($DebugBuild) {
    # /MTd keeps the debug CRT statically linked: still one file to copy around.
    $compileFlags = $common + @('/MTd', '/Od', '/Zi', "/Fd:$objDir\smv.pdb")
    $linkFlags = @('/DEBUG')
} else {
    $compileFlags = $common + @('/MT', '/O2', '/Oi', '/GL', '/Gy', '/GS-', '/fp:fast')
    $linkFlags = @('/LTCG', '/OPT:REF', '/OPT:ICF', '/RELEASE')
}

$libs = @(
    'user32.lib', 'gdi32.lib', 'shell32.lib', 'comdlg32.lib', 'comctl32.lib',
    'shlwapi.lib', 'advapi32.lib', 'dwmapi.lib', 'uxtheme.lib', 'ole32.lib',
    'windowscodecs.lib', 'msimg32.lib'
)

$link = @('/link', '/SUBSYSTEM:WINDOWS', '/MANIFEST:EMBED', '/MANIFESTUAC:NO',
          '/INCREMENTAL:NO') + $linkFlags + $libs

Write-Host "Compiling $(Split-Path -Leaf $Output)..." -ForegroundColor Cyan
$log = Join-Path $buildDir 'build.log'
& cl.exe @compileFlags @sources @link > $log 2>&1
$code = $LASTEXITCODE
Get-Content $log | Where-Object {
    $_ -notmatch '^\w[\w.]*\.cpp$' -and $_ -ne 'Generating Code...' -and $_ -notmatch '^\s*$'
}
if ($code -ne 0) { throw "Build failed ($code). Full log: $log" }

Get-ChildItem $buildDir -Filter '*.obj' -ErrorAction SilentlyContinue | Remove-Item -Force

$exe = Get-Item $Output
Write-Host ''
Write-Host ("Built {0} ({1:N0} KB)" -f $exe.FullName, ($exe.Length / 1KB)) -ForegroundColor Green

if ($Run) { Start-Process -FilePath $exe.FullName }
