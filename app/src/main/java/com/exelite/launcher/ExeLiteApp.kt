package com.exelite.launcher

import android.app.Application
import android.util.Log

/**
 * ExeLiteApp — Uygulama sınıfı.
 * Global başlatmalar burada yapılır.
 */
class ExeLiteApp : Application() {

    override fun onCreate() {
        super.onCreate()
        Log.i("ExeLite", "ExeLiteApp başlatıldı")
        // Gelecekte: Crashlytics, global hata handler, vs.
    }
}
