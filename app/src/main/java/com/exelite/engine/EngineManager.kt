package com.exelite.engine

import android.app.Activity
import android.content.Context
import android.util.Log
import kotlinx.coroutines.*
import java.io.File
import com.winlator.xconnector.UnixSocketConfig
import com.winlator.xconnector.XConnectorEpoll
import com.winlator.xserver.XClientConnectionHandler
import com.winlator.xserver.XClientRequestHandler
import com.winlator.xserver.XServer
import com.winlator.sysvshm.SysVSHMConnectionHandler
import com.winlator.sysvshm.SysVSHMRequestHandler
import com.winlator.sysvshm.SysVSharedMemory
import com.winlator.xserver.SHMSegmentManager

private const val TAG = "EngineManager"

/**
 * EngineManager — Motor'un Kotlin tarafındaki yöneticisi.
 *
 * Sorumluluklar:
 * - Assets'teki Box64/Wine binary'lerini dahili depolama alanına kopyalar
 * - JNI üzerinden initialize → start → stop akışını yönetir
 * - Durum değişikliklerini callback aracılığıyla Activity'e iletir
 */
class EngineManager(private val context: Context) {

    // ── Callback'ler (Activity'den set edilir) ───────────────
    var onStateChanged:  ((EngineState)  -> Unit)? = null
    var onErrorOccurred: ((EngineError, String) -> Unit)? = null
    var onStatusUpdate:  ((fps: Int, memMb: Int) -> Unit)? = null
    var onInstallProgress: ((Int) -> Unit)? = null

    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private var statusJob: Job? = null

    // XServer dependencies
    private var xConnector: XConnectorEpoll? = null
    private var shmConnector: XConnectorEpoll? = null
    private var x11TcpProxy: X11TcpProxy? = null

    // Dahili binary yolları - Kök dizindeki root izinli klasör çakışmalarını önlemek için engine_v2 kullanıldı
    private val internalFilesDir get() = context.filesDir.absolutePath + "/engine_v2"
    private val binDir           get() = "$internalFilesDir/bin"
    private val wineDir          get() = "$internalFilesDir/wine"
    private val winePrefixDir    get() = "$internalFilesDir/wine_prefix"

