param([switch]$Silent)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms

$stateDir = Join-Path $env:LOCALAPPDATA "BetterGroup"
$installPath = Join-Path $stateDir "install.json"
$updateCfgPath = Join-Path $stateDir "BetterGroup_update.ini"

function Msg($text, $icon = "Information", [switch]$Force) {
    if ($Silent -and -not $Force) { return }

    [System.Windows.Forms.MessageBox]::Show(
        $text,
        "BetterGroup Update",
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::$icon
    ) | Out-Null
}

if (-not (Test-Path $installPath)) {
    Msg "BetterGroup non risulta installato." "Error"
    exit 1
}

$c = Get-Content $installPath -Raw | ConvertFrom-Json

$cfg = @{}
if (Test-Path $updateCfgPath) {
    foreach ($line in Get-Content $updateCfgPath) {
        $s = $line.Trim()
        if (-not $s -or $s.StartsWith("#")) { continue }
        $eq = $s.IndexOf("=")
        if ($eq -gt 0) {
            $cfg[$s.Substring(0,$eq).Trim()] =
                $s.Substring($eq+1).Trim()
        }
    }
}

if ($cfg["enabled"] -ne "true") {
    Msg "Gli aggiornamenti sono disattivati."
    exit 0
}

$owner = $cfg["github_owner"]
$repo = $cfg["github_repo"]
$assetName = $cfg["asset_name"]
$channel = $cfg["channel"]

if (-not $owner -or -not $repo) {
    Msg "Updater non configurato: repository GitHub mancante." "Error"
    exit 2
}

if (Get-Process eurotrucks2 -ErrorAction SilentlyContinue) {
    Msg "Chiudi ETS2 prima di aggiornare BetterGroup." "Warning"
    exit 3
}

$headers = @{ "User-Agent" = "BetterGroup-Updater" }

try {
    if ($channel -eq "prerelease") {
        $releases = Invoke-RestMethod `
            -Headers $headers `
            -Uri "https://api.github.com/repos/$owner/$repo/releases"

        $release = $releases |
            Where-Object { -not $_.draft } |
            Select-Object -First 1
    }
    else {
        $release = Invoke-RestMethod `
            -Headers $headers `
            -Uri "https://api.github.com/repos/$owner/$repo/releases/latest"
    }
}
catch {
    Msg ("Impossibile controllare GitHub.`n`n" + $_.Exception.Message) "Error"
    exit 4
}

if (-not $release) {
    if (-not $Silent) { Msg "Nessuna release BetterGroup trovata su GitHub." "Warning" }
    exit 5
}

$latest = $release.tag_name.TrimStart("v")
$currentPath = Join-Path $c.plugins "BetterGroup_version.txt"
$current = if (Test-Path $currentPath) {
    (Get-Content $currentPath -Raw).Trim()
} else {
    "unknown"
}

if ($latest -eq $current) {
    if (-not $Silent) { Msg "BetterGroup e' gia' aggiornato.`nVersione: $current" }
    exit 0
}

$asset = $release.assets |
    Where-Object { $_.name -eq $assetName } |
    Select-Object -First 1

if (-not $asset) {
    Msg "La release $latest non contiene $assetName." "Error"
    exit 6
}

$tmp = Join-Path $env:TEMP (
    "BetterGroup_Update_" +
    [Guid]::NewGuid().ToString("N")
)

New-Item -ItemType Directory -Path $tmp -Force | Out-Null

try {
    $zip = Join-Path $tmp $assetName

    Invoke-WebRequest `
        -Headers $headers `
        -Uri $asset.browser_download_url `
        -OutFile $zip

    Expand-Archive $zip -DestinationPath $tmp -Force

    $payload = Join-Path $tmp "payload"
    $manifestPath = Join-Path $payload "manifest.json"

    if (-not (Test-Path $manifestPath)) {
        throw "manifest.json mancante."
    }

    $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json

    if ($manifest.version -ne $latest) {
        throw "Versione manifest ($($manifest.version)) diversa dalla release ($latest)."
    }

    # Verify every file declared by the manifest.
    foreach ($entry in $manifest.files) {
        $path = Join-Path $payload $entry.name

        if (-not (Test-Path $path)) {
            throw "File update mancante: $($entry.name)"
        }

        $hash = (Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant()

        if ($hash -ne $entry.sha256.ToLowerInvariant()) {
            throw "SHA-256 non valido: $($entry.name)"
        }
    }

    # Plugin payload can be replaced immediately because ETS2 is closed.
    Copy-Item (Join-Path $payload "BetterGroup.dll") `
        (Join-Path $c.plugins "BetterGroup.dll") -Force

    Copy-Item (Join-Path $payload "BetterGroup_roles.cfg") `
        (Join-Path $c.plugins "BetterGroup_roles.cfg") -Force

    Copy-Item (Join-Path $payload "version.txt") `
        $currentPath -Force

    # Do NOT overwrite BetterGroup.cfg: preserve user's debug/settings.

    # Self-update updater/uninstaller AFTER this updater EXE exits.
    $parentPid = 0
    try {
        $parentPid = (Get-CimInstance Win32_Process `
            -Filter "ProcessId=$PID").ParentProcessId
    } catch {}

    $newUpdater = Join-Path $payload "BetterGroup_Update.exe"
    $newUninstall = Join-Path $payload "BetterGroup_Uninstall.exe"
    $destUpdater = Join-Path $stateDir "BetterGroup_Update.exe"
    $destUninstall = Join-Path $stateDir "BetterGroup_Uninstall.exe"

    if ((Test-Path $newUpdater) -or (Test-Path $newUninstall)) {
        $helper = Join-Path $tmp "finish_update.ps1"

        @"
`$ErrorActionPreference = "SilentlyContinue"

if ($parentPid -gt 0) {
    while (Get-Process -Id $parentPid -ErrorAction SilentlyContinue) {
        Start-Sleep -Milliseconds 100
    }
}

Start-Sleep -Milliseconds 150

if (Test-Path "$newUpdater") {
    Copy-Item "$newUpdater" "$destUpdater" -Force
}

if (Test-Path "$newUninstall") {
    Copy-Item "$newUninstall" "$destUninstall" -Force
}

Remove-Item "$tmp" -Recurse -Force -ErrorAction SilentlyContinue
"@ | Set-Content $helper -Encoding UTF8

        Start-Process powershell.exe `
            -WindowStyle Hidden `
            -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$helper`""
    }

    if (-not $Silent) { Msg "BetterGroup aggiornato.`n$current -> $latest" }
}
catch {
    Msg ("Aggiornamento annullato.`nNessun pacchetto non verificato verra' installato.`n`n" +
        $_.Exception.Message) "Error"
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
    exit 7
}
