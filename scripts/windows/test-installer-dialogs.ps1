# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Clicks through the installer's and uninstaller's dialogs (UI Automation:
# buttons are invoked and checkboxes toggled without moving the mouse) and
# checks what each choice does:
#   1. install: welcome, MPL license page, folder page, finish page (desktop
#      shortcut off by default, "Run OpenShape" on); tick the shortcut and
#      keep "Run": the shortcut is made and OpenShape starts;
#   2. install again over it: the folder page offers the installed folder,
#      the finish page shows the shortcut ticked; untick it: it is removed;
#   3. install again while OpenShape runs: "OpenShape is running" asks to
#      close it; close it and click Retry: the install completes;
#   4. uninstall from its dialog (as Apps & features starts it): files,
#      shortcuts and registry entries are gone.
# Needs an installer built with OPENSHAPE_TEST_DESKTOP_DIR=<-TestDesktopDir>
# (its desktop shortcut goes there, never onto the real desktop; the script
# refuses any other setup), a desktop session, and
# no OpenShape installed for this user. While it runs it holds OpenShape's
# automation lock, so automated OpenShape runs (acceptance, screenshots)
# wait instead of clicking into the installer's windows.
#
# Usage (Windows PowerShell 5.1):
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\windows\test-installer-dialogs.ps1 `
#       -Setup build\installer-test\OpenShape-0.1.0-windows-x64-setup.exe `
#       -InstallDir build\installer-test\Programs\OpenShape -TestDesktopDir build\installer-test\desktop
# Exit code: the number of failed checks.
param(
    [Parameter(Mandatory = $true)] [string] $Setup,
    [Parameter(Mandatory = $true)] [string] $InstallDir,
    [Parameter(Mandatory = $true)] [string] $TestDesktopDir
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName UIAutomationClientsideProviders
# .NET's proxies for Win32 controls: without them this machine's UI
# Automation reports NSIS's buttons, checkboxes and fields as plain panes.
# (Registering before the first UI Automation call fails: touch it first.)
$null = [System.Windows.Automation.AutomationElement]::RootElement.Current.Name
[System.Windows.Automation.ClientSettings]::RegisterClientSideProviderAssembly(
    [UIAutomationClientsideProviders.UIAutomationClientSideProviders].Assembly.GetName())

$failures = 0
function Check([bool] $ok, [string] $what) {
    if ($ok) { Write-Host "[PASS] $what" } else { Write-Host "[FAIL] $what"; $script:failures++ }
}
function RegValue([string] $key, [string] $name) {
    $item = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
    if ($null -eq $item) { return $null }
    return $item.$name
}
function Snapshot([string] $path) {
    if (-not (Test-Path -LiteralPath $path)) { return 'absent' }
    $item = Get-Item -LiteralPath $path -Force
    if ($item -is [IO.FileInfo]) { return "present $($item.Length) $($item.LastWriteTimeUtc.Ticks)" }
    return 'present'
}

# ---- UI Automation helpers ----------------------------------------------------
$UIA = [System.Windows.Automation.AutomationElement]
$Scope = [System.Windows.Automation.TreeScope]
$Types = [System.Windows.Automation.ControlType]
function Cond($property, $value) { New-Object System.Windows.Automation.PropertyCondition($property, $value) }
function AndCond($a, $b) { New-Object System.Windows.Automation.AndCondition($a, $b) }

# A top-level window with this title (of this process, if given). NSIS's
# uninstaller caption ends with a space: titles are compared trimmed.
function FindWindow([string] $title, [int] $processId = 0, [int] $seconds = 30) {
    $condition = Cond $UIA::ControlTypeProperty $Types::Window
    if ($processId -ne 0) { $condition = AndCond $condition (Cond $UIA::ProcessIdProperty $processId) }
    $deadline = (Get-Date).AddSeconds($seconds)
    do {
        foreach ($window in $UIA::RootElement.FindAll($Scope::Children, $condition)) {
            try { if ($window.Current.Name.Trim() -eq $title) { return $window } } catch {}
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    return $null
}

# A control of the given type and name inside a window, once it exists (and
# is enabled, for buttons). Pages are replaced as the wizard advances, so
# this also waits for the next page.
function FindControl($window, $type, [string] $name, [int] $seconds = 30) {
    $condition = AndCond (Cond $UIA::NameProperty $name) (Cond $UIA::ControlTypeProperty $type)
    $deadline = (Get-Date).AddSeconds($seconds)
    do {
        try {
            $control = $window.FindFirst($Scope::Descendants, $condition)
            if ($null -ne $control -and $control.Current.IsEnabled -and -not $control.Current.IsOffscreen) { return $control }
        } catch [System.Windows.Automation.ElementNotAvailableException] { return $null }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    return $null
}

function Names($window) { # for failure messages: what the window shows
    try {
        $all = $window.FindAll($Scope::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
        return (@($all | Where-Object { $_.Current.Name } | ForEach-Object { "$($_.Current.ControlType.ProgrammaticName.Split('.')[-1]):'$($_.Current.Name)'" }) -join ', ')
    } catch { return '(window gone)' }
}

function Click($window, [string] $name, [string] $what, [int] $seconds = 30) {
    $button = FindControl $window $Types::Button $name $seconds
    Check ($null -ne $button) "$what (button '$name')"
    if ($null -eq $button) { Write-Host "  the window shows: $(Names $window)"; throw "button '$name' not found" }
    $button.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
}

function CheckBox($window, [string] $name) {
    $box = FindControl $window $Types::CheckBox $name
    if ($null -eq $box) { Write-Host "  the window shows: $(Names $window)"; throw "checkbox '$name' not found" }
    return $box.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern)
}

function SetCheckBox($toggle, [bool] $on) {
    $want = if ($on) { 'On' } else { 'Off' }
    if ($toggle.Current.ToggleState.ToString() -ne $want) { $toggle.Toggle() }
    return $toggle.Current.ToggleState.ToString() -eq $want
}

# The folder page (the one whose Next button says "Install"): its folder field.
function FolderPageValue($window) {
    if ($null -eq (FindControl $window $Types::Button 'Install')) { return '(no folder page)' }
    $folder = $window.FindFirst($Scope::Descendants, (Cond $UIA::ControlTypeProperty $Types::Edit))
    if ($null -eq $folder) { return '(no folder field)' }
    return $folder.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
}

function WaitForExit($process, [int] $seconds = 60) {
    if (-not $process.WaitForExit($seconds * 1000)) { return $false }
    return $true
}

# Processes started from the installed folder (the app the finish page starts).
function AppProcesses([string] $exe) {
    return @(Get-Process -Name OpenShape -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe })
}

# ---- Setup ----------------------------------------------------------------------
$Setup = (Resolve-Path $Setup).Path
New-Item -ItemType Directory -Force (Split-Path $InstallDir) | Out-Null
$InstallDir = [IO.Path]::GetFullPath($InstallDir)
New-Item -ItemType Directory -Force $TestDesktopDir | Out-Null
$TestDesktopDir = (Resolve-Path $TestDesktopDir).Path
$exe = Join-Path $InstallDir 'OpenShape.exe'
$testLink = Join-Path $TestDesktopDir 'OpenShape.lnk'
$uninstKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenShape'
$classes = 'HKCU:\Software\Classes'
$startLink = Join-Path ([Environment]::GetFolderPath('Programs')) 'OpenShape.lnk'
$userDesktopLink = Join-Path ([Environment]::GetFolderPath('Desktop')) 'OpenShape.lnk'
$version = (Get-Item $Setup).VersionInfo.ProductVersion
$setupTitle = "OpenShape $version Setup"
$uninstallTitle = "OpenShape $version Uninstall"

# Only a test build of the installer, whose desktop shortcut goes to
# $TestDesktopDir: a release build would replace the user's own
# desktop\OpenShape.lnk when the box is ticked and delete it when unticked.
# make-installer.sh records the folder in the setup's Comments field.
$comments = (Get-Item $Setup).VersionInfo.Comments
if ($comments -ne "Test build: desktop shortcut in $TestDesktopDir") {
    throw "$Setup is not a test build for $TestDesktopDir (Comments: '$comments'): build it with OPENSHAPE_TEST_DESKTOP_DIR=<that folder> scripts/windows/make-installer.sh"
}

foreach ($existing in @($uninstKey, "$classes\.openshape", "$classes\OpenShape.Project", $startLink, $InstallDir, $testLink)) {
    if (Test-Path -LiteralPath $existing) { throw "$existing exists already: this test only runs where OpenShape is not installed" }
}
$userLinkBefore = Snapshot $userDesktopLink

# OpenShape's automation lock (QLockFile in main.cpp): held open without
# delete sharing, so waiting OpenShape runs neither start nor remove it.
$lockPath = Join-Path ([IO.Path]::GetTempPath()) 'openshape-automation.lock'
$lock = $null
$lockDeadline = (Get-Date).AddMinutes(15)
$waiting = $false
while ($null -eq $lock) {
    Remove-Item -LiteralPath $lockPath -ErrorAction SilentlyContinue # a crashed run's lock (a live one cannot be deleted)
    try {
        $lock = [IO.File]::Open($lockPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
    } catch {
        if ((Get-Date) -gt $lockDeadline) { throw "another automated OpenShape run holds $lockPath" }
        if (-not $waiting) { Write-Host 'waiting for another automated OpenShape run to finish'; $waiting = $true }
        Start-Sleep -Seconds 2
    }
}
$bytes = [Text.Encoding]::UTF8.GetBytes("$PID`npowershell`n$env:COMPUTERNAME`n")
$lock.Write($bytes, 0, $bytes.Length)
$lock.Flush()

$started = New-Object System.Collections.ArrayList
$uninstallPid = 0
try {
    # ---- 1. Install from the dialogs --------------------------------------------
    $p = Start-Process -FilePath $Setup -ArgumentList @("/D=$InstallDir") -PassThru
    [void]$started.Add($p)
    $w = FindWindow $setupTitle $p.Id
    Check ($null -ne $w) "install: the setup window '$setupTitle' opens"
    if ($null -eq $w) { throw 'no setup window' }
    Click $w 'Next >' 'install: welcome page'
    $top = FindControl $w $Types::Text 'OpenShape is free software under the Mozilla Public License 2.0.'
    Check ($null -ne $top) 'install: the license page names the MPL-2.0'
    $licenseText = ''
    $doc = $w.FindFirst($Scope::Descendants, (Cond $UIA::ControlTypeProperty $Types::Document))
    if ($null -eq $doc) { $doc = $w.FindFirst($Scope::Descendants, (Cond $UIA::ControlTypeProperty $Types::Edit)) }
    if ($null -ne $doc) {
        $pattern = $null
        if ($doc.TryGetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern, [ref] $pattern)) {
            $licenseText = $pattern.DocumentRange.GetText(400)
        } elseif ($doc.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref] $pattern)) {
            $licenseText = $pattern.Current.Value
        }
    }
    Check ($licenseText -match 'Mozilla Public License Version 2\.0') "install: the license page shows the license text ('$($licenseText.Split("`n")[0].Trim())')"
    Click $w 'I Agree' 'install: accept the license'
    $folderValue = FolderPageValue $w
    Check ($folderValue.TrimEnd(' ', '\') -eq $InstallDir) "install: the folder page shows the chosen folder ($folderValue)"
    Click $w 'Install' 'install: start installing'
    $desktop = CheckBox $w 'Create a desktop shortcut'
    Check ($desktop.Current.ToggleState.ToString() -eq 'Off') 'install: finish page, desktop shortcut off by default'
    $run = CheckBox $w "Run OpenShape $version"
    Check ($run.Current.ToggleState.ToString() -eq 'On') "install: finish page, 'Run OpenShape' on"
    Check (SetCheckBox $desktop $true) 'install: tick the desktop shortcut'
    Click $w 'Finish' 'install: finish'
    Check (WaitForExit $p) 'install: the setup closes'
    Check ($p.ExitCode -eq 0) "install: exit code 0 (got $($p.ExitCode))"
    Check ((RegValue $uninstKey 'InstallLocation') -eq $InstallDir) 'install: registered in Apps & features'
    Check ((Test-Path $startLink)) 'install: Start-menu shortcut'
    Check (Test-Path $testLink) 'install: desktop shortcut created'
    Check ((RegValue $uninstKey 'DesktopShortcut') -eq $testLink) 'install: desktop shortcut recorded for the uninstaller'
    $deadline = (Get-Date).AddSeconds(30)
    while ((AppProcesses $exe).Count -eq 0 -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
    $apps = AppProcesses $exe
    Check ($apps.Count -eq 1) "install: 'Run OpenShape' started the installed app ($($apps.Count) running)"
    foreach ($app in $apps) { Stop-Process -Id $app.Id -Force; $app.WaitForExit() }
    Start-Sleep -Seconds 1

    # ---- 2. Install again over it; untick the desktop shortcut -------------------
    $p = Start-Process -FilePath $Setup -PassThru # no /D: the installed folder (registry)
    [void]$started.Add($p)
    $w = FindWindow $setupTitle $p.Id
    Check ($null -ne $w) 'upgrade: the setup window opens'
    if ($null -eq $w) { throw 'no setup window' }
    Click $w 'Next >' 'upgrade: welcome page'
    Click $w 'I Agree' 'upgrade: accept the license'
    $folderValue = FolderPageValue $w
    Check ($folderValue.TrimEnd(' ', '\') -eq $InstallDir) "upgrade: the folder page offers the installed folder ($folderValue)"
    Click $w 'Install' 'upgrade: start installing'
    $desktop = CheckBox $w 'Create a desktop shortcut'
    Check ($desktop.Current.ToggleState.ToString() -eq 'On') 'upgrade: finish page shows the existing desktop shortcut ticked'
    Check (SetCheckBox (CheckBox $w "Run OpenShape $version") $false) "upgrade: untick 'Run OpenShape'"
    Check (SetCheckBox $desktop $false) 'upgrade: untick the desktop shortcut'
    Click $w 'Finish' 'upgrade: finish'
    Check (WaitForExit $p) 'upgrade: the setup closes'
    Check ($p.ExitCode -eq 0) "upgrade: exit code 0 (got $($p.ExitCode))"
    Check (-not (Test-Path $testLink)) 'upgrade: unticking removed the desktop shortcut'
    Check ($null -eq (RegValue $uninstKey 'DesktopShortcut')) 'upgrade: no desktop shortcut recorded'
    Check ((Test-Path $exe) -and (RegValue $uninstKey 'InstallLocation') -eq $InstallDir) 'upgrade: still installed and registered'
    Check ((Test-Path $startLink)) 'upgrade: Start-menu shortcut kept'
    Check ((AppProcesses $exe).Count -eq 0) "upgrade: unticked 'Run OpenShape' started nothing"

    # ---- 3. Install while OpenShape runs: close it, Retry ------------------------
    $app = Start-Process -FilePath $exe -PassThru
    [void]$started.Add($app)
    Start-Sleep -Seconds 3
    $p = Start-Process -FilePath $Setup -PassThru
    [void]$started.Add($p)
    $w = FindWindow $setupTitle $p.Id
    Check ($null -ne $w) 'running app: the setup window opens'
    if ($null -eq $w) { throw 'no setup window' }
    Click $w 'Next >' 'running app: welcome page'
    Click $w 'I Agree' 'running app: accept the license'
    Click $w 'Install' 'running app: start installing'
    # The message box is a window of the setup process with a Retry button.
    $retry = $null
    $deadline = (Get-Date).AddSeconds(30)
    $retryCondition = AndCond (Cond $UIA::NameProperty 'Retry') (Cond $UIA::ControlTypeProperty $Types::Button)
    while ($null -eq $retry -and (Get-Date) -lt $deadline) {
        foreach ($top in $UIA::RootElement.FindAll($Scope::Children, (Cond $UIA::ProcessIdProperty $p.Id))) {
            $found = $top.FindFirst($Scope::Descendants, $retryCondition)
            if ($null -ne $found) { $retry = $found; $box = $top; break }
        }
        if ($null -eq $retry) { Start-Sleep -Milliseconds 250 }
    }
    Check ($null -ne $retry) 'running app: the setup asks to close OpenShape (Retry/Cancel)'
    if ($null -eq $retry) { throw 'no message box' }
    Check ((Names $box) -match 'OpenShape is running from') "running app: the message says OpenShape is running ($((Names $box).Substring(0, [Math]::Min(120, (Names $box).Length))))"
    Check (-not $app.HasExited) 'running app: OpenShape was left running'
    Stop-Process -Id $app.Id -Force
    $app.WaitForExit()
    Start-Sleep -Seconds 1
    $retry.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    Check (SetCheckBox (CheckBox $w "Run OpenShape $version" 120) $false) "running app: after Retry the install completes (finish page); untick 'Run OpenShape'"
    Check ((CheckBox $w 'Create a desktop shortcut').Current.ToggleState.ToString() -eq 'Off') 'running app: desktop shortcut still off'
    Click $w 'Finish' 'running app: finish'
    Check (WaitForExit $p) 'running app: the setup closes'
    Check ($p.ExitCode -eq 0) "running app: exit code 0 (got $($p.ExitCode))"
    Check ((Test-Path $exe) -and (RegValue $uninstKey 'InstallLocation') -eq $InstallDir) 'running app: installed and registered'

    # ---- 4. Uninstall from its dialog ----------------------------------------------
    # Apps & features runs UninstallString; the uninstaller copies itself to
    # %TEMP% and runs from there, so its window belongs to another process.
    $u = Start-Process -FilePath (Join-Path $InstallDir 'Uninstall.exe') -PassThru
    $w = FindWindow $uninstallTitle 0 30
    Check ($null -ne $w) "uninstall: the window '$uninstallTitle' opens"
    if ($null -eq $w) { throw 'no uninstall window' }
    $uninstallPid = $w.Current.ProcessId
    Click $w 'Uninstall' 'uninstall: confirm'
    Click $w 'Close' 'uninstall: done' 120
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Process -Id $uninstallPid -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
    [void]$u.WaitForExit(5000)
    Check (-not (Test-Path -LiteralPath $InstallDir)) 'uninstall: program folder removed'
    Check (-not (Test-Path $uninstKey)) 'uninstall: Apps & features entry removed'
    Check (-not (Test-Path "$classes\.openshape")) 'uninstall: .openshape association removed'
    Check (-not (Test-Path "$classes\OpenShape.Project")) 'uninstall: OpenShape.Project removed'
    Check (-not (Test-Path $startLink)) 'uninstall: Start-menu shortcut removed'
    Check (-not (Test-Path $testLink)) 'uninstall: no desktop shortcut'
    Check ((Snapshot $userDesktopLink) -eq $userLinkBefore) "untouched: the user's own desktop shortcut ($($userLinkBefore.Split(' ')[0]))"
} catch {
    Write-Host "[FAIL] $($_.Exception.Message)"
    $failures++
} finally {
    # Leave nothing behind, whatever happened above.
    foreach ($process in $started) { if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue } }
    if ($uninstallPid) { Stop-Process -Id $uninstallPid -Force -ErrorAction SilentlyContinue } # the uninstaller's copy in %TEMP%
    foreach ($app in (AppProcesses $exe)) { Stop-Process -Id $app.Id -Force -ErrorAction SilentlyContinue }
    if ((RegValue $uninstKey 'InstallLocation') -eq $InstallDir -and (Test-Path (Join-Path $InstallDir 'Uninstall.exe'))) {
        Write-Host 'cleanup: uninstalling silently'
        Start-Process -FilePath (Join-Path $InstallDir 'Uninstall.exe') -ArgumentList @('/S', "_?=$InstallDir") -Wait | Out-Null
        Remove-Item -LiteralPath (Join-Path $InstallDir 'Uninstall.exe') -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $InstallDir -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $testLink -ErrorAction SilentlyContinue
    $lock.Close()
    Remove-Item -LiteralPath $lockPath -ErrorAction SilentlyContinue
}

Write-Host "$failures failed checks"
exit $failures
