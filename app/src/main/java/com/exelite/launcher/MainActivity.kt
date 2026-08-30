package com.exelite.launcher

import android.Manifest
import android.app.Activity
import android.content.ContentUris
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.DocumentsContract
import android.provider.MediaStore
import android.provider.Settings
import android.util.Log
import android.view.View
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.google.android.material.button.MaterialButton
import com.exelite.engine.GameActivity
import com.exelite.engine.EngineManager
import kotlinx.coroutines.*
import java.io.File

private const val TAG          = "MainActivity"
private const val PREFS_NAME   = "ExeLitePrefs"
private const val PREF_SETUP   = "last_setup_path"
private const val PREF_EXE     = "last_exe_path"

/**
 * Launcher akışı:
 *  1. Kullanıcı Setup EXE seçer  → "RUN SETUP" etkinleşir
 *  2. Setup çalışır (GameActivity, setup=true)
 *  3. Setup biter → RESULT_OK  → Step 2 (Game EXE) kartı açılır
 *  4. Kullanıcı Game EXE seçer → "LAUNCH" etkinleşir
 *  5. Oyun başlar
 *
 *  Eğer oyun zaten kuruluysa Step 2'ye direkt geçilebilir (Setup kartı opsiyonel).
 */
class MainActivity : AppCompatActivity() {

    // ── Durum ───────────────────────────────────────────────────
    private enum class Phase { SETUP, GAME }
    private var phase = Phase.SETUP

    private var setupPath: String? = null   // seçilen setup .exe
    private var exePath:   String? = null   // seçilen oyun .exe

    // ── UI ──────────────────────────────────────────────────────
    private lateinit var textSetupPath:  TextView
    private lateinit var textExePath:    TextView
    private lateinit var btnSelectSetup: MaterialButton
    private lateinit var btnClearSetup:  MaterialButton
    private lateinit var btnSelectExe:   MaterialButton
    private lateinit var btnClearExe:    MaterialButton
    private lateinit var btnLaunch:      MaterialButton
    private lateinit var tvProgress:     TextView
    private lateinit var tvSubtitle:     TextView

    // ── Activity Result ─────────────────────────────────────────

