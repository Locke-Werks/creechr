# install the creechr native messaging host registration for chrome and
# edge. run this once (per user) after building creechr-bridge.exe.
#
# this writes:
#   HKCU\Software\Google\Chrome\NativeMessagingHosts\com.creechr.bridge
#   HKCU\Software\Microsoft\Edge\NativeMessagingHosts\com.creechr.bridge
# both pointing at creechr-bridge-host.json in this folder.

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$hostJson = Join-Path $repoRoot 'creechr-bridge-host.json'
$bridgeExe = Join-Path $repoRoot 'build\creechr-bridge.exe'

if (-not (Test-Path $bridgeExe)) {
    Write-Warning "creechr-bridge.exe not found at $bridgeExe"
    Write-Warning "build it first (cmake --build build) before running this script."
    Write-Warning "the registration will still be written, but creechr will be a no-op until the binary exists."
}

# the host json points at the binary. extension id list is what locks
# this down to specific extensions — if you change extension id, edit
# the json directly.
$hostJsonContent = @{
    name = 'com.creechr.bridge'
    description = 'native messaging bridge between the creechr browser extension and the desktop pet'
    path = ($bridgeExe -replace '\\', '/')
    type = 'stdio'
    allowed_origins = @(
        'chrome-extension://__YOUR_EXTENSION_ID_HERE__/'
    )
} | ConvertTo-Json -Depth 4

Set-Content -Path $hostJson -Value $hostJsonContent -Encoding UTF8
Write-Host "wrote $hostJson"
Write-Host "  edit allowed_origins with your real extension id after loading the unpacked extension"

# write registry pointers
$chromeKey = 'HKCU:\Software\Google\Chrome\NativeMessagingHosts\com.creechr.bridge'
$edgeKey   = 'HKCU:\Software\Microsoft\Edge\NativeMessagingHosts\com.creechr.bridge'

foreach ($k in @($chromeKey, $edgeKey)) {
    if (-not (Test-Path $k)) {
        New-Item -Path $k -Force | Out-Null
    }
    Set-ItemProperty -Path $k -Name '(Default)' -Value $hostJson
    Write-Host "registered $k"
}

Write-Host ""
Write-Host "done. open chrome://extensions, load the extension/ directory unpacked,"
Write-Host "then edit allowed_origins in $hostJson with the extension's real id."
