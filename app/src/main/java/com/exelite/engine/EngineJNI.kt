package com.exelite.engine

/**
 * EngineJNI — C++ exelite_engine kütüphanesine Kotlin köprüsü.
 *
 * Tüm "external fun"lar, exelite_jni.cpp içinde implement edilmiştir.
 * JNI naming convention: Java_com_exelite_engine_EngineJNI_native<FuncName>
 */
object EngineJNI {

    init {
        System.loadLibrary("exelite_engine")
    }

    // ── Yaşam Döngüsü ────────────────────────────────────────

    /**
     * Motoru başlatır, Wine prefix + DXVK hazırlar.
     * @return EngineError kodu (0 = başarı)
     */
    external fun nativeInitialize(
        exePath:      String,
        workingDir:   String,
        internalFilesDir: String,
        winePrefix:   String,
        renderWidth:  Int,
        renderHeight: Int,
        dxvkEnabled:  Boolean
    ): Int

    /** Oyunu çalıştırır (fork/exec Box64 + Wine) */
    external fun nativeStart(): Int

    /** Oyunu durdurur (SIGTERM → SIGKILL) */
    external fun nativeStop()

    /** Oyunu dondurur (SIGSTOP) */
    external fun nativePause()

    /** Oyunu devam ettirir (SIGCONT) */
    external fun nativeResume()

    // ── Render Surface ───────────────────────────────────────

    /**
     * SurfaceView'ın Surface'ini C++ tarafına iletir.
     * Surface = null → pencere kapatılmış (onSurfaceDestroyed)
     */
    external fun nativeSetSurface(surface: android.view.Surface?)

    // ── Giriş Fonksiyonları ──────────────────────────────────

    /** Joystick ekseni: axisCode=[0..5], value=[-1..+1] */
    external fun nativeSendAxis(axisCode: Int, value: Float)

    /** Gamepad butonu: btn=[0..13], pressed=true/false */
    external fun nativeSendButton(btn: Int, pressed: Boolean)

    /** Klavye tuşu: keyCode=Linux keycode, pressed=true/false */
    external fun nativeSendKey(keyCode: Int, pressed: Boolean)

    /** Mouse hareketi: delta piksel */
    external fun nativeSendMouseMove(dx: Float, dy: Float)

    /** Mouse butonu: 0=Sol, 1=Sağ, 2=Orta */
    external fun nativeSendMouseButton(btn: Int, pressed: Boolean)

    // ── Durum Sorgulama ──────────────────────────────────────

    /**
     * Motor durumu: EngineState enum int değeri
     * 0=IDLE, 1=INITIALIZING, 2=RUNNING, 3=PAUSED, 4=STOPPING, 5=ERROR
     */
    external fun nativeGetState(): Int

    /**
     * Durum dizisi: [state, fps, memory_mb, wine_pid, last_error]
     */
    external fun nativeGetStatus(): IntArray
}

// ── Durum Enum'ları (C++ engine_types.h ile senkron) ─────────

enum class EngineState(val code: Int) {
    IDLE         (0),
    INITIALIZING (1),
    RUNNING      (2),
    PAUSED       (3),
    STOPPING     (4),
    ERROR        (5);

    companion object {
        fun fromCode(code: Int) = values().firstOrNull { it.code == code } ?: IDLE
    }
}

enum class EngineError(val code: Int) {
    NONE              (0),
    EXE_NOT_FOUND     (-1),
    BOX64_INIT_FAIL   (-2),
    WINE_INIT_FAIL    (-3),
    RENDER_INIT_FAIL  (-4),
    PERMISSION_DENIED (-5),
    PROCESS_CRASH     (-6),
    OUT_OF_MEMORY     (-7);

    companion object {
        fun fromCode(code: Int) = values().firstOrNull { it.code == code } ?: NONE
    }
}

// Gamepad eksen kodları (C++ AxisCode enum ile senkron)
object AxisCode {
    const val LEFT_X  = 0
    const val LEFT_Y  = 1
    const val RIGHT_X = 2
    const val RIGHT_Y = 3
    const val LT      = 4
    const val RT      = 5
}

// Gamepad buton kodları
object GamepadButton {
    const val A          = 0
    const val B          = 1
    const val X          = 2
    const val Y          = 3
    const val L1         = 4
    const val R1         = 5
    const val START      = 6
    const val SELECT     = 7
    const val L3         = 8
    const val R3         = 9
    const val DPAD_UP    = 10
    const val DPAD_DOWN  = 11
    const val DPAD_LEFT  = 12
    const val DPAD_RIGHT = 13
}
