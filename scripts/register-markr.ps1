<#
.SYNOPSIS
    Registers Markr as the default app for .md and .txt files and adds an
    "Edit with Markr" item to the Explorer context menu.

.DESCRIPTION
    Per-user registration (HKCU) - no admin rights needed. Safe to re-run.

    What it does:
      1. Registers a "Markr.Document" ProgID pointing at Markr.exe.
      2. Associates .md and .txt with that ProgID.
      3. Registers Markr in Settings > Default apps (RegisteredApplications).
      4. Adds "Edit with Markr" to the right-click menu for all text-based
         files, via the "text" perceived type. Extensions Windows doesn't
         already treat as text (.md, .json, .yml, most code files) are
         marked as text so they pick up the menu item too.
      5. Deletes the protected UserChoice keys so the new association takes
         effect. Windows may still show a one-time "How do you want to open
         this file?" prompt - pick Markr and check "Always".

.PARAMETER ExePath
    Path to Markr.exe. Defaults to Markr.exe in the same folder as this script.

.PARAMETER Unregister
    Removes everything this script added.

.EXAMPLE
    .\register-markr.ps1
    .\register-markr.ps1 -ExePath "C:\Tools\Markr\Markr.exe"
    .\register-markr.ps1 -Unregister
#>
[CmdletBinding()]
param(
    [string]$ExePath,
    [switch]$Unregister
)

$ErrorActionPreference = 'Stop'

$ProgId     = 'Markr.Document'
$AppName    = 'Markr'
$Extensions = @('.md', '.txt')   # extensions Markr becomes the DEFAULT app for
$Classes    = 'HKCU:\Software\Classes'

# Extensions that should show "Edit with Markr" but aren't marked as text by
# Windows out of the box. Extensions Windows already knows are text (.txt,
# .log, .ini, .bat, .c, .cpp, .h, ...) need no entry here - the menu item is
# registered once on the "text" perceived type and covers them automatically.
$MarkAsText = @(
    '.md', '.markdown', '.json', '.yml', '.yaml', '.toml', '.csv',
    '.cs', '.csproj', '.sln', '.config', '.cshtml', '.razor',
    '.js', '.ts', '.jsx', '.tsx', '.vue', '.css', '.scss', '.less',
    '.ps1', '.psm1', '.sql', '.sh', '.py', '.env', '.gitignore',
    '.editorconfig', '.props', '.targets'
)

# ---------------------------------------------------------------------------
# Resolve exe path
# ---------------------------------------------------------------------------
if (-not $Unregister) {
    if (-not $ExePath) {
        $ExePath = Join-Path $PSScriptRoot 'Markr.exe'
    }
    if (-not (Test-Path $ExePath)) {
        throw "Markr.exe not found at '$ExePath'. Place this script next to Markr.exe or pass -ExePath."
    }
    $ExePath = (Resolve-Path $ExePath).Path
}

function Set-RegDefault([string]$Path, [string]$Value) {
    if (-not (Test-Path $Path)) { New-Item -Path $Path -Force | Out-Null }
    Set-ItemProperty -Path $Path -Name '(Default)' -Value $Value
}

function Remove-RegKey([string]$Path) {
    if (Test-Path $Path) { Remove-Item -Path $Path -Recurse -Force }
}

# Windows protects the UserChoice key with a deny-write ACL and a hash, but the
# key itself can be deleted; Windows then falls back to the HKCU association.
function Remove-UserChoice([string]$Extension) {
    $key = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\$Extension\UserChoice"
    if (Test-Path $key) {
        try {
            Remove-Item -Path $key -Force
            Write-Host "  Cleared existing default for $Extension"
        } catch {
            Write-Warning "Could not clear existing default for $Extension. Windows may prompt on first open - choose Markr and check 'Always'."
        }
    }
}

# Tell Explorer that file associations changed so icons/menus refresh
function Update-Explorer {
    $signature = @'
[System.Runtime.InteropServices.DllImport("shell32.dll")]
public static extern void SHChangeNotify(int wEventId, int uFlags, System.IntPtr dwItem1, System.IntPtr dwItem2);
'@
    $shell = Add-Type -MemberDefinition $signature -Name 'Shell32Notify' -Namespace 'Markr' -PassThru
    $shell::SHChangeNotify(0x08000000, 0x0000, [IntPtr]::Zero, [IntPtr]::Zero)  # SHCNE_ASSOCCHANGED
}

# ---------------------------------------------------------------------------
# Unregister
# ---------------------------------------------------------------------------
if ($Unregister) {
    Write-Host "Unregistering $AppName..."

    Remove-RegKey "$Classes\$ProgId"
    Remove-RegKey "$Classes\Applications\Markr.exe"
    Remove-RegKey "HKCU:\Software\$AppName"

    $regApps = 'HKCU:\Software\RegisteredApplications'
    if (Test-Path $regApps) {
        Remove-ItemProperty -Path $regApps -Name $AppName -ErrorAction SilentlyContinue
    }

    # Start Menu shortcut and App Paths entry.
    $shortcutPath = Join-Path ([Environment]::GetFolderPath('Programs')) 'Markr.lnk'
    if (Test-Path $shortcutPath) { Remove-Item $shortcutPath -Force }
    Remove-RegKey 'HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\Markr.exe'

    # Context menu item (single key on the "text" perceived type).
    # PerceivedType=text markers added on extensions are left in place -
    # they are accurate metadata and other software may rely on them.
    Remove-RegKey "$Classes\SystemFileAssociations\text\shell\Markr.Edit"

    foreach ($ext in $Extensions) {
        # Only clear the extension's default if it still points at our ProgID
        $extKey = "$Classes\$ext"
        if (Test-Path $extKey) {
            $current = (Get-ItemProperty -Path $extKey -ErrorAction SilentlyContinue).'(Default)'
            if ($current -eq $ProgId) {
                Remove-ItemProperty -Path $extKey -Name '(Default)' -ErrorAction SilentlyContinue
            }
            $openWith = "$extKey\OpenWithProgids"
            if (Test-Path $openWith) {
                Remove-ItemProperty -Path $openWith -Name $ProgId -ErrorAction SilentlyContinue
            }
        }
        Remove-UserChoice $ext
    }

    Update-Explorer
    Write-Host "Done. $AppName has been unregistered."
    return
}

