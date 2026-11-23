# Publishes a GitHub release of Markr.exe for the version in src\version.h.
#
#   .\scripts\release.ps1                  # build, tag v<version>, publish
#   .\scripts\release.ps1 -Notes "..."     # use these notes instead of generated ones
#   .\scripts\release.ps1 -DryRun          # build and check, but publish nothing
#
# Bump MARKR_VERSION_* and MARKR_VERSION_TEXT in src\version.h, commit, then run
# this. Running copies of Markr find the release through About -> Check for
# Updates, which downloads the attached Markr.exe.
[CmdletBinding()]
param(
    [string]$Notes = '',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$header = Get-Content (Join-Path $root 'src\version.h') -Raw
if ($header -notmatch '#define\s+MARKR_VERSION_TEXT\s+"([^"]+)"') {
    throw 'MARKR_VERSION_TEXT not found in src\version.h.'
}
$version = $Matches[1]
$tag = "v$version"
Write-Host "Releasing Markr $version ($tag)" -ForegroundColor Cyan

# The release must match a commit that is on GitHub.
$status = git -C $root status --porcelain
if ($status) { throw "The working tree has uncommitted changes. Commit them first." }
git -C $root fetch --quiet origin
$branch = git -C $root rev-parse --abbrev-ref HEAD
$ahead = git -C $root rev-list --count "origin/$branch..HEAD"
if ([int]$ahead -gt 0) { throw "There are $ahead unpushed commit(s) on $branch. Push them first." }
if (git -C $root tag --list $tag) { throw "Tag $tag already exists. Bump the version in src\version.h." }

& (Join-Path $root 'build.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
$exe = Join-Path $root 'build\Markr.exe'

if ($DryRun) {
    Write-Host "Dry run: would tag $tag and publish $exe." -ForegroundColor Yellow
    return
}

git -C $root tag -a $tag -m "Markr $version"
git -C $root push origin $tag
if ($LASTEXITCODE -ne 0) { throw "Pushing tag $tag failed." }

$releaseArgs = @('release', 'create', $tag, $exe, '--title', "Markr $version")
if ($Notes) { $releaseArgs += @('--notes', $Notes) } else { $releaseArgs += '--generate-notes' }
& gh @releaseArgs
if ($LASTEXITCODE -ne 0) { throw 'gh release create failed. The tag is pushed; rerun gh by hand.' }

Write-Host "Published Markr $version." -ForegroundColor Green