    // ── Binary Kurulum ──────────────────────────────────────
    /**
     * İlk çalışmada assets/runtime/ içindeki Box64 + Wine binary'lerini
     * /data/data/.../files/ dizinine çıkartır.
     *
     * NOT: Gerçek Box64/Wine binary'leri WinLator release'inden alınmalı
     * ve assets/runtime/ içine konulmalıdır. Bu fonksiyon onları ayarlar.
     */
    suspend fun installRuntime(): Boolean = withContext(Dispatchers.IO) {
        try {
            val marker = File("$internalFilesDir/.runtime_installed")
            if (marker.exists()) {
                Log.i(TAG, "installRuntime: Zaten kurulu, atlandı.")
                return@withContext true
            }

            Log.i(TAG, "installRuntime: Kurulum başlıyor...")
            File(internalFilesDir).mkdirs()
            File(binDir).mkdirs()
            File(wineDir).mkdirs()
            File(winePrefixDir).mkdirs()

            // 1. engine_v3.zip varsa çıkart (opsiyonel — yoksa atla)
            val hasZip = try {
                context.assets.open("engine_v3.zip").close()
                true
            } catch (e: Exception) { false }

            if (hasZip) {
                Log.i(TAG, "installRuntime: engine_v3.zip bulundu, çıkartılıyor...")
                context.assets.open("engine_v3.zip").use { inputStream ->
                    java.util.zip.ZipInputStream(inputStream).use { zis ->
                        var entry = zis.nextEntry
                        while (entry != null) {
                            val outFile = File(internalFilesDir, entry.name)
                            if (entry.isDirectory) {
                                outFile.mkdirs()
                            } else {
                                outFile.parentFile?.mkdirs()
                                outFile.outputStream().use { fos ->
                                    zis.copyTo(fos)
                                }
                            }
                            zis.closeEntry()
                            entry = zis.nextEntry
                        }
                    }
                }
                Log.i(TAG, "installRuntime: ZIP çıkartıldı.")
            } else {
                // engine_v3.zip yoksa assets/runtime/ klasöründen kopyala (fallback)
                val runtimeAssets = try { context.assets.list("runtime") } catch (e: Exception) { null }
                if (!runtimeAssets.isNullOrEmpty()) {
                    Log.i(TAG, "installRuntime: assets/runtime/ klasöründen kopyalanıyor (${runtimeAssets.size} öğe)...")
                    for (asset in runtimeAssets) {
                        copyAssetRecursive("runtime/$asset", internalFilesDir)
                    }
                } else {
                    Log.w(TAG, "installRuntime: Ne ZIP ne de assets/runtime/ var. Binary'ler daha sonra elle eklenecek.")
                }
            }

            // 1.5. wine.tar varsa çıkart (tar -xf komutu ile)
            val hasTar = try {
                context.assets.open("runtime/wine.tar").close()
                true
            } catch (e: Exception) { false }

            if (hasTar) {
                Log.i(TAG, "installRuntime: wine.tar bulundu, tar ile çıkartılıyor (symlink'ler korunacak)...")
                val tarFile = File(internalFilesDir, "wine.tar")
                context.assets.open("runtime/wine.tar").use { input ->
                    tarFile.outputStream().use { output ->
                        input.copyTo(output)
                    }
                }
                
                // Android'in native tar komutunu kullanarak dosyayı çıkar
                try {
                    val process = Runtime.getRuntime().exec(arrayOf("tar", "-xvf", tarFile.absolutePath, "-C", internalFilesDir))
                    val reader = java.io.BufferedReader(java.io.InputStreamReader(process.inputStream))
                    var count = 0
                    val totalFiles = 12224
                    var line: String?
                    while (reader.readLine().also { line = it } != null) {
                        count++
                        if (count % 50 == 0) {
                            val percent = (count * 100) / totalFiles
                            onInstallProgress?.invoke(if (percent > 99) 99 else percent)
                        }
                    }
                    val exitCode = process.waitFor()
                    if (exitCode == 0) {
                        onInstallProgress?.invoke(100)
                        Log.i(TAG, "installRuntime: wine.tar başarıyla çıkartıldı.")
                    } else {
                        Log.e(TAG, "installRuntime: tar çıkarma hatası, exitCode=$exitCode")
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "installRuntime: tar exec hatası: ${e.message}")
                }
                // Çıkarma bittikten sonra tar dosyasını sil
                tarFile.delete()

                // Klasör ismini wine olarak düzelt
                val extractedWineDir = File(internalFilesDir, "wine-11.16-amd64-wow64")
                if (extractedWineDir.exists()) {
                    val targetWineDir = File(internalFilesDir, "wine")
                    if (targetWineDir.exists()) {
                        targetWineDir.deleteRecursively()
                    }
                    extractedWineDir.renameTo(targetWineDir)
                    Log.i(TAG, "installRuntime: wine klasörü taşındı.")
                    
                    // 1.6 wineserver wrapper yerleştir
                    val realWineserver = File(targetWineDir, "bin/wineserver")
                    val wineserverReal = File(targetWineDir, "bin/wineserver.real")
                    if (realWineserver.exists() && !wineserverReal.exists()) {
                        realWineserver.renameTo(wineserverReal)
                        Log.i(TAG, "installRuntime: wineserver -> wineserver.real yapıldı")
                    }
                    
                    val wrapperFile = File(context.applicationInfo.nativeLibraryDir, "libwineserver_wrapper.so")
                    if (wrapperFile.exists()) {
                        wrapperFile.copyTo(realWineserver, overwrite = true)
                        realWineserver.setExecutable(true, false)
                        Log.i(TAG, "installRuntime: libwineserver_wrapper.so -> wineserver olarak kopyalandı")
                    } else {
                        Log.e(TAG, "installRuntime: libwineserver_wrapper.so bulunamadı!")
                    }
                    
                    // faketmp hook kopyala
                    val faketmpFile = File(context.applicationInfo.nativeLibraryDir, "libfaketmp.so")
                    val targetFaketmp = File(binDir, "libfaketmp.so")
                    if (faketmpFile.exists()) {
                        faketmpFile.copyTo(targetFaketmp, overwrite = true)
                        Log.i(TAG, "installRuntime: libfaketmp.so -> bin klasörüne kopyalandı")
                    } else {
                        Log.e(TAG, "installRuntime: libfaketmp.so bulunamadı!")
                    }
                }
            }


            // 2. binary'leri çalıştırılabilir yap
            File(binDir).listFiles()?.forEach { it.setExecutable(true, false) }
            File("$wineDir/bin").listFiles()?.forEach { it.setExecutable(true, false) }

            // 3. Tüm binary dosyalar için +x garantisi
            File(internalFilesDir).walkTopDown().forEach { file ->
                if (file.isFile && (file.parentFile?.name == "bin" || file.name.endsWith(".so") || file.name.endsWith(".so.1"))) {
                    file.setExecutable(true, false)
                }
            }

            // 4. rootfs symlink garantisi
            // ZIP'ten çıkan lib/bin stub dosyaları silinip gerçek symlink'e dönüştürülmeli
            try {
                val rootfsDir = File(internalFilesDir, "rootfs")
                if (rootfsDir.exists()) {
                    val usrLibDir = File(rootfsDir, "usr/lib")
                    val usrBinDir = File(rootfsDir, "usr/bin")

                    // lib → usr/lib
                    val libDir = File(rootfsDir, "lib")
                    if (libDir.exists() && !libDir.isDirectory) {
                        // Stub dosyası var, sil ve symlink yap
                        libDir.delete()
                        Log.i(TAG, "rootfs/lib stub silindi")
                    }
                    if (!libDir.exists() && usrLibDir.exists()) {
                        try {
                            android.system.Os.symlink(usrLibDir.absolutePath, libDir.absolutePath)
                            Log.i(TAG, "rootfs/lib → ${usrLibDir.absolutePath} symlink oluşturuldu")
                        } catch (e: Exception) {
                            Log.w(TAG, "lib symlink başarısız, kopyalanıyor: ${e.message}")
                            usrLibDir.copyRecursively(libDir, overwrite = true)
                        }
                    }

                    // lib64 → usr/lib (box64 için kritik!)
                    val lib64Dir = File(rootfsDir, "lib64")
                    if (lib64Dir.exists() && !lib64Dir.isDirectory) {
                        lib64Dir.delete()
                    }
                    if (!lib64Dir.exists() && usrLibDir.exists()) {
                        try {
                            android.system.Os.symlink(usrLibDir.absolutePath, lib64Dir.absolutePath)
                            Log.i(TAG, "rootfs/lib64 → ${usrLibDir.absolutePath} symlink oluşturuldu")
                        } catch (e: Exception) {
                            Log.w(TAG, "lib64 symlink başarısız: ${e.message}")
                        }
                    }

                    // bin → usr/bin
                    val rfsBinDir = File(rootfsDir, "bin")
                    if (rfsBinDir.exists() && !rfsBinDir.isDirectory) {
                        rfsBinDir.delete()
                        Log.i(TAG, "rootfs/bin stub silindi")
                    }
                    if (!rfsBinDir.exists() && usrBinDir.exists()) {
                        try {
                            android.system.Os.symlink(usrBinDir.absolutePath, rfsBinDir.absolutePath)
                            Log.i(TAG, "rootfs/bin → ${usrBinDir.absolutePath} symlink oluşturuldu")
                        } catch (e: Exception) {
                            Log.w(TAG, "bin symlink başarısız, kopyalanıyor: ${e.message}")
                            usrBinDir.copyRecursively(rfsBinDir, overwrite = true)
                        }
                    }

                    // GNU ld script temizliği (libm.so, libc.so, vb.) - Bionic linker crash (bad ELF magic) engelleme
                    val badLdScripts = listOf("libm.so", "libc.so", "libpthread.so", "libdl.so", "librt.so", "libresolv.so")
                    val searchDirs = listOf(usrLibDir, File(rootfsDir, "usr/lib/aarch64-linux-gnu"), libDir)
                    for (dir in searchDirs) {
                        if (dir.exists()) {
                            for (scriptName in badLdScripts) {
                                val f = File(dir, scriptName)
                                if (f.exists() && f.isFile && !f.isDirectory) {
                                    try {
                                        val header = ByteArray(4)
                                        f.inputStream().use { it.read(header) }
                                        if (header[0] != 0x7F.toByte() || header[1] != 'E'.code.toByte()) {
                                            Log.i(TAG, "GNU ld script silindi (bad ELF magic prevent): \${f.absolutePath}")
                                            f.delete()
                                            
                                            val name = f.name
                                            val target = when (name) {
                                                "libc.so" -> "libc.so.6"
                                                "libm.so" -> "libm.so.6"
                                                "libdl.so" -> "libdl.so.2"
                                                "libpthread.so" -> "libpthread.so.0"
                                                "librt.so" -> "librt.so.1"
                                                "libresolv.so" -> "libresolv.so.2"
                                                else -> null
                                            }
                                            if (target != null) {
                                                try {
                                                    val targetFile = File(f.parentFile, target)
                                                    if (targetFile.exists()) {
                                                        targetFile.copyTo(f, overwrite = true)
                                                        Log.i(TAG, "LD script yerine kopyalama yapıldı: \$name -> \$target")
                                                    } else {
                                                        Log.e(TAG, "Hedef dosya bulunamadığı için kopyalanamadı: \$target")
                                                    }
                                                } catch (e: Exception) {
                                                    Log.e(TAG, "Kopyalama hatası (\$name): \${e.message}")
                                                }
                                            }
                                        }
                                    } catch (e: Exception) {
                                        f.delete()
                                    }
                                }
                            }
                        }
                    }
                }
            } catch (e: Exception) {
                Log.w(TAG, "RootFS link setup warning: ${e.message}")
            }


            // 5. wineserver socket dizinini oluştur (/tmp yerine geçecek)
            val winePrefix = File(internalFilesDir, "wine_prefix")
            winePrefix.mkdirs()
            val wineserverSocket = File(winePrefix, "wineserver-socket")
            wineserverSocket.mkdirs()
            Log.i(TAG, "wineserver-socket dizini: ${wineserverSocket.absolutePath}")

            marker.writeText("ExeLite Runtime v3\n")

            Log.i(TAG, "installRuntime: Tamamlandı.")
            true
        } catch (e: Exception) {
            Log.e(TAG, "installRuntime HATA: ${e.message}")
            false
        }
    }

