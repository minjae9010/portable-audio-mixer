param([string]$Package = (Join-Path $PSScriptRoot '../downloads/driver'))
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script in an Administrator PowerShell.'
}
$inf = Join-Path $Package 'PortableMixerVirtualMic.inf'
$catalog = Join-Path $Package 'PortableMixerVirtualMic.cat'
if (!(Test-Path -LiteralPath $inf) -or !(Test-Path -LiteralPath $catalog)) { throw 'Driver INF or catalog is missing.' }
$signature = Get-AuthenticodeSignature -LiteralPath $catalog
if ($signature.Status -ne 'Valid') {
    throw 'The driver catalog is unsigned or untrusted. Obtain a signed package first. No system changes were made.'
}
# Stage only. The app creates its own temporary device with SwDeviceCreate.
& pnputil.exe /add-driver $inf
if ($LASTEXITCODE -notin @(0,3010)) { throw "Driver staging failed: $LASTEXITCODE" }
Write-Host 'Driver package staged. Start PortableMixer.exe and allow its UAC prompt.'
Write-Host 'Windows must also accept the kernel signature for the device to start.'
