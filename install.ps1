<#
.SYNOPSIS
    Instala o plugin CameraPhone 4K Pro no OBS Studio e (se houver celular conectado) instala o APK via ADB.
#>
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

$DllSrc = Join-Path $Root "obs-plugin\build\cameraphone-obs.dll"
$ObsPluginsDir = "C:\Program Files\obs-studio\obs-plugins\64bit"
$ApkSrc = Join-Path $Root "android-app\app\build\outputs\apk\debug\app-debug.apk"
$AdbExe = "C:\Users\Vinic\AppData\Local\Android\Sdk\platform-tools\adb.exe"

Write-Host "=== CameraPhone OBS Installer ===" -ForegroundColor Cyan

if (Test-Path $ObsPluginsDir) {
    try {
        Copy-Item $DllSrc (Join-Path $ObsPluginsDir "cameraphone-obs.dll") -Force
        Write-Host "[OK] Plugin instalado em: $ObsPluginsDir\cameraphone-obs.dll" -ForegroundColor Green
    } catch {
        Write-Host "[!] Permissão de Administrador necessária para copiar para C:\Program Files\obs-studio." -ForegroundColor Yellow
        Write-Host "    Execute este script como Administrador ou copie manualmente:" -ForegroundColor Yellow
        Write-Host "    Copy-Item '$DllSrc' '$ObsPluginsDir\cameraphone-obs.dll' -Force"
    }
}

if (Test-Path $AdbExe) {
    $devices = & $AdbExe devices | Select-String "\tdevice$"
    if ($devices) {
        Write-Host "[ADB] Dispositivo Android detectado! Instalando APK..." -ForegroundColor Cyan
        & $AdbExe install -r $ApkSrc
        & $AdbExe forward tcp:4748 tcp:4748
        Write-Host "[OK] App CameraPhone OBS instalado e túnel USB (porta 4748) ativo!" -ForegroundColor Green
    } else {
        Write-Host "[i] Nenhum celular Android conectado via ADB no momento." -ForegroundColor Gray
        Write-Host "    Quando conectar o celular com Depuração USB ativa, rode novamente este script ou:" -ForegroundColor Gray
        Write-Host "    & '$AdbExe' install -r '$ApkSrc'" -ForegroundColor Gray
    }
}
