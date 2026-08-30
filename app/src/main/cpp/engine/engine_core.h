// ============================================================
// engine_core.h — ExeLite Motor Ana Başlığı
// ============================================================
#pragma once

#include "engine_types.h"
#include <functional>
#include <atomic>
#include <thread>
#include <memory>

namespace exelite {

// Durum değişikliği callback tipi (JNI'ye gönderilir)
using StateCallback  = std::function<void(EngineState, EngineError, const char*)>;
using StatusCallback = std::function<void(const EngineStatus&)>;

class EngineCore {
public:
    static EngineCore& instance();

    // ── Yaşam Döngüsü ───────────────────────────────────────
    EngineError initialize(const LaunchConfig& config);
    EngineError start();
    void        pause();
    void        resume();
    void        stop();
    void        destroy();

    // ── Durum Sorgulama ─────────────────────────────────────
    EngineState  getState()  const { return state_.load(); }
    EngineStatus getStatus() const;

    // ── Giriş Enjeksiyonu ───────────────────────────────────
    void injectKey      (int32_t key_code, bool pressed);
    void injectMouseMove(float dx, float dy);
    void injectMouseBtn (int32_t btn, bool pressed);
    void injectAxis     (AxisCode axis, float value);
    void injectButton   (int32_t btn, bool pressed);

    // ── Callback Kayıt ──────────────────────────────────────
    void setStateCallback (StateCallback  cb) { state_cb_  = std::move(cb); }
    void setStatusCallback(StatusCallback cb) { status_cb_ = std::move(cb); }

private:
    EngineCore() = default;
    ~EngineCore() { destroy(); }
    EngineCore(const EngineCore&) = delete;
    EngineCore& operator=(const EngineCore&) = delete;

    void notifyState(EngineState s, EngineError e = EngineError::NONE,
                     const char* msg = "");
    void monitorLoop(); // Arka plan izleme thread'i

    std::atomic<EngineState> state_  { EngineState::IDLE };
    LaunchConfig             config_;
    EngineStatus             status_ {};

    StateCallback            state_cb_;
    StatusCallback           status_cb_;
    std::thread              monitor_thread_;
    std::atomic<bool>        monitor_running_ { false };

    int32_t                  wine_pid_  { -1 };
    int32_t                  box64_pid_ { -1 };
};

} // namespace exelite