    // ── Oyun Başlat ─────────────────────────────────────────
    /**
     * @param exePath    .exe dosyasının tam yolu
     * @param gameDir    Oyunun klasörü (DLL'ler burada)
     * @param dxvk       DXVK (DirectX→Vulkan) aktif mi?
     * @param width/height Render çözünürlüğü
     */
    fun launchGame(
        exePath: String,
        gameDir: String,
        dxvk:    Boolean = true,
        width:   Int = 1280,
        height:  Int = 720
    ) {
        scope.launch {
            Log.i(TAG, "launchGame: $exePath")

            // 1) Runtime kurulu mu?
            if (!installRuntime()) {
                onErrorOccurred?.invoke(
                    EngineError.WINE_INIT_FAIL,
                    "Runtime kurulamadı"
                )
                return@launch
            }

            // 2) JNI initialize
            val initResult = EngineError.fromCode(
                EngineJNI.nativeInitialize(
                    exePath, gameDir, internalFilesDir, winePrefixDir,
                    width, height, dxvk
                )
            )

            if (initResult != EngineError.NONE) {
                Log.e(TAG, "initialize hatası: $initResult")
                onErrorOccurred?.invoke(initResult, "Başlatma hatası: $initResult")
                return@launch
            }

            // 3) DXVK DLL'leri wine prefix'e kopyala (Kotlin katmanı)
            if (dxvk) {
                val dxvkOk = copyDxvkToPrefix(winePrefixDir)
                if (!dxvkOk) {
                    Log.w(TAG, "DXVK DLL kopyalama başarısız, devam ediliyor (DXVK olmadan).")
                }
            }

            // 4) JNI start
            val startResult = EngineError.fromCode(EngineJNI.nativeStart())
            if (startResult != EngineError.NONE) {
                Log.e(TAG, "start hatası: $startResult")
                onErrorOccurred?.invoke(startResult, "Başlatma hatası: $startResult")
                return@launch
            }

            onStateChanged?.invoke(EngineState.RUNNING)

            // 5) Durum izleme döngüsünü başlat
            startStatusMonitor()
        }
    }

