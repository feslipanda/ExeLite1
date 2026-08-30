$ErrorActionPreference = "Stop"
Write-Host "=== Building Engine V3 Package ===" -ForegroundColor Cyan

$workspace = "c:\deneme\deneme1\engine_workspace"
$wsl       = "/mnt/c/deneme/deneme1/engine_workspace"
$winlator  = "/mnt/c/deneme/deneme1/winlator_app_src/app/src/main/assets"
$exelite   = "/mnt/c/deneme/deneme1/ExeLite"
$assetsOut = "c:\deneme\deneme1\ExeLite\app\src\main\assets\engine_v3.zip"
$wslOut    = "/mnt/c/deneme/deneme1/ExeLite/app/src/main/assets/engine_v3.zip"
$interp    = "/data/user/0/com.exelite.launcher/files/engine_v2/rootfs/lib/ld-linux-aarch64.so.1"

# 1. Clean old workspace completely
Write-Host "[1/7] Cleaning old workspace..." -ForegroundColor Yellow
wsl -u root bash -c "chmod -R 777 $wsl 2>/dev/null || true; rm -rf $wsl; echo clean_done"
Start-Sleep -Milliseconds 500
if (Test-Path $workspace) {
    Remove-Item -Recurse -Force $workspace -ErrorAction SilentlyContinue
}

# 2. Rootfs (exclude /dev to avoid NTFS special file errors)
Write-Host "[2/7] Extracting rootfs.tzst..." -ForegroundColor Yellow
wsl -u root bash -c "mkdir -p $wsl/engine_v3/rootfs"
wsl -u root bash -c "tar --zstd -xf $winlator/rootfs.tzst -C $wsl/engine_v3/rootfs --exclude='./dev' 2>/dev/null || true"

# 3. Box64 (nested path: ./usr/local/bin/box64 -> 4 levels)
Write-Host "[3/7] Extracting Box64..." -ForegroundColor Yellow
wsl -u root bash -c "mkdir -p $wsl/engine_v3/bin && tar --zstd -xf $winlator/box64/box64-0.4.4.tzst -C $wsl/engine_v3/bin --strip-components=4 && chmod +x $wsl/engine_v3/bin/box64"

# 4. DXVK
Write-Host "[4/7] Extracting DXVK..." -ForegroundColor Yellow
wsl -u root bash -c "mkdir -p $wsl/engine_v3/dxvk && tar --zstd -xf $winlator/dxwrapper/dxvk-2.4.1.tzst -C $wsl/engine_v3/dxvk 2>/dev/null || true"

# 5. Wine 11 (strip top-level wine-*/ dir)
Write-Host "[5/7] Extracting Wine 11..." -ForegroundColor Yellow
wsl -u root bash -c "mkdir -p $wsl/engine_v3/wine && tar -xzf $exelite/app/src/main/assets/runtime/wine.tar.gz -C $wsl/engine_v3/wine --strip-components=1 2>/dev/null || true"

# 6. Wine Prefix (strip top-level .wine/)
Write-Host "[6/7] Setting up Wine Prefix..." -ForegroundColor Yellow
wsl -u root bash -c "mkdir -p $wsl/engine_v3/wine_prefix && tar --zstd -xf $winlator/container_pattern.tzst -C $wsl/engine_v3/wine_prefix --strip-components=1 2>/dev/null || true"
wsl -u root bash -c "mkdir -p $wsl/engine_v3/wine_prefix/drive_c/windows/system32 $wsl/engine_v3/wine_prefix/drive_c/windows/syswow64 $wsl/engine_v3/wine_prefix/dosdevices $wsl/engine_v3/tmp"
wsl -u root bash -c "cp -rf $wsl/engine_v3/wine/lib/wine/x86_64-windows/* $wsl/engine_v3/wine_prefix/drive_c/windows/system32/ 2>/dev/null || true"
wsl -u root bash -c "cp -rf $wsl/engine_v3/wine/lib/wine/i386-windows/* $wsl/engine_v3/wine_prefix/drive_c/windows/syswow64/ 2>/dev/null || true"
wsl -u root bash -c "rm -f $wsl/engine_v3/wine_prefix/dosdevices/c: $wsl/engine_v3/wine_prefix/dosdevices/d: $wsl/engine_v3/wine_prefix/dosdevices/z: 2>/dev/null || true"
wsl -u root bash -c "ln -sf ../drive_c $wsl/engine_v3/wine_prefix/dosdevices/c: && ln -sf /storage/emulated/0 $wsl/engine_v3/wine_prefix/dosdevices/d: && ln -sf / $wsl/engine_v3/wine_prefix/dosdevices/z:"
wsl -u root bash -c "touch $wsl/engine_v3/wine_prefix/.exelite_setup_done"

# 7. Patch Box64 ELF interpreter (Android ld path)
Write-Host "[7/7] Patching Box64 ELF interpreter..." -ForegroundColor Yellow
wsl -u root bash -c "patchelf --set-interpreter $interp $wsl/engine_v3/bin/box64 2>/dev/null || true"

# ZIP via WSL zip (preserves symlinks - PowerShell Compress-Archive breaks them)
Write-Host "Creating engine_v3.zip via WSL..." -ForegroundColor Yellow
wsl -u root bash -c "which zip > /dev/null 2>&1 || apt-get install -y zip > /dev/null 2>&1"
wsl -u root bash -c "rm -f $wslOut && cd $wsl/engine_v3 && zip -r --symlinks $wslOut . 2>&1 | tail -n 5"

if (Test-Path $assetsOut) {
    $sizeMB = [math]::Round((Get-Item $assetsOut).Length / 1MB, 1)
    Write-Host ""
    Write-Host "=== SUCCESS! engine_v3.zip created ($sizeMB MB) ===" -ForegroundColor Green
} else {
    Write-Host "ERROR: engine_v3.zip was not created!" -ForegroundColor Red
    exit 1
}
