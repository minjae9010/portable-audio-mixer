param([string]$WdkRoot)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (!$WdkRoot) { $WdkRoot = Join-Path $root 'build/driver-packages/Microsoft.Windows.WDK.x64.10.0.26100.6584/c' }
$sdkVersion = '10.0.26100.0'
$sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ Build Tools are required.' }
$vc = Get-ChildItem (Join-Path $vs 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$compiler = Join-Path $vc.FullName 'bin/Hostx64/x64/cl.exe'
$linker = Join-Path $vc.FullName 'bin/Hostx64/x64/link.exe'
$include = @("$WdkRoot/Include/$sdkVersion/km", "$WdkRoot/Include/$sdkVersion/km/crt", "$WdkRoot/Include/wdf/kmdf/1.33", "$sdk/Include/$sdkVersion/shared", "$sdk/Include/$sdkVersion/um", "$vc/include", "$root/driver", "$root/driver/vendor/sysvad", "$root/driver/vendor/sysvad/EndpointsCommon", "$root/driver/vendor/sysvad/TabletAudioSample")
if (!(Test-Path "$WdkRoot/Include/$sdkVersion/km/ntddk.h")) { throw 'WDK package not found. See driver/README.ko.md.' }
$out = Join-Path $root 'build/driver'
$obj = Join-Path $root 'build/driver-obj'
New-Item -ItemType Directory -Force $out,$obj | Out-Null
[xml]$project = Get-Content (Join-Path $root 'driver/PortableMixerVirtualMic.vcxproj')
$sources = $project.Project.ItemGroup.ClCompile | Where-Object { $_.Include } | ForEach-Object { Join-Path "$root/driver" $_.Include }
$args = @('/nologo','/c','/kernel','/Zl','/GR-','/EHs-c-','/GS','/W4','/WX','/O2','/std:c++17','/wd4595','/wd4324', '/D_AMD64_','/DAMD64','/D_WIN64','/D_WIN32','/DUNICODE','/D_UNICODE','/D_WIN32_WINNT=0x0A00','/DNTDDI_VERSION=0x0A00000B','/DPC_IMPLEMENTATION','/D_USE_WAVERT_','/D_USE_IPortClsRuntimePower','/D_NEW_DELETE_OPERATORS_','/DDEBUG_LEVEL=DEBUGLVL_TERSE')
$args += $include | ForEach-Object { "/I$_" }
$objects = @()
foreach ($source in $sources) {
    $object = Join-Path $obj ([IO.Path]::GetFileNameWithoutExtension($source) + '.obj')
    & $compiler @args "/Fo$object" $source
    if ($LASTEXITCODE) { throw "Driver compile failed: $source" }
    $objects += $object
}
$libs = @("$WdkRoot/Lib/$sdkVersion/km/x64", "$WdkRoot/Lib/wdf/kmdf/x64/1.33", "$vc/lib/x64") | ForEach-Object { "/LIBPATH:$_" }
& $linker /nologo /driver /subsystem:native,10.00 /entry:FxDriverEntry /nodefaultlib /dynamicbase /nxcompat /integritycheck /release "/out:$out/PortableMixerVirtualMic.sys" @libs @objects ntoskrnl.lib hal.lib wmilib.lib portcls.lib stdunk.lib libcntpr.lib wdmsec.lib WdfLdr.lib WdfDriverEntry.lib BufferOverflowFastFailK.lib
if ($LASTEXITCODE) { throw 'Driver link failed' }
(Get-Content "$root/driver/PortableMixerVirtualMic.inx" -Raw).Replace('$KMDFVERSION$', '1.33') | Set-Content "$out/PortableMixerVirtualMic.inf" -Encoding ascii
& "$WdkRoot/tools/$sdkVersion/x64/infverif.exe" /w "$out/PortableMixerVirtualMic.inf"
if ($LASTEXITCODE) { throw 'INF validation failed' }
& "$WdkRoot/bin/$sdkVersion/x86/Inf2Cat.exe" "/driver:$out" /os:10_NI_X64,10_GE_X64 /uselocaltime
if ($LASTEXITCODE) { throw 'Catalog generation failed' }
Write-Host "Unsigned driver package: $out"
Write-Host 'This script does not install, load, sign, or change Windows boot/security settings.'