# ---------------------------------------------------------------------------
# Register
# ---------------------------------------------------------------------------
Write-Host "Registering $AppName from: $ExePath"

$openCommand = "`"$ExePath`" `"%1`""

# 1. ProgID - what the file types point at
Set-RegDefault "$Classes\$ProgId"                     "$AppName Document"
Set-RegDefault "$Classes\$ProgId\DefaultIcon"         "`"$ExePath`",0"
Set-RegDefault "$Classes\$ProgId\shell"               'open'
Set-RegDefault "$Classes\$ProgId\shell\open"          "Open with $AppName"
Set-RegDefault "$Classes\$ProgId\shell\open\command"  $openCommand

# 2. Application entry - makes Markr appear in the "Open with" picker
Set-RegDefault "$Classes\Applications\Markr.exe\shell\open\command" $openCommand
Set-ItemProperty -Path "$Classes\Applications\Markr.exe" -Name 'FriendlyAppName' -Value $AppName

# 3. Capabilities - makes Markr appear in Settings > Default apps
$caps = "HKCU:\Software\$AppName\Capabilities"
if (-not (Test-Path "$caps\FileAssociations")) { New-Item -Path "$caps\FileAssociations" -Force | Out-Null }
Set-ItemProperty -Path $caps -Name 'ApplicationName'        -Value $AppName
Set-ItemProperty -Path $caps -Name 'ApplicationDescription' -Value 'Markdown and text editor'
foreach ($ext in $Extensions) {
    Set-ItemProperty -Path "$caps\FileAssociations" -Name $ext -Value $ProgId
}
$regApps = 'HKCU:\Software\RegisteredApplications'
if (-not (Test-Path $regApps)) { New-Item -Path $regApps -Force | Out-Null }
Set-ItemProperty -Path $regApps -Name $AppName -Value "Software\$AppName\Capabilities"

# 4. Default-app association for .md and .txt
foreach ($ext in $Extensions) {
    Write-Host "  Associating $ext"
    Set-RegDefault "$Classes\$ext" $ProgId

    $openWith = "$Classes\$ext\OpenWithProgids"
    if (-not (Test-Path $openWith)) { New-Item -Path $openWith -Force | Out-Null }
    Set-ItemProperty -Path $openWith -Name $ProgId -Value ([byte[]]@()) -Type None

    Remove-UserChoice $ext
}

# .md files have no default content type on some systems; set a sensible one
Set-ItemProperty -Path "$Classes\.md" -Name 'Content Type' -Value 'text/markdown'

# 5. "Edit with Markr" context menu - registered ONCE on the "text" perceived
#    type. Every extension with PerceivedType=text (merged HKLM+HKCU view)
#    shows this menu item, regardless of which app is its default handler.
$menu = "$Classes\SystemFileAssociations\text\shell\Markr.Edit"
Set-RegDefault $menu "Edit with $AppName"
Set-ItemProperty -Path $menu -Name 'Icon' -Value "`"$ExePath`",0"
Set-RegDefault "$menu\command" $openCommand

# Mark additional text-based extensions as text so they pick up the menu item.
# Values merge across HKLM/HKCU per key, so adding only PerceivedType under
# HKCU does not disturb an extension's existing HKLM registration.
foreach ($ext in $MarkAsText) {
    $extKey = "$Classes\$ext"
    if (-not (Test-Path $extKey)) { New-Item -Path $extKey -Force | Out-Null }
    Set-ItemProperty -Path $extKey -Name 'PerceivedType' -Value 'text'
}

# 6. Start Menu shortcut - Windows Search indexes the Start Menu, so this is
#    what makes typing "markr" in Start find the app like any installed one.
#    The App Paths entry additionally makes Win+R "markr" work.
Write-Host '  Creating Start Menu shortcut'
$shortcutPath = Join-Path ([Environment]::GetFolderPath('Programs')) 'Markr.lnk'
$wsh = New-Object -ComObject WScript.Shell
$link = $wsh.CreateShortcut($shortcutPath)
$link.TargetPath = $ExePath
$link.WorkingDirectory = (Split-Path $ExePath -Parent)
$link.IconLocation = "$ExePath,0"
$link.Description = 'Markr - Markdown and text editor'
$link.Save()

$appPaths = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\Markr.exe'
Set-RegDefault $appPaths $ExePath
Set-ItemProperty -Path $appPaths -Name 'Path' -Value (Split-Path $ExePath -Parent)

Update-Explorer

Write-Host ''
Write-Host "Done. $AppName is registered for: $($Extensions -join ', ')"
Write-Host 'Note: if Windows shows "How do you want to open this file?" the first'
Write-Host 'time, pick Markr and check "Always" - after that it sticks.'
Write-Host 'On Windows 11, "Edit with Markr" appears under "Show more options"'
Write-Host '(Shift+F10) in the right-click menu.'
