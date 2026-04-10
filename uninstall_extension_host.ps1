# undo what install_extension_host.ps1 did. removes both registry keys
# and (optionally) the host json file. doesn't touch the extension itself
# — you'd uninstall that from chrome://extensions.

$ErrorActionPreference = 'SilentlyContinue'

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$hostJson = Join-Path $repoRoot 'creechr-bridge-host.json'

$chromeKey = 'HKCU:\Software\Google\Chrome\NativeMessagingHosts\com.creechr.bridge'
$edgeKey   = 'HKCU:\Software\Microsoft\Edge\NativeMessagingHosts\com.creechr.bridge'

foreach ($k in @($chromeKey, $edgeKey)) {
    if (Test-Path $k) {
        Remove-Item -Path $k -Force
        Write-Host "removed $k"
    }
}

if (Test-Path $hostJson) {
    Remove-Item -Path $hostJson -Force
    Write-Host "removed $hostJson"
}

Write-Host "done."
