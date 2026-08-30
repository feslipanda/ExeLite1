package com.exelite.engine

import android.content.ComponentName
import android.content.Intent
import android.content.ServiceConnection
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.util.Log
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.FrameLayout
import androidx.activity.OnBackPressedCallback
import androidx.appcompat.app.AppCompatActivity
import com.exelite.launcher.GamepadOverlay
import com.winlator.widget.XServerView
import com.winlator.xserver.XServer
import com.winlator.xserver.ScreenInfo

private const val TAG = "GameActivity"

class GameActivity : AppCompatActivity() {

    companion object {
        const val EXTRA_EXE_PATH   = "exe_path"
        const val EXTRA_GAME_DIR   = "game_dir"
        const val EXTRA_DXVK       = "dxvk_enabled"
        const val EXTRA_WIDTH      = "render_width"
        const val EXTRA_HEIGHT     = "render_height"
        const val EXTRA_SETUP_MODE = "setup_mode"  // true → installer/setup EXE
    }

    private lateinit var xServerView: XServerView
    private lateinit var xServer: XServer
    private lateinit var gamepadOverlay: GamepadOverlay
    private var engineService: EngineService? = null
    private var isBound = false
    private var isSetupMode = false   // setup tamamlanınca RESULT_OK döndür

    private var isServiceReady  = false

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            val localBinder = binder as? EngineService.LocalBinder ?: return
            engineService = localBinder.getService()
            isBound = true
            
            // Gamepad Overlay'e engine referansını ver
            gamepadOverlay.engineManager = engineService?.engineManager
            
            Log.i(TAG, "EngineService bağlandı.")
            isServiceReady = true
            tryStartGame()
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            engineService = null
            isBound = false
            isServiceReady = false
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val width = intent.getIntExtra(EXTRA_WIDTH, 1280)
        val height = intent.getIntExtra(EXTRA_HEIGHT, 720)

        // 1. Render Surface (XServer)
        val screenInfo = ScreenInfo(width, height)
        xServer = XServer(this, screenInfo)
        xServerView = XServerView(this, xServer)

        // XServer <-> GLRenderer
        xServer.renderer = xServerView.renderer

        // 2. Gamepad Overlay
        gamepadOverlay = GamepadOverlay(this)
        gamepadOverlay.xServer = xServer

        val layout = FrameLayout(this)
        layout.addView(xServerView, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT,
            FrameLayout.LayoutParams.MATCH_PARENT
        ))
        layout.addView(gamepadOverlay, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT,
            FrameLayout.LayoutParams.MATCH_PARENT
        ))

        setContentView(layout)

        // Tam ekran immersive mod — setContentView'dan SONRA (DecorView attach edilmeden insetsController null olur)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.insetsController?.let { controller ->
                controller.hide(WindowInsets.Type.systemBars())
                controller.systemBarsBehavior =
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (
                android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
                or android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            )
        }

        // Android 8+ için: önce startForegroundService, ardından bind
        val serviceIntent = Intent(this, EngineService::class.java)
        try {
            startForegroundService(serviceIntent)
            bindService(serviceIntent, serviceConnection, BIND_AUTO_CREATE)
            Log.i(TAG, "startForegroundService + bindService çağrıldı.")
        } catch (e: Exception) {
            Log.e(TAG, "Servis bağlanırken hata: ${e.message}")
        }

        // API 33+ için OnBackPressedCallback
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                engineService?.engineManager?.stopGame()
                finish()
            }
        })
    }

    private fun startGame() {
        val exePath   = intent.getStringExtra(EXTRA_EXE_PATH)   ?: return
        val gameDir   = intent.getStringExtra(EXTRA_GAME_DIR)   ?: exePath.substringBeforeLast("/")
        val dxvk      = intent.getBooleanExtra(EXTRA_DXVK,      true)
        val width     = intent.getIntExtra    (EXTRA_WIDTH,      1280)
        val height    = intent.getIntExtra    (EXTRA_HEIGHT,     720)
        isSetupMode   = intent.getBooleanExtra(EXTRA_SETUP_MODE, false)

        val manager = engineService?.engineManager ?: return
        
        manager.startXServer(xServer)

        manager.onStateChanged  = { state -> onEngineStateChanged(state) }
        manager.onErrorOccurred = { err, msg -> onEngineError(err, msg) }
        manager.onStatusUpdate  = { fps, mem ->
            engineService?.updateNotification(
                if (isSetupMode) "Kurulum devam ediyor…"
                else "FPS: $fps | RAM: ${mem}MB"
            )
        }

        if (isSetupMode) {
            gamepadOverlay.inputMode = GamepadOverlay.InputMode.MOUSE
            Log.i(TAG, "Setup modu: installer çalıştırılıyor → $exePath")
            manager.launchGame(exePath, gameDir, dxvk = false, width, height)
        } else {
            gamepadOverlay.inputMode = GamepadOverlay.InputMode.GAMEPAD
            manager.launchGame(exePath, gameDir, dxvk, width, height)
        }
    }

    /**
     * EngineService hazır olunca oyunu başlat.
     */
    private fun tryStartGame() {
        if (isServiceReady) {
            Log.i(TAG, "tryStartGame: Service hazır, oyun başlatılıyor.")
            startGame()
        }
    }

    private fun onEngineStateChanged(state: EngineState) {
        runOnUiThread {
            when {
                // Setup modu tamamlandı (IDLE) → RESULT_OK ile geri dön
                isSetupMode && state == EngineState.IDLE -> {
                    Log.i(TAG, "Setup tamamlandı — RESULT_OK döndürülüyor")
                    setResult(RESULT_OK)
                    finish()
                }
                // Setup hataya düştü → RESULT_CANCELED
                isSetupMode && state == EngineState.ERROR -> {
                    Log.w(TAG, "Setup hata — RESULT_CANCELED")
                    setResult(RESULT_CANCELED)
                    finish()
                }
                // Normal oyun modu kapandı
                !isSetupMode && (state == EngineState.IDLE || state == EngineState.ERROR) -> {
                    finish()
                }
                else -> { /* devam */ }
            }
        }
    }

    private fun onEngineError(error: EngineError, message: String) {
        runOnUiThread {
            Log.e(TAG, "onEngineError: $error — $message")
            finish()
        }
    }

    override fun onPause() {
        super.onPause()
        // onPause sırasında SIGSTOP gönderilmemeli, aksi takdirde Wine TCP ve çizim iş parçacıkları kilitlenir.
    }

    override fun onResume() {
        super.onResume()
    }

    override fun onDestroy() {
        if (isBound) {
            unbindService(serviceConnection)
            isBound = false
        }
        // Servisi de durdur (foreground notification temizlensin)
        val serviceIntent = Intent(this, EngineService::class.java)
        stopService(serviceIntent)
        super.onDestroy()
    }
}
