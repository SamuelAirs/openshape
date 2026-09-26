# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Installs OpenShape silently into a test folder, checks everything the
# installer promises (files, Apps & features entry, .openshape association,
# Start-menu shortcut; optionally that the installed app starts, that an
# upgrade over it works and waits for a running app), uninstalls it and
# checks that everything is gone again - and that nothing of the user's
# (settings, data folder, a desktop shortcut of their own) was touched.
# Refuses to run when OpenShape is already installed for this user.
#
# Usage (Windows PowerShell 5.1 or pwsh):
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\windows\test-installer.ps1 `
#       -Setup dist\OpenShape-0.1.0-windows-x64-setup.exe -InstallDir build\installer-test\OpenShape `
#       [-PackageDir dist\OpenShape] [-TestDesktopDir <folder>] [-Screenshot <png>] [-TestRunningApp]
# -TestDesktopDir: the installer was built with OPENSHAPE_TEST_DESKTOP_DIR set
#   to this folder (scripts/windows/make-installer.sh): also tests /DESKTOP.
# -Screenshot / -TestRunningApp start the installed app (a window; they take
#   OpenShape's automation lock like every automated run).
# Exit code: the number of failed checks.
param(
    [Parameter(Mandatory = $true)] [string] $Setup,
    [Parameter(Mandatory = $true)] [string] $InstallDir,
    [string] $PackageDir,
    [string] $TestDesktopDir,
    [string] $Screenshot,
    [switch] $TestRunningApp
)
$ErrorActionPreference = 'Stop'
$failures = 0
function Check([bool] $ok, [string] $what) {
    if ($ok) { Write-Host "[PASS] $what" } else { Write-Host "[FAIL] $what"; $script:failures++ }
}
function RegValue([string] $key, [string] $name) {
    $item = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
    if ($null -eq $item) { return $null }
    return $item.$name
}
function LinkTarget([string] $link) {
    return (New-Object -ComObject WScript.Shell).CreateShortcut($link).TargetPath
}
function Snapshot([string] $path) { # something the installer must not touch: exists? (a file: unchanged?)
    if (-not (Test-Path -LiteralPath $path)) { return 'absent' }
    $item = Get-Item -LiteralPath $path -Force
    if ($item -is [IO.FileInfo]) { return "present $($item.Length) $($item.LastWriteTimeUtc.Ticks)" }
    return 'present'
}

$Setup = (Resolve-Path $Setup).Path
New-Item -ItemType Directory -Force (Split-Path $InstallDir) | Out-Null
$InstallDir = [IO.Path]::GetFullPath($InstallDir)
$uninstKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenShape'
$classes = 'HKCU:\Software\Classes'
$startLink = Join-Path ([Environment]::GetFolderPath('Programs')) 'OpenShape.lnk'
$userDesktopLink = Join-Path ([Environment]::GetFolderPath('Desktop')) 'OpenShape.lnk'

foreach ($existing in @($uninstKey, "$classes\.openshape", "$classes\OpenShape.Project", $startLink, $InstallDir)) {
    if (Test-Path -LiteralPath $existing) { throw "$existing exists already: this test only runs where OpenShape is not installed" }
}
# The user's own things, which must survive install and uninstall unchanged.
$untouched = @{
    'desktop shortcut of the user' = $userDesktopLink
    'app settings (HKCU\Software\OpenShape)' = 'HKCU:\Software\OpenShape'
    'app data folder' = (Join-Path $env:LOCALAPPDATA 'OpenShape')
}
$before = @{}
foreach ($k in $untouched.Keys) { $before[$k] = Snapshot $untouched[$k] }

# ---- Install ---------------------------------------------------------------
$arguments = @('/S')
if ($TestDesktopDir) { New-Item -ItemType Directory -Force $TestDesktopDir | Out-Null; $arguments += '/DESKTOP' }
$arguments += "/D=$InstallDir" # last and unquoted, as NSIS requires
$timer = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $Setup -ArgumentList $arguments -Wait -PassThru
Check ($p.ExitCode -eq 0) "silent install exits with 0 (got $($p.ExitCode), $([int]$timer.Elapsed.TotalSeconds) s)"

