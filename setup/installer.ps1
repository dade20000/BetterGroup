param([switch]$Quiet)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

function Get-SteamLibraries {
    $libs = New-Object System.Collections.Generic.List[string]
    try {
        $steam = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction Stop).SteamPath
        if ($steam) {
            $steam = $steam -replace '/', '\'
            $libs.Add($steam)

            $vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
            if (Test-Path $vdf) {
                $text = Get-Content $vdf -Raw -ErrorAction SilentlyContinue
                [regex]::Matches($text, '"path"\s+"([^"]+)"') | ForEach-Object {
                    $p = $_.Groups[1].Value -replace '\\\\', '\'
                    if ($p -and -not $libs.Contains($p)) {
                        $libs.Add($p)
                    }
                }
            }
        }
    } catch {}
    return $libs
}

function Find-Ets2 {
    foreach ($lib in (Get-SteamLibraries)) {
        $p = Join-Path $lib "steamapps\common\Euro Truck Simulator 2"
        if (Test-Path (Join-Path $p "bin\win_x64")) {
            return $p
        }
    }
    return $null
}

function Find-TmpStaffPng {
    $roots = @(
        (Join-Path $env:APPDATA "TruckersMP"),
        (Join-Path $env:LOCALAPPDATA "TruckersMP")
    )

    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }

        $f = Get-ChildItem $root -Filter "truckersmp_staff.png" -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.DirectoryName -match '[\\/]shared_mod[\\/]ui$' } |
            Select-Object -First 1

        if ($f) { return $f.FullName }
    }

    return $null
}

function Pick-Folder($Description) {
    $d = New-Object System.Windows.Forms.FolderBrowserDialog
    $d.Description = $Description
    $d.ShowNewFolderButton = $false

    if ($d.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
        return $d.SelectedPath
    }

    return $null
}

function Make-MagentaMarker($SourcePng, $DestPng) {
    $src = [System.Drawing.Bitmap]::FromFile($SourcePng)

    try {
        $dst = New-Object System.Drawing.Bitmap $src.Width, $src.Height,
            ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

        try {
            for ($y=0; $y -lt $src.Height; $y++) {
                for ($x=0; $x -lt $src.Width; $x++) {
                    $c = $src.GetPixel($x,$y)

                    if ($c.A -eq 0) {
                        $dst.SetPixel($x,$y,[System.Drawing.Color]::Transparent)
                    } else {
                        $dst.SetPixel(
                            $x,$y,
                            [System.Drawing.Color]::FromArgb(
                                $c.A,255,0,255))
                    }
                }
            }

            $dst.Save(
                $DestPng,
                [System.Drawing.Imaging.ImageFormat]::Png)
        }
        finally {
            $dst.Dispose()
        }
    }
    finally {
        $src.Dispose()
    }
}

function New-Shortcut($Path,$Target,$Arguments="") {
    $shell = New-Object -ComObject WScript.Shell
    $sc = $shell.CreateShortcut($Path)
    $sc.TargetPath = $Target
    $sc.Arguments = $Arguments
    $sc.WorkingDirectory = Split-Path $Target
    $sc.Save()
}

$root = $PSScriptRoot

foreach ($required in @(
    "BetterGroup.dll",
    "BetterGroup_roles.cfg",
    "BetterGroup.cfg",
    "version.txt",
    "BetterGroup_Update.exe",
    "BetterGroup_Uninstall.exe",
    "BetterGroup_update.ini"
)) {
    if (-not (Test-Path (Join-Path $root $required))) {
        throw "File installer mancante: $required"
    }
}

if (Get-Process eurotrucks2 -ErrorAction SilentlyContinue) {
    throw "Chiudi Euro Truck Simulator 2 prima di installare BetterGroup."
}

$ets2 = Find-Ets2
if (-not $ets2) {
    $ets2 = Pick-Folder "Seleziona la cartella principale di Euro Truck Simulator 2"
}

if (-not $ets2 -or -not (Test-Path (Join-Path $ets2 "bin\win_x64"))) {
    throw "Cartella ETS2 non valida."
}

$staff = Find-TmpStaffPng

if (-not $staff) {
    $ui = Pick-Folder "Seleziona la cartella TruckersMP shared_mod\ui"
    if ($ui) {
        $candidate = Join-Path $ui "truckersmp_staff.png"
        if (Test-Path $candidate) { $staff = $candidate }
    }
}

if (-not $staff) {
    throw "truckersmp_staff.png non trovato."
}

$plugins = Join-Path $ets2 "bin\win_x64\plugins"
$stateDir = Join-Path $env:LOCALAPPDATA "BetterGroup"
$backup = Join-Path $stateDir "truckersmp_staff.original.png"

