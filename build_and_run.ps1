$ErrorActionPreference = "Stop"

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host " ExeLite - Build and Run Script"          -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

# Locate ADB
function Find-Adb {
    if (Get-Command adb -ErrorAction SilentlyContinue) { return "adb" }
    $candidates = @(
        "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe",
        "C:\Android\platform-tools\adb.exe",
        "C:\Users\$env:USERNAME\AppData\Local\Android\Sdk\platform-tools\adb.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    return $null
}

$adbPath = Find-Adb
if (-not $adbPath) {
    Write-Host "[!] ADB bulunamadi. Kurulum adimi atlanacak." -ForegroundColor Yellow
    $skipInstall = $true
} else {
    $skipInstall = $false
    Write-Host "ADB: $adbPath" -ForegroundColor DarkGray
}

# PRE: engine_v3.zip yoksa olustur
$zipPath = "app\src\main\assets\engine_v3.zip"
if (-not (Test-Path $zipPath)) {
    Write-Host ""
    Write-Host "[PRE] engine_v3.zip bulunamadi, olusturuluyor..." -ForegroundColor Yellow
    & .\build_engine_v3.ps1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "engine_v3.zip olusturulamadi!" -ForegroundColor Red
        exit 1
    }
} else {
    $sizeMB = [math]::Round((Get-Item $zipPath).Length / 1MB, 1)
    Write-Host "[PRE] engine_v3.zip mevcut ($sizeMB MB), atlanıyor." -ForegroundColor DarkGray
}

# 1. APK Build
Write-Host ""
Write-Host "[1/3] Building debug APK..." -ForegroundColor Yellow
.\gradlew.bat assembleDebug
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build basarisiz!" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "Build basarili!" -ForegroundColor Green

if ($skipInstall) {
    Write-Host ""
    Write-Host "[!] ADB yok - sadece build yapildi." -ForegroundColor Yellow
    Write-Host "    APK: app\build\outputs\apk\debug\app-debug.apk"
    exit 0
}

# 2. Install APK
$apkPath = "app\build\outputs\apk\debug\app-debug.apk"
if (-not (Test-Path $apkPath)) {
    Write-Host "[!] APK bulunamadi: $apkPath" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "[2/3] Cihaza APK yukleniyor..." -ForegroundColor Yellow
& $adbPath install -r $apkPath
if ($LASTEXITCODE -ne 0) {
    Write-Host "Kurulum basarisiz. Cihazin bagli ve acik olduguna emin ol." -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "Kurulum basarili!" -ForegroundColor Green

Write-Host "  -> Uygulama verisi temizleniyor..." -ForegroundColor DarkGray
& $adbPath shell pm clear com.exelite.launcher

# 3. Launch
Write-Host ""
Write-Host "[3/3] ExeLite baslatiliyor..." -ForegroundColor Yellow
& $adbPath shell am start -n "com.exelite.launcher/.MainActivity"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Uygulama baslatılamadi." -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "Hazir! ExeLite cihazda calisiyor." -ForegroundColor Green
