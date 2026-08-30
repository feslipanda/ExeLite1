// ============================================================
// engine_core.cpp — ExeLite Motor Ana İmplementasyonu
// Box64 + Wine process yönetimi
// ============================================================
#include "engine_core.h"
#include "process_manager.h"
#include "wine_config.h"
#include "render_bridge.h"
#include "input_bridge.h"

#include <android/log.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fstream>
#include <chrono>

#define LOG_TAG "ExeLite"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

namespace exelite {

// ── Singleton ───────────────────────────────────────────────
EngineCore& EngineCore::instance() {
    static EngineCore inst;
    return inst;
}

// ── Başlatma ────────────────────────────────────────────────
EngineError EngineCore::initialize(const LaunchConfig& config) {
    if (state_.load() != EngineState::IDLE) {
        LOGE("initialize: Engine zaten çalışıyor!");
        return EngineError::NONE;
    }

    config_ = config;
    notifyState(EngineState::INITIALIZING);
    LOGI("initialize: exe=%s dir=%s", config.exe_path.c_str(), config.working_dir.c_str());

    // 1) .exe dosyası var mı kontrol et
    if (access(config.exe_path.c_str(), F_OK) != 0) {
        LOGE("initialize: .exe bulunamadı: %s", config.exe_path.c_str());
        notifyState(EngineState::ERROR, EngineError::EXE_NOT_FOUND, "EXE dosyası bulunamadı");
        return EngineError::EXE_NOT_FOUND;
    }

    // 2) Wine prefix hazırla
    EngineError wine_err = WineConfig::instance().setupPrefix(
        config.wine_prefix, config.dxvk_enabled
    );
    if (wine_err != EngineError::NONE) {
        notifyState(EngineState::ERROR, wine_err, "Wine prefix kurulamadı");
        return wine_err;
    }

    // 3) Render katmanını başlat (opsiyonel — ana render Java GLRenderer'dır)
    // C++ RenderBridge sadece SHM framebuffer modu için kullanılır.
    // Başarısız olursa devam et, Java tarafı render'ı halledecek.
    EngineError render_err = RenderBridge::instance().initialize(
        config.render_width, config.render_height
    );
    if (render_err != EngineError::NONE) {
        LOGI("initialize: C++ RenderBridge başlatılamadı (opsiyonel, devam ediliyor)");
    }

    // 4) Input bridge'i hazırla
    InputBridge::instance().initialize();

    LOGI("initialize: Tüm bileşenler hazır.");
    return EngineError::NONE;
}

// ── Başlat ──────────────────────────────────────────────────
EngineError EngineCore::start() {
    if (state_.load() != EngineState::INITIALIZING) {
        return EngineError::NONE;
    }

    // Process Manager ile Box64 + Wine üzerinden .exe başlat
    auto result = ProcessManager::instance().launchExe(
        config_.exe_path,
        config_.working_dir,
        config_.internal_files_dir,
        config_.wine_prefix,
        config_.max_memory_mb
    );

    if (!result.success) {
        notifyState(EngineState::ERROR, EngineError::PROCESS_CRASH, result.error_msg.c_str());
        return EngineError::PROCESS_CRASH;
    }

    wine_pid_  = result.wine_pid;
    box64_pid_ = result.box64_pid;

    notifyState(EngineState::RUNNING);
    LOGI("start: Oyun başlatıldı — wine_pid=%d box64_pid=%d",
         wine_pid_, box64_pid_);

    // Arka plan izleme thread'ini başlat
    monitor_running_ = true;
    monitor_thread_  = std::thread(&EngineCore::monitorLoop, this);

    return EngineError::NONE;
}

// ── Duraklat ────────────────────────────────────────────────
void EngineCore::pause() {
    if (state_.load() != EngineState::RUNNING) return;
    if (wine_pid_ > 0) {
        kill(wine_pid_, SIGSTOP);  // Wine process'i dondur
        LOGI("pause: SIGSTOP → wine_pid=%d", wine_pid_);
    }
    notifyState(EngineState::PAUSED);
}

// ── Devam Et ────────────────────────────────────────────────
void EngineCore::resume() {
    if (state_.load() != EngineState::PAUSED) return;
    if (wine_pid_ > 0) {
        kill(wine_pid_, SIGCONT);  // Wine process'i devam ettir
        LOGI("resume: SIGCONT → wine_pid=%d", wine_pid_);
    }
    notifyState(EngineState::RUNNING);
}

// ── Durdur ──────────────────────────────────────────────────
void EngineCore::stop() {
    EngineState cur = state_.load();
    if (cur == EngineState::IDLE || cur == EngineState::STOPPING) return;

    notifyState(EngineState::STOPPING);
    monitor_running_ = false;

    ProcessManager::instance().kill();

    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }

    RenderBridge::instance().destroy();
    InputBridge::instance().destroy();

    wine_pid_  = -1;
    box64_pid_ = -1;

    notifyState(EngineState::IDLE);
    LOGI("stop: Engine durduruldu.");
}

// ── Temizle ─────────────────────────────────────────────────
void EngineCore::destroy() {
    stop();
}

// ── İzleme Loop'u ───────────────────────────────────────────
// Arka planda oyunun hâlâ çalışıp çalışmadığını kontrol eder
void EngineCore::monitorLoop() {
    LOGD("monitorLoop: Başladı.");

    while (monitor_running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (!ProcessManager::instance().isRunning()) {
            if (state_.load() == EngineState::RUNNING) {
                LOGI("monitorLoop: Wine process sonlandı, oyun kapandı.");
                notifyState(EngineState::IDLE);
                monitor_running_ = false;
            }
            break;
        }

        // RAM / FPS durumunu güncelle
        status_.fps           = RenderBridge::instance().getCurrentFps();
        status_.used_memory_mb = ProcessManager::instance().getMemoryUsageMb();
        status_.wine_pid      = wine_pid_;
        status_.box64_pid     = box64_pid_;
        status_.state         = state_.load();

        if (status_cb_) {
            status_cb_(status_);
        }
    }

    LOGD("monitorLoop: Bitti.");
}

// ── Durum Bildir ─────────────────────────────────────────────
void EngineCore::notifyState(EngineState s, EngineError e, const char* msg) {
    state_.store(s);
    status_.state      = s;
    status_.last_error = e;
    if (msg) {
        strncpy(status_.error_message, msg, sizeof(status_.error_message) - 1);
    }
    if (state_cb_) {
        state_cb_(s, e, msg);
    }
}

EngineStatus EngineCore::getStatus() const {
    return status_;
}

// ── Giriş Fonksiyonları ──────────────────────────────────────
void EngineCore::injectKey(int32_t key_code, bool pressed) {
    InputBridge::instance().sendKey(key_code, pressed);
}

void EngineCore::injectMouseMove(float dx, float dy) {
    InputBridge::instance().sendMouseMove(dx, dy);
}

void EngineCore::injectMouseBtn(int32_t btn, bool pressed) {
    InputBridge::instance().sendMouseButton(btn, pressed);
}

void EngineCore::injectAxis(AxisCode axis, float value) {
    InputBridge::instance().sendAxis(axis, value);
}

void EngineCore::injectButton(int32_t btn, bool pressed) {
    InputBridge::instance().sendButton(btn, pressed);
}

} // namespace exelite
