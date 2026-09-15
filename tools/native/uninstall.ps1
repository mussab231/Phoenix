$ErrorActionPreference = "Stop"

$hostName = "com.phoenix.host"
$keys = @(
    "HKCU:\Software\Google\Chrome\NativeMessagingHosts\$hostName",
    "HKCU:\Software\Microsoft\Edge\NativeMessagingHosts\$hostName",
    "HKCU:\Software\Mozilla\NativeMessagingHosts\$hostName"
)
foreach ($key in $keys) {
    if (Test-Path -LiteralPath $key) {
        Remove-Item -Path $key -Recurse -Force
        Write-Host "Removed: $key"
    }
}
Write-Host "Phoenix native messaging host unregistered."