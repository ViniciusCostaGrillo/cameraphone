<#
.SYNOPSIS
    Instala o plugin CameraPhone 4K Pro e (se houver celular conectado) instala o CameraPhone.apk via ADB.
#>
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

$DllSrc = Join-Path $Root "dist\cameraphone.dll"
$PluginsDir = "C:\Program Files\obs-studio\obs-plugins\64bit"
$ApkSrc = Join-Path $Root "dist\CameraPhone.apk"
$AdbExe = "C:\Users\Vinic\AppData\Local\Android\Sdk\platform-tools\adb.exe"

Write-Host "=== CameraPhone 4K Pro Installer ===" -ForegroundColor Cyan

if (Test-Path $PluginsDir) {
    try {
        Remove-Item (Join-Path $PluginsDir "cameraphone-obs.dll") -Force -ErrorAction SilentlyContinue
        Copy-Item $DllSrc (Join-Path $PluginsDir "cameraphone.dll") -Force
        Write-Host "[OK] Plugin instalado em: $PluginsDir\cameraphone.dll" -ForegroundColor Green
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
