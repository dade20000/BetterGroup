$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms

$stateDir = Join-Path $env:LOCALAPPDATA "BetterGroup"
$state = Join-Path $stateDir "install.json"

if (-not (Test-Path $state)) {
    [System.Windows.Forms.MessageBox]::Show(
        "BetterGroup non risulta installato.",
        "BetterGroup Uninstall")
    exit 0
}

if (Get-Process eurotrucks2 -ErrorAction SilentlyContinue) {
    [System.Windows.Forms.MessageBox]::Show(
        "Chiudi ETS2 prima di disinstallare BetterGroup.",
        "BetterGroup Uninstall")
    exit 1
}

$c = Get-Content $state -Raw | ConvertFrom-Json

$result = [System.Windows.Forms.MessageBox]::Show(
    "Rimuovere BetterGroup e ripristinare lo staff shield originale?",
    "BetterGroup Uninstall",
    [System.Windows.Forms.MessageBoxButtons]::YesNo,
    [System.Windows.Forms.MessageBoxIcon]::Question)

if ($result -ne [System.Windows.Forms.DialogResult]::Yes) {
    exit 0
}

Remove-Item (Join-Path $c.plugins "BetterGroup.dll") -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $c.plugins "BetterGroup_roles.cfg") -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $c.plugins "BetterGroup.cfg") -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $c.plugins "BetterGroup_version.txt") -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $c.plugins "BetterGroup.log") -Force -ErrorAction SilentlyContinue

if ($c.backup -and (Test-Path $c.backup)) {
    Copy-Item $c.backup $c.staff -Force
}

$menu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\BetterGroup"
Remove-Item $menu -Recurse -Force -ErrorAction SilentlyContinue


try {
    Unregister-ScheduledTask `
        -TaskName "BetterGroup Update Check" `
        -Confirm:$false `
        -ErrorAction SilentlyContinue
}
catch {}

# Keep the running uninstaller EXE until process exits; delete the remaining state.
Remove-Item $state -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $stateDir "BetterGroup_update.ini") -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $stateDir "BetterGroup_Update.exe") -Force -ErrorAction SilentlyContinue

[System.Windows.Forms.MessageBox]::Show(
    "BetterGroup rimosso. Lo shield TruckersMP originale e' stato ripristinato.",
    "BetterGroup Uninstall")
