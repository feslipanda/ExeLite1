# ExeLite — Geliştirici Notları

## Proje Yapısı

```
ExeLite/
├── app/
│   └── src/main/
│       ├── AndroidManifest.xml
│       ├── cpp/                          ← C++ Motor (NDK)
│       │   ├── CMakeLists.txt
│       │   ├── engine/
│       │   │   ├── engine_types.h        ← Tüm tip tanımları
│       │   │   ├── engine_core.h/cpp     ← Ana motor yöneticisi
│       │   │   ├── process_manager.h/cpp ← Box64+Wine process
│       │   │   ├── input_bridge.h/cpp    ← Gamepad → uinput
│       │   │   ├── render_bridge.h/cpp   ← Surface render
│       │   │   ├── wine_config.h/cpp     ← Wine prefix kurulum
│       │   │   └── exelite_jni.cpp       ← JNI giriş noktası
│       │   └── bridge/
│       │       └── jni_utils.h/cpp       ← JNI yardımcıları
│       ├── java/com/exelite/
│       │   ├── engine/
│       │   │   ├── EngineJNI.kt          ← JNI Kotlin köprüsü
│       │   │   ├── EngineManager.kt      ← Motor yönetimi
│       │   │   ├── EngineService.kt      ← Foreground service
│       │   │   └── GameActivity.kt       ← Oyun ekranı
│       │   └── launcher/
│       │       ├── ExeLiteApp.kt         ← Application sınıfı
│       │       └── MainActivity.kt       ← Oyun seçim ekranı
│       └── assets/
│           └── runtime/                  ← Box64 + Wine binary'leri BURAYA
│               ├── bin/box64
│               ├── wine/bin/wine64
│               ├── wine/lib/
│               └── dxvk/                 ← DXVK DLL'leri
```

## Şu Anki Durum (Aşama 1 - Motor İskeleti)

✅ Tüm C++ motor sınıfları yazıldı
✅ JNI Bridge tamamlandı (Kotlin ↔ C++)  
✅ Wine prefix kurulum sistemi hazır
✅ Sanal gamepad (uinput) implementasyonu hazır
✅ Foreground service (arka plan çalışma)
✅ GameActivity (SurfaceView render)

## Sonraki Adımlar

### Zorunlu (Build için)
1. **Android Studio'yu kur** → https://developer.android.com/studio
2. **NDK'yı kur**: Android Studio → SDK Manager → NDK (Side by side)
3. Projeyi Android Studio'da aç: `File → Open → ExeLite/`

### Binary Dosyalar (Runtime)
WinLator'ın release APK'sından Box64 + Wine binary'lerini çıkar:
- APK'yı unzip et
- `lib/arm64-v8a/` içindeki `.so` dosyaları
- `assets/` içindeki wine prefix dosyaları

Bu dosyaları `app/src/main/assets/runtime/` içine koy.

### DXVK Binary'leri
- https://github.com/doitsujin/dxvk/releases adresinden Android için derlenmiş versiyonu indir
- `d3d9.dll`, `d3d11.dll`, `d3d10core.dll` → `assets/runtime/dxvk/` içine koy

## Mimari Kararlar

| Karar | Seçim | Neden |
|-------|-------|-------|
| CPU Emülasyon | Box64 | Açık kaynak, Android ARM64 destekli |
| Windows API | Wine (--no-desktop) | Explorer olmadan → %30 daha az RAM |
| DirectX | DXVK | Vulkan tabanlı, en hızlı çeviri |
| Render | SurfaceView → SDL2 | Düşük gecikme |
| Gamepad | Linux uinput (sanal cihaz) | Wine standart HID olarak algılar |
| Mimari | ARM64 only | 4GB RAM = 64-bit cihaz garantisi |
