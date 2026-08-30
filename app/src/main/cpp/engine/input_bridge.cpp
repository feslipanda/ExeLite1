// ============================================================
// input_bridge.cpp — Android Dokunmatik/Gamepad → Wine Girişi
//
// Strateji:
//   Android'den gelen joystick & buton eventleri →
//   Wine'a /dev/input veya shared memory üzerinden XInput
//   mesajı olarak iletilir.
//
//   Şimdilik: uinput (Linux virtual input device) kullanılır.
//   Bu sayede Wine, gerçek bir gamepad varmış gibi görür.
// ============================================================
#include "input_bridge.h"

#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/uinput.h>
#include <cstring>
#include <cmath>

#define LOG_TAG "ExeLite.Input"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace exelite {

// uinput file descriptor (sanal gamepad + fare + klavye)
static int g_uinput_gamepad_fd = -1;
static int g_uinput_keyboard_fd = -1;
static int g_uinput_mouse_fd = -1;

// ── Yardımcı: uinput event gönder ───────────────────────────
static void uinput_write_event(int fd, uint16_t type, uint16_t code, int32_t value) {
    if (fd < 0) return;
    struct input_event ev{};
    ev.type  = type;
    ev.code  = code;
    ev.value = value;
    write(fd, &ev, sizeof(ev));
    // SYN_REPORT
    struct input_event syn{};
    syn.type  = EV_SYN;
    syn.code  = SYN_REPORT;
    syn.value = 0;
    write(fd, &syn, sizeof(syn));
}

// ── Sanal Gamepad Oluştur (uinput) ──────────────────────────
static int create_virtual_gamepad() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        LOGE("uinput açılamadı (root gerekebilir): %s", strerror(errno));
        return -1;
    }

    // Gamepad butonları aktif et
    ioctl(fd, UI_SET_EVBIT,   EV_KEY);
    ioctl(fd, UI_SET_KEYBIT,  BTN_GAMEPAD);
    ioctl(fd, UI_SET_KEYBIT,  BTN_SOUTH);   // A
    ioctl(fd, UI_SET_KEYBIT,  BTN_EAST);    // B
    ioctl(fd, UI_SET_KEYBIT,  BTN_WEST);    // X
    ioctl(fd, UI_SET_KEYBIT,  BTN_NORTH);   // Y
    ioctl(fd, UI_SET_KEYBIT,  BTN_TL);      // L1
    ioctl(fd, UI_SET_KEYBIT,  BTN_TR);      // R1
    ioctl(fd, UI_SET_KEYBIT,  BTN_SELECT);  // Select/Back
    ioctl(fd, UI_SET_KEYBIT,  BTN_START);   // Start/Menu
    ioctl(fd, UI_SET_KEYBIT,  BTN_THUMBL);  // L3
    ioctl(fd, UI_SET_KEYBIT,  BTN_THUMBR);  // R3
    ioctl(fd, UI_SET_KEYBIT,  BTN_DPAD_UP);
    ioctl(fd, UI_SET_KEYBIT,  BTN_DPAD_DOWN);
    ioctl(fd, UI_SET_KEYBIT,  BTN_DPAD_LEFT);
    ioctl(fd, UI_SET_KEYBIT,  BTN_DPAD_RIGHT);

    // Analog eksenler aktif et
    ioctl(fd, UI_SET_EVBIT,   EV_ABS);
    ioctl(fd, UI_SET_ABSBIT,  ABS_X);   // Sol joystick X
    ioctl(fd, UI_SET_ABSBIT,  ABS_Y);   // Sol joystick Y
    ioctl(fd, UI_SET_ABSBIT,  ABS_RX);  // Sağ joystick X
    ioctl(fd, UI_SET_ABSBIT,  ABS_RY);  // Sağ joystick Y
    ioctl(fd, UI_SET_ABSBIT,  ABS_Z);   // LT
    ioctl(fd, UI_SET_ABSBIT,  ABS_RZ);  // RT

    struct uinput_setup usetup{};
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x045e;  // Microsoft Xbox
    usetup.id.product = 0x028e;
    usetup.id.version = 1;
    strncpy(usetup.name, "ExeLite Virtual Xbox Controller",
            UINPUT_MAX_NAME_SIZE);

    // Eksen aralıklarını ayarla
    struct uinput_abs_setup abs{};
    auto set_abs = [&](uint16_t code) {
        abs.code = code;
        abs.absinfo.minimum = -32768;
        abs.absinfo.maximum =  32767;
        abs.absinfo.flat    =  1000;
        abs.absinfo.fuzz    =  16;
        ioctl(fd, UI_ABS_SETUP, &abs);
    };
    set_abs(ABS_X); set_abs(ABS_Y);
    set_abs(ABS_RX); set_abs(ABS_RY);

    abs.absinfo.minimum = 0;
    abs.absinfo.flat    = 0;
    set_abs(ABS_Z); set_abs(ABS_RZ);

    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);

    LOGI("create_virtual_gamepad: Sanal gamepad oluşturuldu.");
    return fd;
}

