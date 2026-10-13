# Install / restore the openvr_api.dll proxy for Borderlands 2 VR.
#   .\install.ps1            -> install the proxy (backs up the original first)
#   .\install.ps1 -Restore   -> put the original back
param([switch]$Restore)

$game = 'C:\Program Files (x86)\Steam\steamapps\common\Borderlands 2 VR\Binaries\Win64'
$dll  = Join-Path $game 'openvr_api.dll'
$bak  = Join-Path $game 'openvr_api.dll.bak'
$orig = Join-Path $game 'openvr_api_orig.dll'
$built = Join-Path $PSScriptRoot 'build\openvr_api.dll'

if (Get-Process Borderlands2VR -ErrorAction SilentlyContinue) {
    Write-Error 'Close Borderlands 2 VR first.'; exit 1
}

if ($Restore) {
    if (!(Test-Path $bak)) { Write-Error "No backup at $bak"; exit 1 }
    Copy-Item $bak $dll -Force
    if (Test-Path $orig) { Remove-Item $orig -Force }
    Write-Host "Restored original openvr_api.dll"
    exit 0
}

if (!(Test-Path $built)) { Write-Error "Build first: $built missing"; exit 1 }

# 1) back up the pristine original once
if (!(Test-Path $bak)) { Copy-Item $dll $bak -Force; Write-Host "Backed up original -> openvr_api.dll.bak" }

# 2) the real loader goes to openvr_api_orig.dll (our proxy forwards to it)
Copy-Item $bak $orig -Force

# 3) our proxy takes the openvr_api.dll name
Copy-Item $built $dll -Force
Write-Host "Installed proxy. Original saved as openvr_api.dll.bak / openvr_api_orig.dll"