New-Item -ItemType Directory -Path $plugins -Force | Out-Null
New-Item -ItemType Directory -Path $stateDir -Force | Out-Null

Write-Host ""
Write-Host "BetterGroup 1.0.0 RC2"
Write-Host "======================"
Write-Host "ETS2 plugins:"
Write-Host "  $plugins"
Write-Host "TruckersMP staff icon:"
Write-Host "  $staff"
Write-Host ""

if (-not $Quiet) {
    $answer = Read-Host "Continuare con l'installazione? [S/n]"
    if ($answer -and $answer.ToLower() -notin @("s","si","sì","y","yes")) {
        exit 0
    }
}

# Backup one clean/original copy.
if (-not (Test-Path $backup)) {
    Copy-Item $staff $backup -Force
}

# Remove only our previous plugin builds.
Get-ChildItem $plugins -Filter "TMPStaffShield*.dll" -File -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue

Remove-Item (Join-Path $plugins "BetterGroup.dll") -Force -ErrorAction SilentlyContinue

Copy-Item (Join-Path $root "BetterGroup.dll") (Join-Path $plugins "BetterGroup.dll") -Force
Copy-Item (Join-Path $root "BetterGroup_roles.cfg") (Join-Path $plugins "BetterGroup_roles.cfg") -Force

# Preserve an existing user cfg on reinstall/update.
if (-not (Test-Path (Join-Path $plugins "BetterGroup.cfg"))) {
    Copy-Item (Join-Path $root "BetterGroup.cfg") (Join-Path $plugins "BetterGroup.cfg") -Force
}

Copy-Item (Join-Path $root "version.txt") (Join-Path $plugins "BetterGroup_version.txt") -Force

# Create marker FROM the local installed TMP art. No TMP image is bundled.
Make-MagentaMarker $backup $staff

Copy-Item (Join-Path $root "BetterGroup_Update.exe") (Join-Path $stateDir "BetterGroup_Update.exe") -Force
Copy-Item (Join-Path $root "BetterGroup_Uninstall.exe") (Join-Path $stateDir "BetterGroup_Uninstall.exe") -Force
Copy-Item (Join-Path $root "BetterGroup_update.ini") (Join-Path $stateDir "BetterGroup_update.ini") -Force


# Read updater preferences installed with the package.
$updateCfg = @{}
foreach ($line in Get-Content (Join-Path $stateDir "BetterGroup_update.ini") -ErrorAction SilentlyContinue) {
    $s = $line.Trim()
    if (-not $s -or $s.StartsWith("#")) { continue }
    $eq = $s.IndexOf("=")
    if ($eq -gt 0) {
        $updateCfg[$s.Substring(0,$eq).Trim()] = $s.Substring($eq+1).Trim()
    }
}

@{
    ets2 = $ets2
    plugins = $plugins
    staff = $staff
    backup = $backup
    state = $stateDir
} | ConvertTo-Json | Set-Content (Join-Path $stateDir "install.json") -Encoding UTF8

# Start menu shortcuts.
$menu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\BetterGroup"
New-Item -ItemType Directory -Path $menu -Force | Out-Null

New-Shortcut (Join-Path $menu "Check for updates.lnk") (Join-Path $stateDir "BetterGroup_Update.exe")
New-Shortcut (Join-Path $menu "Uninstall BetterGroup.lnk") (Join-Path $stateDir "BetterGroup_Uninstall.exe")


# Optional silent automatic check at user logon.
if ($updateCfg["auto_check"] -eq "true") {
    try {
        $taskName = "BetterGroup Update Check"
        $updaterExe = Join-Path $stateDir "BetterGroup_Update.exe"

        $action = New-ScheduledTaskAction `
            -Execute $updaterExe `
            -Argument "--silent"

        $trigger = New-ScheduledTaskTrigger -AtLogOn

        $settings = New-ScheduledTaskSettingsSet `
            -StartWhenAvailable `
            -AllowStartIfOnBatteries `
            -DontStopIfGoingOnBatteries

        Register-ScheduledTask `
            -TaskName $taskName `
            -Action $action `
            -Trigger $trigger `
            -Settings $settings `
            -Description "Checks for BetterGroup updates at user logon." `
            -Force | Out-Null
    }
    catch {
        # Automatic check is optional. Manual Start-menu update remains available.
    }
}

Write-Host ""
Write-Host "BetterGroup installato."
Write-Host "Updater manuale: Menu Start -> BetterGroup -> Check for updates"`nWrite-Host "Controllo automatico update: configurato se auto_check=true"
Write-Host "Nessuna notifica verde 'loaded' viene mostrata in game."
Write-Host ""
Read-Host "Premi INVIO per chiudere"