    private val setupFilePicker = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri -> uri?.let { handleFileUri(it, isSetup = true) } }

    private val exeFilePicker = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri -> uri?.let { handleFileUri(it, isSetup = false) } }

    /**
     * Setup GameActivity bitti → RESULT_OK gelirse Game EXE adımına geç.
     */
    private val setupLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            setStatus("Setup complete. Now select the game executable.")
            unlockGamePhase()
        } else {
            // Hata nedeni: Box64/Wine binary'leri henüz assets/runtime/ klasörüne eklenmemiş.
            // Logcat'te ExeLite.PM tag'ini arayın.
            android.widget.Toast.makeText(
                this,
                "Setup failed. Check logcat for: ExeLite.PM\nBinary'ler eksik olabilir.",
                android.widget.Toast.LENGTH_LONG
            ).show()
            setStatus("Setup failed. Box64/Wine binary missing — check logcat tag: ExeLite.PM")
        }
    }

    /** MANAGE_EXTERNAL_STORAGE (Android 11+) */
    private val manageStorageLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) {
        val granted = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && Environment.isExternalStorageManager()
        setStatus(if (granted) "Storage permission granted." else "Warning: limited storage access.")
    }

    // ── Lifecycle ───────────────────────────────────────────────

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        bindViews()
        restorePrefs()
        checkStoragePermission()
        setupClickListeners()
        refreshUI()
    }

    private fun bindViews() {
        textSetupPath  = findViewById(R.id.textViewSetupPath)
        textExePath    = findViewById(R.id.textViewExePath)
        btnSelectSetup = findViewById(R.id.buttonSelectSetup)
        btnClearSetup  = findViewById(R.id.buttonClearSetup)
        btnSelectExe   = findViewById(R.id.buttonSelectExe)
        btnClearExe    = findViewById(R.id.buttonClearExe)
        btnLaunch      = findViewById(R.id.buttonLaunch)
        tvProgress     = findViewById(R.id.tvProgress)
        tvSubtitle     = findViewById(R.id.textViewSubtitle)
    }

    private fun setupClickListeners() {
        btnSelectSetup.setOnClickListener { checkPermissionThen { setupFilePicker.launch(arrayOf("*/*")) } }
        btnClearSetup.setOnClickListener  { clearSetup() }
        btnSelectExe.setOnClickListener   { checkPermissionThen { exeFilePicker.launch(arrayOf("*/*")) } }
        btnClearExe.setOnClickListener    { clearExe() }
        btnLaunch.setOnClickListener      { onLaunchClicked() }
    }

    // ── İzin ────────────────────────────────────────────────────

    private fun checkStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                AlertDialog.Builder(this)
                    .setTitle("Storage Access Required")
                    .setMessage("ExeLite needs full storage access to read game files from any location (including SD card).")
                    .setPositiveButton("Open Settings") { _, _ ->
                        manageStorageLauncher.launch(
                            Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                        )
                    }
                    .setNegativeButton("Skip") { _, _ ->
                        setStatus("Warning: storage access limited.")
                    }
                    .show()
            }
        } else {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(
                    arrayOf(
                        Manifest.permission.READ_EXTERNAL_STORAGE,
                        Manifest.permission.WRITE_EXTERNAL_STORAGE
                    ), 100
                )
            }
        }
    }

    private fun checkPermissionThen(block: () -> Unit) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !Environment.isExternalStorageManager()) {
            Toast.makeText(this, "Grant storage permission first.", Toast.LENGTH_SHORT).show()
            return
        }
        block()
    }

    // ── File URI → Path ─────────────────────────────────────────

    private fun handleFileUri(uri: Uri, isSetup: Boolean) {
        setStatus("Resolving file path…")
        val path = resolveRealPath(uri)

        if (path == null) {
            setStatus("Could not resolve file path. Move the file to internal storage and try again.")
            Toast.makeText(this, "Path resolution failed", Toast.LENGTH_LONG).show()
            return
        }
        if (!File(path).exists()) {
            setStatus("File not found: $path")
            return
        }

        if (isSetup) {
            setupPath = path
            setStatus("Setup selected: ${File(path).name}")
        } else {
            exePath = path
            setStatus("Game EXE selected: ${File(path).name}")
        }

        savePrefs()
        refreshUI()
    }

    /**
     * SAF URI → gerçek dosya yolu.
     * Zincir: ExternalStorage doc → Downloads provider → MediaStore → /proc/self/fd
     */
    private fun resolveRealPath(uri: Uri): String? {
        val scheme = uri.scheme

        if (scheme == "file") return uri.path

        if (scheme == "content") {
            // 1. Document URI
            if (DocumentsContract.isDocumentUri(this, uri)) {
                try {
                    val docId    = DocumentsContract.getDocumentId(uri)
                    val authority = uri.authority ?: ""

                    if (authority == "com.android.externalstorage.documents") {
                        val (type, rel) = docId.split(":").let { it[0] to (it.getOrNull(1) ?: "") }
                        if (type.equals("primary", ignoreCase = true)) {
                            val p = "${Environment.getExternalStorageDirectory()}/$rel"
                            if (File(p).exists()) return p
                        } else {
                            for (vol in getExternalFilesDirs(null)) {
                                val root = vol?.absolutePath?.substringBefore("/Android") ?: continue
                                val p = "$root/$rel"
                                if (File(p).exists()) return p
                            }
                        }
                    }

                    if (authority == "com.android.providers.downloads.documents") {
                        if (docId.startsWith("raw:")) return docId.removePrefix("raw:")
                        docId.toLongOrNull()?.let { id ->
                            val cu = ContentUris.withAppendedId(
                                Uri.parse("content://downloads/public_downloads"), id
                            )
                            return queryColumn(cu, MediaStore.MediaColumns.DATA)
                        }
                    }

                    if (authority == "com.android.providers.media.documents") {
                        val parts = docId.split(":")
                        val mediaId = parts.getOrNull(1)?.toLongOrNull() ?: return null
                        val base = when (parts[0]) {
                            "image" -> MediaStore.Images.Media.EXTERNAL_CONTENT_URI
                            "video" -> MediaStore.Video.Media.EXTERNAL_CONTENT_URI
                            "audio" -> MediaStore.Audio.Media.EXTERNAL_CONTENT_URI
                            else    -> MediaStore.Files.getContentUri("external")
                        }
                        return queryColumn(base, MediaStore.MediaColumns.DATA,
                            "${MediaStore.MediaColumns._ID}=?", arrayOf(mediaId.toString()))
                    }
                } catch (e: Exception) {
                    Log.w(TAG, "Doc URI resolve: ${e.message}")
                }
            }

            // 2. Normal content URI — MediaStore
            queryColumn(uri, MediaStore.MediaColumns.DATA)
                ?.takeIf { File(it).exists() }
                ?.let { return it }

            // 3. /proc/self/fd
            try {
                contentResolver.openFileDescriptor(uri, "r")?.use { pfd ->
                    val p = File("/proc/self/fd/${pfd.fd}").canonicalPath
                    if (File(p).exists()) return p
                }
            } catch (e: Exception) {
                Log.w(TAG, "/proc/self/fd: ${e.message}")
            }

            Log.e(TAG, "URI path unresolvable: $uri")
        }
        return null
    }

    private fun queryColumn(
        uri: Uri, column: String,
        selection: String? = null, args: Array<String>? = null
    ): String? = try {
        contentResolver.query(uri, arrayOf(column), selection, args, null)
            ?.use { c ->
                val idx = c.getColumnIndex(column)   // -1 döndürebilir — kontrol et
                if (idx >= 0 && c.moveToFirst()) c.getString(idx).takeIf { !it.isNullOrEmpty() } else null
            }
    } catch (e: Exception) { null }

    // ── UI Refresh ───────────────────────────────────────────────

    private fun refreshUI() {
        // Setup kartı
        if (setupPath != null) {
            textSetupPath.text = File(setupPath!!).name
            textSetupPath.setTextColor(0xFFAABBFF.toInt())
            btnClearSetup.visibility = View.VISIBLE
        } else {
            textSetupPath.text = "No file selected"
            textSetupPath.setTextColor(0xFF2A3558.toInt())
            btnClearSetup.visibility = View.GONE
        }

        // Game EXE kartı — sadece GAME fazında ya da daha önce seçilmişse aktif
        val gamePhaseActive = phase == Phase.GAME || exePath != null
        btnSelectExe.isEnabled = gamePhaseActive
        btnSelectExe.alpha = if (gamePhaseActive) 1.0f else 0.4f
        btnSelectExe.setTextColor(if (gamePhaseActive) 0xFF88FFBB.toInt() else 0xFF2A4A38.toInt())

        if (exePath != null) {
            textExePath.text = File(exePath!!).name
            textExePath.setTextColor(0xFF88FFBB.toInt())
            btnClearExe.visibility = View.VISIBLE
        } else {
            textExePath.text = if (gamePhaseActive) "No file selected" else "Complete setup first"
            textExePath.setTextColor(0xFF1A2A20.toInt())
            btnClearExe.visibility = View.GONE
        }

        // Launch butonu
        val canRun = setupPath != null || exePath != null
        btnLaunch.isEnabled = canRun
        btnLaunch.alpha = if (canRun) 1.0f else 0.4f

        btnLaunch.text = when {
            setupPath != null && exePath == null -> "RUN SETUP"
            exePath   != null                    -> "LAUNCH GAME"
            else                                 -> "RUN SETUP"
        }
    }

    private fun unlockGamePhase() {
        phase = Phase.GAME
        setupPath = null          // setup tek seferlik
        textSetupPath.text = "Setup completed"
        textSetupPath.setTextColor(0xFF44AA66.toInt())
        btnClearSetup.visibility = View.GONE
        refreshUI()
    }

    // ── Clear ────────────────────────────────────────────────────

    private fun clearSetup() {
        setupPath = null
        savePrefs()
        refreshUI()
        setStatus("Setup selection cleared.")
    }

    private fun clearExe() {
        exePath = null
        savePrefs()
        refreshUI()
        setStatus("Game EXE selection cleared.")
    }

    // ── Launch ───────────────────────────────────────────────────

    private fun onLaunchClicked() {
        // Öncelik: Setup varsa setup çalıştır
        if (setupPath != null) {
            runSetup(setupPath!!)
            return
        }
        // Yoksa oyunu başlat
        val exe = exePath ?: run {
            Toast.makeText(this, "Select a file first.", Toast.LENGTH_SHORT).show()
            return
        }
        launchGame(exe)
    }

    private fun runSetup(path: String) {
        Log.i(TAG, "runSetup → $path")
        
        btnLaunch.isEnabled = false
        tvProgress.text = "Preparing Engine... (0%)"
        val engineManager = EngineManager(this)
        engineManager.onInstallProgress = { percent ->
            runOnUiThread { tvProgress.text = "Extracting Engine... ($percent%)" }
        }

        CoroutineScope(Dispatchers.IO).launch {
            if (engineManager.installRuntime()) {
                withContext(Dispatchers.Main) {
                    setStatus("Starting installer…")
                    tvProgress.text = ""
                    btnLaunch.isEnabled = true
                    val intent = Intent(this@MainActivity, GameActivity::class.java).apply {
                        putExtra(GameActivity.EXTRA_EXE_PATH,   path)
                        putExtra(GameActivity.EXTRA_GAME_DIR,   File(path).parent ?: "/sdcard")
                        putExtra(GameActivity.EXTRA_SETUP_MODE, true)
                        putExtra(GameActivity.EXTRA_DXVK,       false)
                        putExtra(GameActivity.EXTRA_WIDTH,       1280)
                        putExtra(GameActivity.EXTRA_HEIGHT,      720)
                    }
                    setupLauncher.launch(intent)
                }
            } else {
                withContext(Dispatchers.Main) {
                    setStatus("Engine installation failed!")
                    tvProgress.text = "Failed!"
                    btnLaunch.isEnabled = true
                }
            }
        }
    }

    private fun launchGame(path: String) {
        val gameDir = File(path).parent ?: "/sdcard"
        Log.i(TAG, "launchGame → exe=$path  dir=$gameDir")
        
        btnLaunch.isEnabled = false
        tvProgress.text = "Preparing Engine... (0%)"
        val engineManager = EngineManager(this)
        engineManager.onInstallProgress = { percent ->
            runOnUiThread { tvProgress.text = "Extracting Engine... ($percent%)" }
        }

        CoroutineScope(Dispatchers.IO).launch {
            if (engineManager.installRuntime()) {
                withContext(Dispatchers.Main) {
                    setStatus("Launching game…")
                    tvProgress.text = ""
                    btnLaunch.isEnabled = true
                    val intent = Intent(this@MainActivity, GameActivity::class.java).apply {
                        putExtra(GameActivity.EXTRA_EXE_PATH,   path)
                        putExtra(GameActivity.EXTRA_GAME_DIR,   gameDir)
                        putExtra(GameActivity.EXTRA_SETUP_MODE, false)
                        putExtra(GameActivity.EXTRA_DXVK,       true)
                        putExtra(GameActivity.EXTRA_WIDTH,       1280)
                        putExtra(GameActivity.EXTRA_HEIGHT,      720)
                    }
                    startActivity(intent)
                }
            } else {
                withContext(Dispatchers.Main) {
                    setStatus("Engine installation failed!")
                    tvProgress.text = "Failed!"
                    btnLaunch.isEnabled = true
                }
            }
        }
    }

    // ── Helpers ──────────────────────────────────────────────────

    private fun setStatus(msg: String) {
        Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
        Log.i(TAG, "Status: $msg")
    }

    private fun savePrefs() {
        getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit()
            .putString(PREF_SETUP, setupPath)
            .putString(PREF_EXE,   exePath)
            .apply()
    }

    private fun restorePrefs() {
        // Cache iptal edildi. Her açılışta temiz gelsin.
        getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit().clear().apply()
        setupPath = null
        exePath = null
        phase = Phase.SETUP
    }
}