    // ── Durdur ──────────────────────────────────────────────
    fun stopGame() {
        statusJob?.cancel()
        EngineJNI.nativeStop()
        stopXServer()
        onStateChanged?.invoke(EngineState.IDLE)
        Log.i(TAG, "stopGame: Durduruldu.")
    }

    fun pauseGame() {
        EngineJNI.nativePause()
        onStateChanged?.invoke(EngineState.PAUSED)
    }

    fun resumeGame() {
        EngineJNI.nativeResume()
        onStateChanged?.invoke(EngineState.RUNNING)
    }

    // ── Giriş ───────────────────────────────────────────────
    fun sendAxis  (axis: Int, value: Float) = EngineJNI.nativeSendAxis(axis, value)
    fun sendButton(btn:  Int, pressed: Boolean) = EngineJNI.nativeSendButton(btn, pressed)
    fun sendKey   (key:  Int, pressed: Boolean) = EngineJNI.nativeSendKey(key, pressed)
    fun sendMouseMove(dx: Float, dy: Float)     = EngineJNI.nativeSendMouseMove(dx, dy)
    
    // ── XServer Yönetimi ────────────────────────────────────────────
    fun startXServer(xServer: XServer) {
        val tmpDir = File(internalFilesDir, "tmp")
        tmpDir.mkdirs()
        tmpDir.setReadable(true, false)
        tmpDir.setWritable(true, false)
        tmpDir.setExecutable(true, false)

        val x11Dir = File(internalFilesDir, "tmp/.X11-unix")
        x11Dir.mkdirs()
        x11Dir.setReadable(true, false)
        x11Dir.setWritable(true, false)
        x11Dir.setExecutable(true, false)

        val shmDir = File(internalFilesDir, "tmp/.sysvshm")
        shmDir.mkdirs()
        shmDir.setReadable(true, false)
        shmDir.setWritable(true, false)
        shmDir.setExecutable(true, false)

        // 1. X11 Unix Socket Connector
        val socketConfig = UnixSocketConfig.create(internalFilesDir, UnixSocketConfig.XSERVER_PATH)
        
        xConnector = XConnectorEpoll(socketConfig, XClientConnectionHandler(xServer), XClientRequestHandler())
        xConnector?.setInitialInputBufferCapacity(4096)
        xConnector?.setInitialOutputBufferCapacity(4096)
        xConnector?.setCanReceiveAncillaryMessages(true)
        xConnector?.start()
        Log.i(TAG, "XServer Unix socket started at ${socketConfig.path}")

        // 2. SysV Shared Memory Connector (MIT-SHM extension için gerekli)
        val sysVSharedMemory = SysVSharedMemory()
        xServer.shmSegmentManager = SHMSegmentManager(sysVSharedMemory)
        val shmSocketConfig = UnixSocketConfig.create(internalFilesDir, UnixSocketConfig.SYSVSHM_SERVER_PATH)
        shmConnector = XConnectorEpoll(shmSocketConfig, SysVSHMConnectionHandler(sysVSharedMemory), SysVSHMRequestHandler())
        shmConnector?.setInitialInputBufferCapacity(128)
        shmConnector?.setInitialOutputBufferCapacity(128)
        shmConnector?.start()
        Log.i(TAG, "SysVSHM connector started at ${shmSocketConfig.path}")

        // 3. TCP Proxy (Unix -> TCP 6000)
        x11TcpProxy = X11TcpProxy(socketConfig.path, 6000)
        x11TcpProxy?.start()
        Log.i(TAG, "XServer and TCP proxy started at 6000")
    }