// ── Singleton ───────────────────────────────────────────────
InputBridge& InputBridge::instance() {
    static InputBridge inst;
    return inst;
}

void InputBridge::initialize() {
    if (initialized_) return;
    g_uinput_gamepad_fd  = create_virtual_gamepad();
    initialized_ = (g_uinput_gamepad_fd >= 0);
    if (!initialized_) {
        LOGE("initialize: uinput başlatılamadı! Giriş çalışmayabilir.");
    }
}

void InputBridge::destroy() {
    if (g_uinput_gamepad_fd >= 0) {
        ioctl(g_uinput_gamepad_fd, UI_DEV_DESTROY);
        close(g_uinput_gamepad_fd);
        g_uinput_gamepad_fd = -1;
    }
    initialized_ = false;
}

// ── Gamepad Buton ────────────────────────────────────────────
// btn: 0=A, 1=B, 2=X, 3=Y, 4=L1, 5=R1, 6=Start, 7=Select
//      8=L3, 9=R3, 10=DUp, 11=DDown, 12=DLeft, 13=DRight
static const uint16_t BTN_MAP[] = {
    BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH,
    BTN_TL, BTN_TR, BTN_START, BTN_SELECT,
    BTN_THUMBL, BTN_THUMBR,
    BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT
};
static constexpr int BTN_MAP_SIZE = sizeof(BTN_MAP) / sizeof(BTN_MAP[0]);

void InputBridge::sendButton(int32_t btn, bool pressed) {
    if (btn < 0 || btn >= BTN_MAP_SIZE) return;
    uinput_write_event(g_uinput_gamepad_fd, EV_KEY, BTN_MAP[btn], pressed ? 1 : 0);
}

// ── Analog Eksen ─────────────────────────────────────────────
// value: -1.0 → +1.0 arasında normalize edilmiş değer
void InputBridge::sendAxis(AxisCode axis, float value) {
    int32_t raw = (int32_t)(value * 32767.0f);
    uint16_t abs_code;
    switch (axis) {
        case AxisCode::LEFT_X:  abs_code = ABS_X;  break;
        case AxisCode::LEFT_Y:  abs_code = ABS_Y;  break;
        case AxisCode::RIGHT_X: abs_code = ABS_RX; break;
        case AxisCode::RIGHT_Y: abs_code = ABS_RY; break;
        case AxisCode::LT:
            abs_code = ABS_Z;
            raw = (int32_t)((value + 1.0f) * 0.5f * 32767.0f);
            break;
        case AxisCode::RT:
            abs_code = ABS_RZ;
            raw = (int32_t)((value + 1.0f) * 0.5f * 32767.0f);
            break;
        default: return;
    }
    uinput_write_event(g_uinput_gamepad_fd, EV_ABS, abs_code, raw);
}

// ── Klavye ───────────────────────────────────────────────────
void InputBridge::sendKey(int32_t key_code, bool pressed) {
    // Şimdilik log (klavye uinput ayrıca implement edilecek)
    LOGI("sendKey: key=%d pressed=%d", key_code, pressed);
}

// ── Mouse ────────────────────────────────────────────────────
void InputBridge::sendMouseMove(float dx, float dy) {
    LOGI("sendMouseMove: dx=%.2f dy=%.2f", dx, dy);
}

void InputBridge::sendMouseButton(int32_t btn, bool pressed) {
    LOGI("sendMouseButton: btn=%d pressed=%d", btn, pressed);
}

// (writeXInputState, writeKeyEvent, writeMouseEvent stub'lar)
void InputBridge::writeXInputState()  {}
void InputBridge::writeKeyEvent(int32_t, bool) {}
void InputBridge::writeMouseEvent(float, float, int32_t, bool) {}

} // namespace exelite
