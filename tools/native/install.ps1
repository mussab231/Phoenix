param(
    [string]$PhoenixExe,
    # The extension ships with a fixed public key, so its ID is stable across
    # machines and browser profiles. Override only if you forked the extension
    # and changed the key.
    [string]$ExtensionId = "lmjocnjhfnloppdn",
    [ValidateSet("Chrome", "Edge")]
    [string[]]$Browsers = @("Chrome", "Edge")
)

$ErrorActionPreference = "Stop"

$ManifestDir = $PSScriptRoot

# Locate Phoenix.exe: explicit param, else the default build output.
if (-not $PhoenixExe) {
    $candidates = @(
        (Join-Path $PSScriptRoot "..\..\build\Phoenix.exe"),
        (Join-Path $PSScriptRoot "..\..\Phoenix.exe")
    )
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c) { $PhoenixExe = $c; break }
    }
}
if (-not $PhoenixExe -or -not (Test-Path -LiteralPath $PhoenixExe)) {
    Write-Error "Phoenix.exe not found. Pass -PhoenixExe C:\path\to\Phoenix.exe"
    exit 1
}
$PhoenixExe = (Resolve-Path -LiteralPath $PhoenixExe).Path
Write-Host "Host executable: $PhoenixExe"

# The Chromium native-messaging manifest pins the extension by ID. The
# bundled extension carries a fixed key, so the default ID is stable and no
# prompt is needed; pass -ExtensionId only for a forked build.
if (-not $ExtensionId) {
    $ExtensionId = "lmjocnjhfnloppdn"
}

# Chromium insists the ID end with '/'. Normalise a bare id like 'aabbcc...'.
if ($ExtensionId -notmatch "/$") { $ExtensionId = "$ExtensionId/" }

$hostName = "com.phoenix.host"
$chromiumManifest = [ordered]@{
    name             = $hostName
    description      = "Hands download links to the Phoenix download manager."
    path             = $PhoenixExe
    type             = "stdio"
    allowed_origins  = @("chrome-extension://$ExtensionId")
}
$manifestPath = Join-Path $ManifestDir "manifest.chrome.json"
# Chromium requires strict ASCII/UTF-8 with NO byte-order mark: a BOM makes the
# manifest unreadable and the host silently never starts. .NET's UTF8 no-BOM
# encoder avoids that.
$json = $chromiumManifest | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText($manifestPath, $json, [System.Text.UTF8Encoding]::new($false))
Write-Host "Wrote $manifestPath"

$hostKeys = @()
if ($Browsers -contains "Chrome") {
    $hostKeys += "HKCU:\Software\Google\Chrome\NativeMessagingHosts\$hostName"
}
if ($Browsers -contains "Edge") {
    $hostKeys += "HKCU:\Software\Microsoft\Edge\NativeMessagingHosts\$hostName"
}
foreach ($key in $hostKeys) {
    New-Item -Path $key -Force | Out-Null
    Set-ItemProperty -Path $key -Name "(default)" -Value $manifestPath
    Write-Host "Registered: $key -> $manifestPath"
}

Write-Host ""
Write-Host "Done. Next steps:"
Write-Host "  1. Load the extension from tools\native\extension (chrome://extensions -> Load unpacked)."
Write-Host "  2. Restart the browser, then right-click a link -> Send link to Phoenix,"
Write-Host "     or right-click the toolbar icon -> Catch downloads automatically."