$exe = Join-Path $InstallDir 'OpenShape.exe'
Check (Test-Path $exe) 'OpenShape.exe installed'
Check (Test-Path (Join-Path $InstallDir 'Uninstall.exe')) 'Uninstall.exe installed'
$installedFiles = @(Get-ChildItem -LiteralPath $InstallDir -Recurse -File).Count
if ($PackageDir) {
    $packaged = @(Get-ChildItem -LiteralPath $PackageDir -Recurse -File).Count
    Check ($installedFiles -eq $packaged + 1) "installed files = packaged files + uninstaller ($installedFiles = $packaged + 1)"
}
$version = (Get-Item $exe).VersionInfo.ProductVersion

Check ((RegValue $uninstKey 'DisplayName') -eq 'OpenShape') 'Apps & features: DisplayName'
Check ((RegValue $uninstKey 'DisplayVersion') -eq $version) "Apps & features: DisplayVersion $version"
Check ((RegValue $uninstKey 'DisplayIcon') -eq "$exe,0") 'Apps & features: DisplayIcon'
Check ((RegValue $uninstKey 'Publisher') -eq 'OpenShape contributors') 'Apps & features: Publisher'
Check ((RegValue $uninstKey 'URLInfoAbout') -eq 'https://github.com/SamuelAirs/openshape') 'Apps & features: URLInfoAbout'
Check ((RegValue $uninstKey 'InstallLocation') -eq $InstallDir) 'Apps & features: InstallLocation'
Check ((RegValue $uninstKey 'UninstallString') -eq "`"$InstallDir\Uninstall.exe`"") 'Apps & features: UninstallString'
Check ((RegValue $uninstKey 'EstimatedSize') -gt 1000) "Apps & features: EstimatedSize $(RegValue $uninstKey 'EstimatedSize') KiB"
Check ((RegValue $uninstKey 'NoModify') -eq 1 -and (RegValue $uninstKey 'NoRepair') -eq 1) 'Apps & features: NoModify, NoRepair'

Check ((RegValue "$classes\.openshape" '(default)') -eq 'OpenShape.Project') '.openshape -> OpenShape.Project'
Check ($null -ne (RegValue "$classes\.openshape\OpenWithProgids" 'OpenShape.Project')) '.openshape OpenWithProgids'
Check ((RegValue "$classes\OpenShape.Project\DefaultIcon" '(default)') -eq "$exe,0") 'OpenShape.Project icon'
Check ((RegValue "$classes\OpenShape.Project\shell\open\command" '(default)') -eq "`"$exe`" `"%1`"") 'OpenShape.Project open command'

Check (Test-Path $startLink) "Start-menu shortcut $startLink"
if (Test-Path $startLink) { Check ((LinkTarget $startLink) -eq $exe) 'Start-menu shortcut target' }
if ($TestDesktopDir) {
    $testLink = Join-Path $TestDesktopDir 'OpenShape.lnk'
    Check (Test-Path $testLink) '/DESKTOP: desktop shortcut created'
    if (Test-Path $testLink) { Check ((LinkTarget $testLink) -eq $exe) 'desktop shortcut target' }
    Check ((RegValue $uninstKey 'DesktopShortcut') -eq $testLink) 'desktop shortcut recorded for the uninstaller'
} else {
    Check ($null -eq (RegValue $uninstKey 'DesktopShortcut')) 'no desktop shortcut without /DESKTOP'
}

