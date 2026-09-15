param(
    [string]$PhoenixExe,
    [string]$ExtensionId,
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

# The Chromium native-messaging manifest pins the extension by ID. An unpacked
# MV3 extension gets a stable ID only if its manifest.json carries a "key"; if
# none is set, read the ID from chrome://extensions and pass it here.
if (-not $ExtensionId) {
    $ExtensionId = Read-Host "Extension ID (from chrome://extensions, or 'skip' to leave the placeholder)"
}
if ($ExtensionId -eq "skip") { $ExtensionId = "YOUR_EXTENSION_ID_HERE" }

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
$chromiumManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
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
if ($ExtensionId -eq "YOUR_EXTENSION_ID_HERE/") {
    Write-Host "  2. Re-run this script with -ExtensionId <id> after loading, so the host"
    Write-Host "     manifest allows your extension."
} else {
    Write-Host "  2. Restart the browser, then right-click a link -> Send link to Phoenix."
}