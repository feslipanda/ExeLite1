// ============================================================
// engine_types.h — ExeLite Motor Tipleri ve Sabitleri
// ============================================================
#pragma once

#include <cstdint>
#include <string>

namespace exelite {

// ── Engine Durumları ────────────────────────────────────────
enum class EngineState : int32_t {
    IDLE        = 0,   // Hiçbir şey çalışmıyor
    INITIALIZING = 1,  // Box64/Wine başlatılıyor
    RUNNING     = 2,   // Oyun çalışıyor
    PAUSED      = 3,   // Askıya alındı
    STOPPING    = 4,   // Durduruluyor
    ERROR       = 5    // Hata oluştu
};

// ── Hata Kodları ────────────────────────────────────────────
enum class EngineError : int32_t {
    NONE             = 0,
    EXE_NOT_FOUND    = -1,
    BOX64_INIT_FAIL  = -2,
    WINE_INIT_FAIL   = -3,
    RENDER_INIT_FAIL = -4,
    PERMISSION_DENIED= -5,
    PROCESS_CRASH    = -6,
    OUT_OF_MEMORY    = -7
};

// ── Başlatma Konfigürasyonu ──────────────────────────────────
struct LaunchConfig {
    std::string exe_path;          // /storage/.../Game/game.exe
    std::string working_dir;       // /storage/.../Game/
    std::string internal_files_dir; // /data/user/0/com.exelite.launcher/files
    std::string wine_prefix;       // /data/data/.../wine_prefix/
    
    int32_t     render_width  = 1280;   // Render çözünürlüğü
    int32_t     render_height = 720;
    bool        dxvk_enabled  = true;  // DirectX → Vulkan
    bool        esync_enabled = false; // Eventfd sync (hafıza tasarrufu için kapalı)
    
    // RAM kısıtlama — wine'ın aldığı max RAM
    int64_t     max_memory_mb = 1536; // 1.5 GB wine için
    
    // Performans modu
    bool        low_latency   = true;
    int32_t     cpu_cores     = 2;    // Box64 için ayrılan çekirdek sayısı
};

// ── Giriş Tipleri (Gamepad → DirectInput) ───────────────────
enum class InputType : int32_t {
    KEY_DOWN       = 1,
    KEY_UP         = 2,
    MOUSE_MOVE     = 3,
    MOUSE_BUTTON   = 4,
    JOYSTICK_AXIS  = 5,
    JOYSTICK_BTN   = 6
};

// ── Joystick Eksen Kodları ──────────────────────────────────
enum class AxisCode : int32_t {
    LEFT_X  = 0,
    LEFT_Y  = 1,
    RIGHT_X = 2,
    RIGHT_Y = 3,
    LT      = 4,  // Sol tetik
    RT      = 5   // Sağ tetik
};

// ── Motor Durum Raporu ──────────────────────────────────────
struct EngineStatus {
    EngineState state;
    EngineError last_error;
    int32_t     fps;
    int64_t     used_memory_mb;
    int32_t     wine_pid;       // Wine process ID
    int32_t     box64_pid;      // Box64 process ID
    char        error_message[256];
};

} // namespace exelite
