// ============================================================
// exelite_jni.cpp — Ana JNI Giriş Noktası
//
// Java/Kotlin tarafından System.loadLibrary("exelite_engine")
// ile yüklenir. Kotlin'deki "external fun" fonksiyonlarının
// C++ implementasyonu burada bulunur.
//
// Naming convention:
//   Java_<paket_noktasız>_<sınıf>_<fonksiyon>
// ============================================================
#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>

#include "../engine/engine_core.h"
#include "../engine/engine_types.h"
#include "../engine/render_bridge.h"
#include "../bridge/jni_utils.h"

#define LOG_TAG "ExeLite.JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace exelite;

// ── Global JVM Referansı ─────────────────────────────────────
static JavaVM*   g_jvm          = nullptr;
static jclass    g_callback_cls = nullptr;
static jobject   g_callback_obj = nullptr;

// ── JNI_OnLoad ───────────────────────────────────────────────
JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    LOGI("JNI_OnLoad: exelite_engine yüklendi.");
    return JNI_VERSION_1_6;
}

// ============================================================
// 1) ENGINE INIT — Başlatma
// ============================================================
extern "C"
JNIEXPORT jint JNICALL
Java_com_exelite_engine_EngineJNI_nativeInitialize(
        JNIEnv* env,
        jobject thiz,
        jstring exe_path,
        jstring working_dir,
        jstring internal_files_dir,
        jstring wine_prefix,
        jint    render_width,
        jint    render_height,
        jboolean dxvk_enabled)
{
    LaunchConfig cfg;
    cfg.exe_path      = jni_utils::jstring_to_str(env, exe_path);
    cfg.working_dir   = jni_utils::jstring_to_str(env, working_dir);
    cfg.internal_files_dir = jni_utils::jstring_to_str(env, internal_files_dir);
    cfg.wine_prefix   = jni_utils::jstring_to_str(env, wine_prefix);
    cfg.render_width  = (int32_t)render_width;
    cfg.render_height = (int32_t)render_height;
    cfg.dxvk_enabled  = (bool)dxvk_enabled;

    LOGI("nativeInitialize: exe=%s", cfg.exe_path.c_str());

    EngineError err = EngineCore::instance().initialize(cfg);
    return (jint)err;
}

// ============================================================
// 2) ENGINE START — Oyunu Başlat
// ============================================================
extern "C"
JNIEXPORT jint JNICALL
Java_com_exelite_engine_EngineJNI_nativeStart(JNIEnv* env, jobject thiz)
{
    EngineError err = EngineCore::instance().start();
    LOGI("nativeStart: result=%d", (int)err);
    return (jint)err;
}

// ============================================================
// 3) ENGINE STOP — Durdur
// ============================================================
extern "C"
JNIEXPORT void JNICALL
Java_com_exelite_engine_EngineJNI_nativeStop(JNIEnv* env, jobject thiz)
{
    LOGI("nativeStop");
    EngineCore::instance().stop();
}

// ============================================================
// 4) ENGINE PAUSE / RESUME
// ============================================================
extern "C"
JNIEXPORT void JNICALL
Java_com_exelite_engine_EngineJNI_nativePause(JNIEnv* env, jobject thiz)
{
    EngineCore::instance().pause();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_exelite_engine_EngineJNI_nativeResume(JNIEnv* env, jobject thiz)
{
    EngineCore::instance().resume();
}

// ============================================================
// 5) SURFACE ATTACH — SurfaceView penceresi bağla
// ============================================================
extern "C"
JNIEXPORT void JNICALL
Java_com_exelite_engine_EngineJNI_nativeSetSurface(
        JNIEnv* env, jobject thiz, jobject surface)
{
    ANativeWindow* window = nullptr;
    if (surface != nullptr) {
        window = ANativeWindow_fromSurface(env, surface);
    }
    RenderBridge::instance().setWindow(window);
    LOGI("nativeSetSurface: window=%p", window);
}

// ============================================================
// 6) GAMEPAD INPUT — Joystick eksen
// ============================================================
extern "C"
JNIEXPORT void JNICALL
Java_com_exelite_engine_EngineJNI_nativeSendAxis(
        JNIEnv* env, jobject thiz,
        jint axis_code, jfloat value)
{
    EngineCore::instance().injectAxis((AxisCode)axis_code, value);
}

// ============================================================
// 7) GAMEPAD INPUT — Buton
// ============================================================
extern "C"
JNIEXPORT void JNICALL
Java_com_exelite_engine_EngineJNI_nativeSendButton(
        JNIEnv* env, jobject thiz,
        jint btn, jboolean pressed)
{
    EngineCore::instance().injectButton(btn, pressed);
}

// ============================================================
// 8) KLAVYE INPUT
// ============================================================
extern "C"
JNIEXPORT void JNICALL
Java_com_exelite_engine_EngineJNI_nativeSendKey(
        JNIEnv* env, jobject thiz,
        jint key_code, jboolean pressed)
{
    EngineCore::instance().injectKey(key_code, pressed);
}

// ============================================================
// 9) MOUSE INPUT
// ============================================================
extern "C"
JNIEXPORT void JNICALL
Java_com_exelite_engine_EngineJNI_nativeSendMouseMove(
        JNIEnv* env, jobject thiz,
        jfloat dx, jfloat dy)
{
    EngineCore::instance().injectMouseMove(dx, dy);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_exelite_engine_EngineJNI_nativeSendMouseButton(
        JNIEnv* env, jobject thiz,
        jint btn, jboolean pressed)
{
    EngineCore::instance().injectMouseBtn(btn, pressed);
}

// ============================================================
// 10) DURUM SORGULAMA
// ============================================================
extern "C"
JNIEXPORT jint JNICALL
Java_com_exelite_engine_EngineJNI_nativeGetState(JNIEnv* env, jobject thiz)
{
    return (jint)EngineCore::instance().getState();
}

extern "C"
JNIEXPORT jintArray JNICALL
Java_com_exelite_engine_EngineJNI_nativeGetStatus(JNIEnv* env, jobject thiz)
{
    // [0]=state, [1]=fps, [2]=memory_mb, [3]=wine_pid, [4]=last_error
    EngineStatus st = EngineCore::instance().getStatus();
    jint mem_mb = (st.used_memory_mb > INT32_MAX) ? INT32_MAX : (jint)st.used_memory_mb;
    jint data[5] = {
        (jint)st.state,
        st.fps,
        mem_mb,
        st.wine_pid,
        (jint)st.last_error
    };
    jintArray arr = env->NewIntArray(5);
    env->SetIntArrayRegion(arr, 0, 5, data);
    return arr;
}