    private fun stopXServer() {
        xConnector?.destroy()
        xConnector = null
        shmConnector?.destroy()
        shmConnector = null
        x11TcpProxy?.stop()
        x11TcpProxy = null
    }
    fun sendMouseButton(btn: Int, pressed: Boolean) = EngineJNI.nativeSendMouseButton(btn, pressed)

    // ── Durum İzleme ────────────────────────────────────────
    private fun startStatusMonitor() {
        statusJob = scope.launch {
            while (isActive) {
                delay(1000L) // Her saniye güncelle

                val status = EngineJNI.nativeGetStatus()
                if (status.size >= 5) {
                    val state   = EngineState.fromCode(status[0])
                    val fps     = status[1]
                    val memMb   = status[2]
                    val errCode = status[4]

                    withContext(Dispatchers.Main) {
                        onStatusUpdate?.invoke(fps, memMb)
                    }

                    // Oyun kapandıysa loop'u bitir
                    if (state == EngineState.IDLE || state == EngineState.ERROR) {
                        withContext(Dispatchers.Main) {
                            onStateChanged?.invoke(state)
                        }
                        break
                    }
                }
            }
        }
    }

    // ── Temizlik ────────────────────────────────────────────
    fun destroy() {
        statusJob?.cancel()
        // nativeStop() scope.cancel()'den ÖNCE çağrılmalı!
        // Aksi halde scope iptal edildikten sonra JNI çağrısı undefined olabilir.
        EngineJNI.nativeStop()
        scope.cancel()
    }

