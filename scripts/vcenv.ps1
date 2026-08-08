# Locates a Visual Studio C++ toolchain and imports its environment into the
# current PowerShell session. Dot-source this file, then call Enter-MsvcEnvironment.

function Find-VsInstallPath {
    if ($env:VSINSTALLDIR -and (Test-Path (Join-Path $env:VSINSTALLDIR 'VC\Auxiliary\Build\vcvarsall.bat'))) {
        return $env:VSINSTALLDIR.TrimEnd('\')
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $path = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null
        if ($path) { return ($path | Select-Object -First 1) }
    }

    foreach ($base in @("${env:ProgramFiles}\Microsoft Visual Studio", "${env:ProgramFiles(x86)}\Microsoft Visual Studio")) {
        if (-not (Test-Path $base)) { continue }
        $candidate = Get-ChildItem $base -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { Get-ChildItem $_.FullName -Directory -ErrorAction SilentlyContinue } |
            Where-Object { Test-Path (Join-Path $_.FullName 'VC\Auxiliary\Build\vcvarsall.bat') } |
            Select-Object -First 1
        if ($candidate) { return $candidate.FullName }
    }

    return $null
}

function Enter-MsvcEnvironment {
    param([string]$Arch = 'x64')

    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }

    $vsPath = Find-VsInstallPath
    if (-not $vsPath) {
        throw 'No Visual Studio C++ toolchain found. Install "Desktop development with C++" (or the Build Tools) and retry.'
    }

    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
    Write-Host "Using toolchain: $vsPath" -ForegroundColor DarkGray

    # Run vcvarsall in cmd, then copy the resulting environment into this session.
    $output = & "$env:ComSpec" /c "`"$vcvars`" $Arch >nul 2>&1 && set"
    if ($LASTEXITCODE -ne 0) { throw "vcvarsall.bat failed ($LASTEXITCODE)" }

    foreach ($line in $output) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path ("Env:" + $matches[1]) -Value $matches[2] -ErrorAction SilentlyContinue
        }
    }

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw 'cl.exe still not on PATH after importing the Visual Studio environment.'
    }
}
