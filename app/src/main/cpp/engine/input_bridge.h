// ============================================================
// input_bridge.h / input_bridge.cpp — Gamepad → Wine Giriş
// ============================================================
#pragma once
#include "engine_types.h"

namespace exelite {

class InputBridge {
public:
    static InputBridge& instance();
    void initialize();
    void destroy();

    void sendKey        (int32_t key_code, bool pressed);
    void sendMouseMove  (float dx, float dy);
    void sendMouseButton(int32_t btn, bool pressed);
    void sendAxis       (AxisCode axis, float value);
    void sendButton     (int32_t btn, bool pressed);

private:
    InputBridge() = default;
    bool initialized_ { false };

    // XInput state (Xbox gamepad emülasyonu)
    float axis_state_[6] { 0.0f };
    bool  btn_state_[16] { false };

    void writeXInputState();
    void writeKeyEvent(int32_t key_code, bool pressed);
    void writeMouseEvent(float dx, float dy, int32_t btn, bool pressed);
};

} // namespace exelite