    // ── DXVK DLL Kopyalama (Kotlin tarafı) ───────────────────────
    /**
     * DXVK DLL'lerini assets/runtime/dxvk/system32/ altından
     * wine prefix dizinine kopyalar.
     * Prefix, nativeInitialize() tarafından oluşturulmuş olmalıdır.
     */
    private suspend fun copyDxvkToPrefix(winePrefixPath: String): Boolean =
        withContext(Dispatchers.IO) {
            try {
                val srcDir  = File("$internalFilesDir/dxvk/system32")
                val destDir = File("$winePrefixPath/drive_c/windows/system32")
                destDir.mkdirs()

                if (!srcDir.exists()) {
                    Log.w(TAG, "copyDxvkToPrefix: DXVK kaynak dizini yok: ${srcDir.absolutePath}")
                    return@withContext false
                }

                var copied = 0
                srcDir.listFiles()?.forEach { dll ->
                    val dest = File(destDir, dll.name)
                    if (!dest.exists()) {     // Zaten varsa üzerine yazma
                        dll.copyTo(dest, overwrite = false)
                        Log.i(TAG, "DXVK DLL kopyalandı: ${dll.name}")
                        copied++
                    }
                }
                Log.i(TAG, "copyDxvkToPrefix: $copied DLL kopyalandı → ${destDir.absolutePath}")
                true
            } catch (e: Exception) {
                Log.e(TAG, "copyDxvkToPrefix HATA: ${e.message}")
                false
            }
        }

    // ── Assets Kopyalama Yardımcısı ─────────────────────────
    private fun copyAssetRecursive(assetPath: String, destDir: String) {
        val list = context.assets.list(assetPath)
        val relativePath = assetPath.removePrefix("runtime/")
        if (list.isNullOrEmpty()) {
            // Dosya
            val destFile = File("$destDir/$relativePath")
            destFile.parentFile?.mkdirs()
            context.assets.open(assetPath).use { input ->
                destFile.outputStream().use { output ->
                    input.copyTo(output)
                }
            }
        } else {
            // Dizin
            for (item in list) {
                copyAssetRecursive("$assetPath/$item", destDir)
            }
        }
    }
}