# ---- The installed app starts with nothing but Windows on PATH ---------------
if ($Screenshot) {
    Remove-Item -LiteralPath $Screenshot -ErrorAction SilentlyContinue
    $savedPath = $env:PATH
    $env:PATH = 'C:\Windows\System32'
    try {
        $app = Start-Process -FilePath $exe -ArgumentList @('--demo', 'bracket', '--screenshot', "`"$Screenshot`"") -Wait -PassThru
    } finally { $env:PATH = $savedPath }
    Check ($app.ExitCode -eq 0) "installed app: --demo bracket --screenshot exits with 0 (got $($app.ExitCode))"
    Check ((Test-Path $Screenshot) -and (Get-Item $Screenshot).Length -gt 10000) "installed app wrote $Screenshot"
}

# ---- Upgrade over the installation --------------------------------------------
if ($TestRunningApp) {
    $running = Start-Process -FilePath $exe -ArgumentList @('--demo', 'empty') -PassThru
    Start-Sleep -Seconds 3
    $p = Start-Process -FilePath $Setup -ArgumentList @('/S', "/D=$InstallDir") -Wait -PassThru
    Check ($p.ExitCode -ne 0) "install over a running OpenShape gives up (exit code $($p.ExitCode))"
    Check (-not $running.HasExited) 'the running OpenShape was left alone'
    Stop-Process -Id $running.Id -Force -ErrorAction SilentlyContinue
    $running.WaitForExit()
    Start-Sleep -Seconds 1
}
$marker = Join-Path $InstallDir 'user-file.txt' # something of the user's in the program folder
Set-Content -LiteralPath $marker -Value 'not the installer''s'
$p = Start-Process -FilePath $Setup -ArgumentList @('/S', "/D=$InstallDir") -Wait -PassThru
Check ($p.ExitCode -eq 0) "upgrade (install over the installation) exits with 0 (got $($p.ExitCode))"
Check (@(Get-ChildItem -LiteralPath $InstallDir -Recurse -File).Count -eq $installedFiles + 1) 'upgrade: same files again'
Check ((RegValue $uninstKey 'InstallLocation') -eq $InstallDir) 'upgrade: still registered'
Check ((Test-Path $startLink) -and (LinkTarget $startLink) -eq $exe) 'upgrade: Start-menu shortcut kept'
if ($TestDesktopDir) { Check (Test-Path (Join-Path $TestDesktopDir 'OpenShape.lnk')) 'upgrade: desktop shortcut kept' }

# ---- Uninstall (as Apps & features does: the uninstaller copies itself away) ----
$quiet = RegValue $uninstKey 'QuietUninstallString'
Check ($quiet -eq "`"$InstallDir\Uninstall.exe`" /S") 'QuietUninstallString'
$p = Start-Process -FilePath (Join-Path $InstallDir 'Uninstall.exe') -ArgumentList '/S' -Wait -PassThru
Check ($p.ExitCode -eq 0) "silent uninstall starts (exit code $($p.ExitCode))"
$deadline = (Get-Date).AddSeconds(90)
while ((Test-Path -LiteralPath $exe) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
Start-Sleep -Seconds 2 # the uninstaller's last steps (registry) after the files

$left = @(Get-ChildItem -LiteralPath $InstallDir -Recurse -Force -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
Check ($left.Count -eq 1 -and $left[0] -eq $marker) "program files removed; only the user's file is left ($($left.Count) items)"
Remove-Item -LiteralPath $marker -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $InstallDir -ErrorAction SilentlyContinue
Check (-not (Test-Path $uninstKey)) 'Apps & features entry removed'
Check (-not (Test-Path "$classes\.openshape")) '.openshape association removed'
Check (-not (Test-Path "$classes\OpenShape.Project")) 'OpenShape.Project removed'
Check (-not (Test-Path $startLink)) 'Start-menu shortcut removed'
if ($TestDesktopDir) { Check (-not (Test-Path (Join-Path $TestDesktopDir 'OpenShape.lnk'))) 'desktop shortcut removed' }
foreach ($k in $untouched.Keys) {
    Check ((Snapshot $untouched[$k]) -eq $before[$k]) "untouched: $k ($($before[$k].Split(' ')[0]))"
}

Write-Host "$failures failed checks"
exit $failures
