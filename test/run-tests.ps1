# Builds and runs the parser/layout/search test suite.
# Usage: .\test\run-tests.ps1 [-Filter Blocks] [-KeepIntermediate]
[CmdletBinding()]
param(
    [string]$Filter = '',
    [switch]$KeepIntermediate
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

. (Join-Path $root 'scripts\vcenv.ps1')
Enter-MsvcEnvironment

$outDir = Join-Path $root 'build\test'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$sources = @(
    'src\md_parser.cpp',
    'src\md_debug.cpp',
    'src\layout.cpp',
    'src\search.cpp',
    'src\highlight.cpp'
) | ForEach-Object { Join-Path $root $_ } | Where-Object { Test-Path $_ }
$sources += (Get-ChildItem (Join-Path $root 'test') -Filter '*.cpp' | ForEach-Object { $_.FullName })

$exe = Join-Path $outDir 'mdtests.exe'

Write-Host 'Compiling tests...' -ForegroundColor Cyan
$clArgs = @(
    '/nologo', '/std:c++17', '/EHsc', '/W4', '/WX', '/wd4100', '/O2', '/MT',
    '/DUNICODE', '/D_UNICODE', '/D_CRT_SECURE_NO_WARNINGS',
    "/Fo:$outDir\", "/Fe:$exe"
) + $sources

$log = Join-Path $outDir 'build.log'
& cl.exe @clArgs > $log 2>&1
$buildCode = $LASTEXITCODE
Get-Content $log | Where-Object { $_ -notmatch '^\w[\w.]*\.cpp$' -and $_ -ne 'Generating Code...' }
if ($buildCode -ne 0) { throw "Test compilation failed ($buildCode)" }

Write-Host ''
& $exe $Filter
$code = $LASTEXITCODE

if (-not $KeepIntermediate) {
    Get-ChildItem $outDir -Filter '*.obj' | Remove-Item -Force
}

if ($code -ne 0) { throw "Tests failed ($code)" }
Write-Host 'All tests passed.' -ForegroundColor Green
