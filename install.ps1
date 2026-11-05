<#
.SYNOPSIS
    Instala o plugin CameraPhone 4K Pro e (se houver celular conectado) instala o CameraPhone.apk via ADB.
#>
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

$DllSrc = if (Test-Path (Join-Path $Root "dist\cameraphone.dll")) { Join-Path $Root "dist\cameraphone.dll" } else { Join-Path $Root "cameraphone.dll" }
$PluginsDir = "C:\Program Files\obs-studio\obs-plugins\64bit"
$DataDir = "C:\Program Files\obs-studio\data\obs-plugins\cameraphone"
$ApkSrc = if (Test-Path (Join-Path $Root "dist\CameraPhone.apk")) { Join-Path $Root "dist\CameraPhone.apk" } else { Join-Path $Root "CameraPhone.apk" }
$AdbExe = if (Test-Path (Join-Path $Root "dist\adb\adb.exe")) {
    Join-Path $Root "dist\adb\adb.exe"
} elseif (Test-Path (Join-Path $Root "adb\adb.exe")) {
    Join-Path $Root "adb\adb.exe"
} else {
    "C:\Users\Vinic\AppData\Local\Android\Sdk\platform-tools\adb.exe"
}

Write-Host "=== CameraPhone 4K Pro Installer ===" -ForegroundColor Cyan

if (Test-Path $PluginsDir) {
    try {
        Remove-Item (Join-Path $PluginsDir "cameraphone-obs.dll") -Force -ErrorAction SilentlyContinue
        Copy-Item $DllSrc (Join-Path $PluginsDir "cameraphone.dll") -Force
        New-Item -ItemType Directory -Force -Path (Join-Path $DataDir "adb") | Out-Null
        if (Test-Path $ApkSrc) { Copy-Item $ApkSrc (Join-Path $DataDir "CameraPhone.apk") -Force }
        if (Test-Path $AdbExe) {
            $AdbDir = Split-Path -Parent $AdbExe
            Copy-Item (Join-Path $AdbDir "adb.exe") (Join-Path $DataDir "adb\adb.exe") -Force -ErrorAction SilentlyContinue
            Copy-Item (Join-Path $AdbDir "AdbWinApi.dll") (Join-Path $DataDir "adb\AdbWinApi.dll") -Force -ErrorAction SilentlyContinue
            Copy-Item (Join-Path $AdbDir "AdbWinUsbApi.dll") (Join-Path $DataDir "adb\AdbWinUsbApi.dll") -Force -ErrorAction SilentlyContinue
        }
        Write-Host "[OK] Plugin + ADB instalados em: $PluginsDir\cameraphone.dll" -ForegroundColor Green
    } catch {
        Write-Host "[!] Execute CameraPhone.exe para instalar automaticamente com elevacao UAC." -ForegroundColor Yellow
    }
}

if (Test-Path $AdbExe) {
    $devices = & $AdbExe devices | Select-String "\tdevice$"
    if ($devices) {
        Write-Host "[ADB] Dispositivo Android detectado! Instalando CameraPhone.apk..." -ForegroundColor Cyan
        & $AdbExe install -r -g $ApkSrc
        & $AdbExe forward tcp:4748 tcp:4748
        Write-Host "[OK] App CameraPhone instalado e tunel USB (porta 4748) ativo!" -ForegroundColor Green
    } else {
        Write-Host "[i] Nenhum celular Android conectado via ADB no momento." -ForegroundColor Gray
    }
}
