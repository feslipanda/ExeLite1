package com.exelite.engine

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.os.Binder
import android.os.IBinder
import android.util.Log
import com.exelite.launcher.MainActivity
import com.exelite.launcher.R

private const val TAG     = "EngineService"
private const val CHANNEL = "exelite_engine"
private const val NOTIF_ID = 1001

/**
 * EngineService — Foreground Service.
 *
 * Oyun, Activity'nin lifecycle'ından bağımsız olarak
 * arka planda (foreground service olarak) çalışmaya devam eder.
 * Bildirim çubuğunda "ExeLite çalışıyor" bildirimi gösterir.
 */
class EngineService : Service() {

    inner class LocalBinder : Binder() {
        fun getService(): EngineService = this@EngineService
    }

    private val binder = LocalBinder()
    val engineManager  by lazy { EngineManager(this) }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        Log.i(TAG, "onCreate")
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        startForeground(NOTIF_ID, buildNotification("Oyun başlatılıyor..."))
        return START_STICKY
    }

    fun updateNotification(text: String) {
        val nm = getSystemService(NotificationManager::class.java)
        nm.notify(NOTIF_ID, buildNotification(text))
    }

    override fun onDestroy() {
        engineManager.destroy()
        Log.i(TAG, "onDestroy")
        super.onDestroy()
    }

    private fun buildNotification(text: String): Notification {
        val intent = Intent(this, MainActivity::class.java)
        val pi = PendingIntent.getActivity(this, 0, intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

        return Notification.Builder(this, CHANNEL)
            .setContentTitle("ExeLite")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentIntent(pi)
            .setOngoing(true)
            .build()
    }

    private fun createNotificationChannel() {
        val ch = NotificationChannel(
            CHANNEL,
            "ExeLite Engine",
            NotificationManager.IMPORTANCE_LOW
        ).apply { description = "Windows oyun motoru çalışıyor" }

        getSystemService(NotificationManager::class.java)
            .createNotificationChannel(ch)
    }
}
