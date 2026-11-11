#Requires -Version 5.1
<#
.SYNOPSIS
    Windows installer and uninstaller for ClassicCars Studio.
.DESCRIPTION
    Installs ClassicCars Studio to a user-selected directory on Windows
    (defaults to %LOCALAPPDATA%\Programs\ClassicCars Studio), preserves
    existing user state in state\, and creates Start Menu and Desktop
    .lnk shortcuts via WScript.Shell.
.EXAMPLE
    .\installers\install-windows.ps1
.EXAMPLE
    .\installers\install-windows.ps1 -InstallDir "D:\Apps\ClassicCars Studio" -Yes
.EXAMPLE
    .\installers\install-windows.ps1 -Uninstall -InstallDir "D:\Apps\ClassicCars Studio"
#>

[CmdletBinding()]
param(
    [Alias("dir", "prefix", "d")]
    [string]$InstallDir = "",

    [string]$SourceDir = "",

    [Alias("y", "quiet", "q")]
    [switch]$Yes,

    [switch]$Gui,
    [switch]$Cli,
    [switch]$NoMenu,
    [switch]$NoDesktop,
    [switch]$NoPath,
    [switch]$AddToPath,
    [switch]$Uninstall,
    [switch]$PurgeState
)

$ErrorActionPreference = "Stop"
$AppName = "ClassicCars Studio"
$ManifestName = ".install-manifest.json"

function Resolve-SourceRoot {
    param([string]$Explicit)
    if ($Explicit) {
        return (Resolve-Path -LiteralPath $Explicit).Path
    }
    $here = $PSScriptRoot
    $parent = Split-Path -Parent $here
    if ((Test-Path (Join-Path $parent "web")) -or (Test-Path (Join-Path $parent "VERSION"))) {
        return $parent
    }
    if ((Test-Path (Join-Path $here "web")) -or (Test-Path (Join-Path $here "VERSION"))) {
        return $here
    }
    throw "Could not locate ClassicCars Studio payload directory near $here"
}

function Get-DefaultInstallDir {
    $base = $env:LOCALAPPDATA
    if (-not $base) {
        $base = Join-Path $HOME "AppData\Local"
    }
    return (Join-Path $base "Programs\ClassicCars Studio")
}

function Get-AppVersion {
    param([string]$Root)
    $verFile = Join-Path $Root "VERSION"
    if (Test-Path -LiteralPath $verFile) {
        $v = (Get-Content -LiteralPath $verFile -Raw).Trim()
        if ($v) { return $v }
    }
    return "0.0.0"
}

function Show-InstallerDialog {
    param(
        [string]$Version,
        [string]$DefaultDir,
        [bool]$DefaultMenu,
        [bool]$DefaultDesktop,
        [bool]$DefaultPath
    )
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [System.Windows.Forms.Application]::EnableVisualStyles()

    $form = New-Object System.Windows.Forms.Form
    $form.Text = "Install $AppName v$Version"
    $form.Size = New-Object System.Drawing.Size(560, 330)
    $form.StartPosition = "CenterScreen"
    $form.FormBorderStyle = "FixedDialog"
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false
    $form.BackColor = [System.Drawing.Color]::FromArgb(24, 26, 31)
    $form.ForeColor = [System.Drawing.Color]::FromArgb(230, 237, 243)

    $titleLabel = New-Object System.Windows.Forms.Label
    $titleLabel.Text = "$AppName v$Version"
    $titleLabel.Font = New-Object System.Drawing.Font("Segoe UI", 14, [System.Drawing.FontStyle]::Bold)
    $titleLabel.Location = New-Object System.Drawing.Point(24, 18)
    $titleLabel.Size = New-Object System.Drawing.Size(500, 28)
    $form.Controls.Add($titleLabel)

    $subLabel = New-Object System.Windows.Forms.Label
    $subLabel.Text = "Choose the destination folder where ClassicCars Studio will be installed."
    $subLabel.Font = New-Object System.Drawing.Font("Segoe UI", 9)
    $subLabel.ForeColor = [System.Drawing.Color]::FromArgb(157, 165, 180)
    $subLabel.Location = New-Object System.Drawing.Point(24, 48)
    $subLabel.Size = New-Object System.Drawing.Size(500, 20)
    $form.Controls.Add($subLabel)

    $dirLabel = New-Object System.Windows.Forms.Label
    $dirLabel.Text = "Installation Folder:"
    $dirLabel.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
    $dirLabel.Location = New-Object System.Drawing.Point(24, 80)
    $dirLabel.Size = New-Object System.Drawing.Size(200, 20)
    $form.Controls.Add($dirLabel)

    $dirBox = New-Object System.Windows.Forms.TextBox
    $dirBox.Text = $DefaultDir
    $dirBox.Font = New-Object System.Drawing.Font("Segoe UI", 9)
    $dirBox.Location = New-Object System.Drawing.Point(24, 104)
    $dirBox.Size = New-Object System.Drawing.Size(400, 26)
    $dirBox.BackColor = [System.Drawing.Color]::FromArgb(33, 37, 43)
    $dirBox.ForeColor = [System.Drawing.Color]::FromArgb(230, 237, 243)
    $form.Controls.Add($dirBox)

    $browseBtn = New-Object System.Windows.Forms.Button
    $browseBtn.Text = "Browse..."
    $browseBtn.Location = New-Object System.Drawing.Point(434, 102)
    $browseBtn.Size = New-Object System.Drawing.Size(90, 27)
    $browseBtn.FlatStyle = "Flat"
    $browseBtn.BackColor = [System.Drawing.Color]::FromArgb(44, 49, 58)
    $browseBtn.Add_Click({
        $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
        $dialog.Description = "Select the destination folder for $AppName"
        $dialog.ShowNewFolderButton = $true
        if ($dirBox.Text -and (Test-Path -LiteralPath (Split-Path -Parent $dirBox.Text))) {
            $dialog.SelectedPath = (Split-Path -Parent $dirBox.Text)
        }
        if ($dialog.ShowDialog($form) -eq [System.Windows.Forms.DialogResult]::OK) {
            $picked = $dialog.SelectedPath
            $leaf = Split-Path -Leaf $picked
            if ($leaf -notmatch "^(?i)classiccars[- ]studio$") {
                $picked = Join-Path $picked "ClassicCars Studio"
            }
            $dirBox.Text = $picked
        }
    })
    $form.Controls.Add($browseBtn)

    $chkMenu = New-Object System.Windows.Forms.CheckBox
    $chkMenu.Text = "Create Start Menu shortcut"
    $chkMenu.Checked = $DefaultMenu
    $chkMenu.Location = New-Object System.Drawing.Point(24, 146)
    $chkMenu.Size = New-Object System.Drawing.Size(480, 22)
    $form.Controls.Add($chkMenu)

    $chkDesktop = New-Object System.Windows.Forms.CheckBox
    $chkDesktop.Text = "Create Desktop shortcut"
    $chkDesktop.Checked = $DefaultDesktop
    $chkDesktop.Location = New-Object System.Drawing.Point(24, 174)
    $chkDesktop.Size = New-Object System.Drawing.Size(480, 22)
    $form.Controls.Add($chkDesktop)

    $chkPath = New-Object System.Windows.Forms.CheckBox
    $chkPath.Text = "Create CLI launcher (classiccars-studio.cmd) and add to User PATH"
    $chkPath.Checked = $DefaultPath
    $chkPath.Location = New-Object System.Drawing.Point(24, 202)
    $chkPath.Size = New-Object System.Drawing.Size(480, 22)
    $form.Controls.Add($chkPath)

    $installBtn = New-Object System.Windows.Forms.Button
    $installBtn.Text = "Install"
    $installBtn.Location = New-Object System.Drawing.Point(424, 244)
    $installBtn.Size = New-Object System.Drawing.Size(100, 32)
    $installBtn.FlatStyle = "Flat"
    $installBtn.BackColor = [System.Drawing.Color]::FromArgb(59, 130, 246)
    $installBtn.ForeColor = [System.Drawing.Color]::White
    $installBtn.DialogResult = [System.Windows.Forms.DialogResult]::OK
    $form.Controls.Add($installBtn)

    $cancelBtn = New-Object System.Windows.Forms.Button
    $cancelBtn.Text = "Cancel"
    $cancelBtn.Location = New-Object System.Drawing.Point(324, 244)
    $cancelBtn.Size = New-Object System.Drawing.Size(90, 32)
    $cancelBtn.FlatStyle = "Flat"
    $cancelBtn.BackColor = [System.Drawing.Color]::FromArgb(44, 49, 58)
    $cancelBtn.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
    $form.Controls.Add($cancelBtn)

    $form.AcceptButton = $installBtn
    $form.CancelButton = $cancelBtn

    $dialogResult = $form.ShowDialog()
    if ($dialogResult -ne [System.Windows.Forms.DialogResult]::OK) {
        Write-Host "Installation cancelled by user."
        exit 0
    }

    return @{
        InstallDir = $dirBox.Text.Trim()
        CreateMenu = [bool]$chkMenu.Checked
        CreateDesktop = [bool]$chkDesktop.Checked
        CreatePath = [bool]$chkPath.Checked
    }
}

function New-ShortcutFile {
    param(
        [string]$ShortcutPath,
        [string]$TargetPath,
        [string]$WorkingDirectory,
        [string]$IconLocation
    )
    $parentDir = Split-Path -Parent $ShortcutPath
    if (-not (Test-Path -LiteralPath $parentDir)) {
        New-Item -ItemType Directory -Path $parentDir -Force | Out-Null
    }
    $ws = New-Object -ComObject WScript.Shell
    $sc = $ws.CreateShortcut($ShortcutPath)
    $sc.TargetPath = $TargetPath
    $sc.WorkingDirectory = $WorkingDirectory
    $sc.Description = "Open ClassicCars Studio in its own window"
    if ($IconLocation -and (Test-Path -LiteralPath $IconLocation)) {
        $sc.IconLocation = "$IconLocation,0"
    }
    $sc.Save()
}

function Invoke-UninstallStudio {
    param(
        [string]$TargetDir,
        [bool]$RemoveState
    )
    if (-not (Test-Path -LiteralPath $TargetDir)) {
        Write-Host "Nothing to uninstall: $TargetDir does not exist."
        return
    }
    $resolvedTarget = (Resolve-Path -LiteralPath $TargetDir).Path
    Write-Host "Uninstalling $AppName from $resolvedTarget..."

    $startMenuLnk = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\$AppName.lnk"
    $desktopLnk = Join-Path ([Environment]::GetFolderPath("Desktop")) "$AppName.lnk"

    foreach ($lnk in @($startMenuLnk, $desktopLnk)) {
        if ($lnk -and (Test-Path -LiteralPath $lnk)) {
            Remove-Item -LiteralPath $lnk -Force -ErrorAction SilentlyContinue
            Write-Host "Removed shortcut: $lnk"
        }
    }

    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    if ($userPath) {
        $parts = $userPath -split ";" | Where-Object { $_ -and ($_.TrimEnd('\') -ne $resolvedTarget.TrimEnd('\')) }
        $newPath = ($parts -join ";")
        if ($newPath -ne $userPath) {
            [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
            Write-Host "Removed $resolvedTarget from User PATH"
        }
    }

    $files = @(
        "classiccars-studio.exe",
        "classiccars-studio",
        "launcher.py",
        "launcher.sh",
        "launcher.bat",
        "launch-studio.sh",
        "classiccars-studio.cmd",
        "VERSION",
        "README.md",
        "ClassicCars-Studio.desktop",
        $ManifestName,
        "uninstall.sh",
        "uninstall.bat"
    )
    foreach ($f in $files) {
        $p = Join-Path $resolvedTarget $f
        if (Test-Path -LiteralPath $p) {
            Remove-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue
        }
    }

    foreach ($d in @("web", "preview", "assets", "installers")) {
        $dp = Join-Path $resolvedTarget $d
        if (Test-Path -LiteralPath $dp) {
            Remove-Item -LiteralPath $dp -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    $stateDir = Join-Path $resolvedTarget "state"
    if ($RemoveState -and (Test-Path -LiteralPath $stateDir)) {
        Remove-Item -LiteralPath $stateDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "Removed user state directory: $stateDir"
    } elseif (Test-Path -LiteralPath $stateDir) {
        $jsonFiles = Get-ChildItem -LiteralPath $stateDir -File -Filter "*.json" -ErrorAction SilentlyContinue
        if ($jsonFiles) {
            Write-Host "Preserved user state in $stateDir. Pass -PurgeState to remove saved settings."
        } else {
            Remove-Item -LiteralPath $stateDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    $remaining = Get-ChildItem -LiteralPath $resolvedTarget -Force -ErrorAction SilentlyContinue
    if (-not $remaining) {
        Remove-Item -LiteralPath $resolvedTarget -Force -ErrorAction SilentlyContinue
    }

    Write-Host "$AppName has been uninstalled."
}

$targetDir = if ($InstallDir) { $InstallDir } else { Get-DefaultInstallDir }

if ($Uninstall) {
    Invoke-UninstallStudio -TargetDir $targetDir -RemoveState ([bool]$PurgeState)
    exit 0
}

$sourceRoot = Resolve-SourceRoot -Explicit $SourceDir
$version = Get-AppVersion -Root $sourceRoot

$createMenu = -not $NoMenu
$createDesktop = -not $NoDesktop
$createPath = if ($AddToPath) { $true } else { -not $NoPath }

if ($Gui -or (-not $InstallDir -and -not $Yes)) {
    if (-not $Cli -and [Environment]::UserInteractive) {
        $selection = Show-InstallerDialog `
            -Version $version `
            -DefaultDir $targetDir `
            -DefaultMenu $createMenu `
            -DefaultDesktop $createDesktop `
            -DefaultPath $createPath
        $targetDir = $selection.InstallDir
        $createMenu = $selection.CreateMenu
        $createDesktop = $selection.CreateDesktop
        $createPath = $selection.CreatePath
    } else {
        Write-Host "=== $AppName v$version Windows Installer ==="
        $inputDir = Read-Host "Installation Folder [$targetDir]"
        if ($inputDir.Trim()) {
            $targetDir = $inputDir.Trim()
        }
    }
}

$targetDir = [System.IO.Path]::GetFullPath($targetDir)
Write-Host "Installing $AppName v$version -> $targetDir"

if (-not (Test-Path -LiteralPath $targetDir)) {
    New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
}

$stateDir = Join-Path $targetDir "state"
$preservedFiles = @()
if (Test-Path -LiteralPath $stateDir) {
    $preservedFiles = @(Get-ChildItem -LiteralPath $stateDir -File | Select-Object -ExpandProperty Name)
} else {
    New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
}
$tmpTraffic = Join-Path $stateDir "tmp-traffic"
if (-not (Test-Path -LiteralPath $tmpTraffic)) {
    New-Item -ItemType Directory -Path $tmpTraffic -Force | Out-Null
}

$payloadFiles = @(
    "classiccars-studio.exe",
    "classiccars-studio",
    "launcher.py",
    "launcher.sh",
    "launcher.bat",
    "launch-studio.sh",
    "VERSION",
    "README.md"
)

if ($sourceRoot.TrimEnd('\') -ne $targetDir.TrimEnd('\')) {
    foreach ($f in $payloadFiles) {
        $srcFile = Join-Path $sourceRoot $f
        if (Test-Path -LiteralPath $srcFile) {
            Copy-Item -LiteralPath $srcFile -Destination (Join-Path $targetDir $f) -Force
        }
    }

    foreach ($d in @("web", "preview", "assets", "installers")) {
        $srcDir = Join-Path $sourceRoot $d
        $dstDir = Join-Path $targetDir $d
        if (Test-Path -LiteralPath $srcDir) {
            if (Test-Path -LiteralPath $dstDir) {
                Remove-Item -LiteralPath $dstDir -Recurse -Force
            }
            Copy-Item -LiteralPath $srcDir -Destination $dstDir -Recurse -Force
        }
    }
}

$exePath = Join-Path $targetDir "classiccars-studio.exe"
$batPath = Join-Path $targetDir "launcher.bat"
$launchTarget = if (Test-Path -LiteralPath $exePath) { $exePath } else { $batPath }

$cmdWrapper = Join-Path $targetDir "classiccars-studio.cmd"
Set-Content -LiteralPath $cmdWrapper -Value "@echo off`r`n`"$launchTarget`" %*`r`n" -Encoding UTF8

$uninstallBat = Join-Path $targetDir "uninstall.bat"
$uninstallBatContent = @"
@echo off
setlocal EnableExtensions
set "INSTALL_DIR=%~dp0"
if "%INSTALL_DIR:~-1%"=="\" set "INSTALL_DIR=%INSTALL_DIR:~0,-1%"
powershell -NoProfile -ExecutionPolicy Bypass -File "%INSTALL_DIR%\installers\install-windows.ps1" -Uninstall -InstallDir "%INSTALL_DIR%" %*
"@
Set-Content -LiteralPath $uninstallBat -Value $uninstallBatContent -Encoding UTF8

$shortcuts = @{}
if ($createMenu) {
    $startMenuLnk = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\$AppName.lnk"
    New-ShortcutFile -ShortcutPath $startMenuLnk -TargetPath $launchTarget -WorkingDirectory $targetDir -IconLocation $exePath
    $shortcuts["menu_shortcut"] = $startMenuLnk
    Write-Host "Created Start Menu shortcut: $startMenuLnk"
}

if ($createDesktop) {
    $desktopLnk = Join-Path ([Environment]::GetFolderPath("Desktop")) "$AppName.lnk"
    New-ShortcutFile -ShortcutPath $desktopLnk -TargetPath $launchTarget -WorkingDirectory $targetDir -IconLocation $exePath
    $shortcuts["desktop_shortcut"] = $desktopLnk
    Write-Host "Created Desktop shortcut: $desktopLnk"
}

if ($createPath) {
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $entries = if ($userPath) { $userPath -split ";" } else { @() }
    $alreadyPresent = $entries | Where-Object { $_.TrimEnd('\') -ieq $targetDir.TrimEnd('\') }
    if (-not $alreadyPresent) {
        $newPath = if ($userPath) { "$userPath;$targetDir" } else { $targetDir }
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
        Write-Host "Added $targetDir to User PATH"
    }
    $shortcuts["cli_wrapper"] = $cmdWrapper
}

$manifest = @{
    app = $AppName
    version = $version
    install_dir = $targetDir
    installed_at = (Get-Date).ToUniversalTime().ToString("o")
    shortcuts = $shortcuts
}
$manifestPath = Join-Path $targetDir $ManifestName
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

if ($preservedFiles.Count -gt 0) {
    Write-Host "Preserved existing user state files in ${stateDir}: $($preservedFiles -join ', ')"
}

Write-Host ""
Write-Host "Installation complete!"
Write-Host "  Location:    $targetDir"
Write-Host "  Launch with: $launchTarget"
Write-Host "  Uninstall:   $uninstallBat"
