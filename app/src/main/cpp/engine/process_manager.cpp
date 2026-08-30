// ============================================================
// process_manager.cpp — Wine + Box64 Process Başlatma
//
// Akış:
//   Android App → JNI → ProcessManager::launchExe()
//       → fork() → execve(box64, [wine, game.exe])
//
// Box64, x86_64 Wine binary'sini ARM64'de çalıştırır.
// Wine ise Windows API katmanını sağlar, Explorer.exe OLMADAN.
// ============================================================
#include "process_manager.h"

#include <android/log.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sched.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>

#define LOG_TAG "ExeLite.PM"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace exelite {

ProcessManager& ProcessManager::instance() {
    static ProcessManager inst;
    return inst;
}

LaunchResult ProcessManager::launchExe(
    const std::string& exe_path,
    const std::string& working_dir,
    const std::string& internal_files_dir,
    const std::string& wine_prefix,
    int64_t            max_memory_mb)
{
    LaunchResult result;

    // ── Box64 binary yolu ────────────────────────────────────
    // Box64 binary'si assets'ten /data/user/0/.../files/engine_v2/bin/box64 konumuna
    // uygulama ilk açılışında kopyalanır (EngineManager.installRuntime).
    const std::string box64_bin  = internal_files_dir + "/bin/box64";

    // wine64 → wine fallback: bazı Wine derlemelerinde sadece "wine" olur
    const std::string wine_bin64 = internal_files_dir + "/wine/bin/wine64";
    const std::string wine_bin32 = internal_files_dir + "/wine/bin/wine";
    const std::string wine_bin   = (access(wine_bin64.c_str(), X_OK) == 0)
                                   ? wine_bin64 : wine_bin32;

    LOGI("launchExe: Paths —");
    LOGI("  internal_files_dir = %s", internal_files_dir.c_str());
    LOGI("  box64_bin          = %s", box64_bin.c_str());
    LOGI("  wine_bin64         = %s", wine_bin64.c_str());
    LOGI("  wine_bin32         = %s", wine_bin32.c_str());
    LOGI("  wine_bin (chosen)  = %s", wine_bin.c_str());
    LOGI("  exe_path           = %s", exe_path.c_str());
    LOGI("  wine_prefix        = %s", wine_prefix.c_str());

    // ── Varlık kontrolü ──────────────────────────────────────────
    if (access(box64_bin.c_str(), X_OK) != 0) {
        LOGE("Box64 binary bulunamadı: %s", box64_bin.c_str());
        LOGE("  (access errno=%d: %s)", errno, strerror(errno));
        // Dosya var mı ama +x eksik mi?
        if (access(box64_bin.c_str(), F_OK) == 0) {
            LOGE("  -> Dosya VAR ama çalıştırma izni YOK! chmod +x gerekiyor.");
            // Otomatik +x ver
            chmod(box64_bin.c_str(), 0755);
            LOGI("  -> chmod 755 uygulandı, tekrar deneniyor...");
            if (access(box64_bin.c_str(), X_OK) != 0) {
                result.error_msg = "Box64 binary var ama çalıştırılamıyor (chmod başarısız): " + box64_bin;
                LOGE("%s", result.error_msg.c_str());
                return result;
            }
        } else {
            result.error_msg = "Box64 binary bulunamadı: " + box64_bin
                             + " — Lütfen engine binary'lerini assets/runtime/ klasörüne ekleyin.";
            LOGE("%s", result.error_msg.c_str());
            return result;
        }
    }
    if (access(wine_bin.c_str(), X_OK) != 0) {
        LOGE("Wine binary bulunamadı. Aranan yollar:");
        LOGE("  %s", wine_bin64.c_str());
        LOGE("  %s", wine_bin32.c_str());
        // Dosya var ama +x yok mu?
        if (access(wine_bin32.c_str(), F_OK) == 0) {
            chmod(wine_bin32.c_str(), 0755);
            LOGI("wine binary (+x eklendi): %s", wine_bin32.c_str());
        } else if (access(wine_bin64.c_str(), F_OK) == 0) {
            chmod(wine_bin64.c_str(), 0755);
            LOGI("wine64 binary (+x eklendi): %s", wine_bin64.c_str());
        } else {
            result.error_msg = "Wine binary bulunamadı. Aranan:\n  "
                             + wine_bin64 + "\n  " + wine_bin32;
            LOGE("%s", result.error_msg.c_str());
            return result;
        }
    }
    LOGI("Wine binary seçildi: %s", wine_bin.c_str());


    // ── Ortam Değişkenleri ───────────────────────────────────
    const std::string wine_dir    = internal_files_dir + "/wine";
    const std::string wine_lib    = wine_dir + "/lib";
    const std::string wine_share  = wine_dir + "/share";
    const std::string rootfs_lib  = internal_files_dir + "/rootfs/usr/lib";
    const std::string rootfs_lib2 = internal_files_dir + "/rootfs/lib";

    // Gerçek X11 socket path'i (bind mount YOK — tam path kullanıyoruz)
    const std::string x11_socket_dir = internal_files_dir + "/tmp/.X11-unix";
    const std::string x11_socket     = x11_socket_dir + "/X0";
    const std::string shm_socket     = internal_files_dir + "/tmp/.sysvshm/SM0";

    std::vector<std::string> env_strings = {
        "WINEPREFIX="     + wine_prefix,
        "WINELOADER="     + wine_bin,
        "WINESERVER="     + wine_dir + "/bin/wineserver",
        "WINEDLLPATH="    + wine_lib + "/wine/x86_64-unix:" + wine_lib + "/wine/x86_64-windows:" + wine_lib + "/wine/i386-windows:" + wine_lib + "/wine/i386-unix:" + wine_lib + "/wine",
        "WINEDATADIR="    + wine_share + "/wine",
        "WINEDEBUG=err+all",
        "WINEDLLOVERRIDES=d3d9,d3d10core,d3d11=n,b",
        // X11: DISPLAY=:0 ama socket tam path'te — Wine bu env var ile socket'i bulur
        "DISPLAY=:0",
        // Kritik: Wine/libX11 bu path'i DISPLAY=:0 yerine kullanır (bazı derleme ayarlarında)
        "XSOCKET_DIR="    + x11_socket_dir,
        "ANDROID_SYSVSHM_SERVER=" + shm_socket,
        // /tmp yoksa internal tmp kullan
        "TMPDIR="         + internal_files_dir + "/tmp",
        "INTERNAL_FILES_DIR=" + internal_files_dir,
        "WINESERVER_SOCKET_DIRECTORY=" + wine_prefix,
        "XDG_RUNTIME_DIR=" + internal_files_dir + "/tmp",
        // LD_LIBRARY_PATH: ld-linux-aarch64.so.1'in ARM64 libc.so.6'yı bulabilmesi için (KRİTİK!)
        "LD_LIBRARY_PATH=" + rootfs_lib + ":" + rootfs_lib2,
        // Box64 ayarları
        "BOX64_PATH="     + wine_dir + "/bin",
        "BOX64_LD_LIBRARY_PATH=" + wine_lib + ":" + wine_lib + "/wine/x86_64-unix:" + wine_lib + "/wine/i386-unix:" + internal_files_dir + "/rootfs/usr/lib/x86_64-linux-gnu:" + rootfs_lib,
        "BOX64_LOG=1",
        "BOX64_SHOWSEGV=1",
        "BOX64_SHOWBT=1",
        "BOX64_NOBANNER=0",
        "BOX64_DYNAREC=1",
        "BOX64_DYNAREC_STRONG=1",
        "BOX64_DYNAREC_BLEEDING_EDGE=1",
        "BOX64_BASH="     + wine_bin,
        "BOX64_BIN="      + box64_bin,
        "REAL_WINESERVER=" + wine_dir + "/bin/wineserver.real",
        // Genel
        "HOME="           + wine_prefix,
        "PATH="           + wine_dir + "/bin:" + internal_files_dir + "/rootfs/usr/local/bin:" + internal_files_dir + "/rootfs/usr/bin:/system/bin",
        "WINE_LARGE_ADDRESS_AWARE=1",
        "WINEESYNC=0",
        "WINEFSYNC=0",
        "LD_PRELOAD=" + internal_files_dir + "/bin/libfaketmp.so",
    };

    // env_strings → char* dizisine çevir
    std::vector<const char*> envp;
    for (auto& s : env_strings) envp.push_back(s.c_str());
    envp.push_back(nullptr);

    // ── Komut Argümanları ────────────────────────────────────
    // Box64'ü doğrudan exec yerine ld-linux interpreter üzerinden çalıştır:
    //   ld-linux-aarch64.so.1 --library-path <rootfs/usr/lib> box64 wine exe
    // Bu sayede:
    //   1) ld-linux, --library-path'teki ARM64 libc.so.6'yı bulur
    //   2) patchelf rpath gerekmez
    //   3) Android linker çakışması olmaz
    const std::string ld_interp = internal_files_dir + "/rootfs/lib/ld-linux-aarch64.so.1";
    const std::string lib_path  = rootfs_lib + ":" + rootfs_lib2;

    bool use_interp = (access(ld_interp.c_str(), X_OK) == 0);
    LOGI("ld_interp %s: %s", use_interp ? "KULLANILIYOR" : "BULUNAMADI", ld_interp.c_str());

    std::vector<const char*> argv;
    std::string explorer_cmd = "explorer";
    std::string desktop_arg = "/desktop=ExeLite,1280x720";

    if (use_interp) {
        argv = {
            ld_interp.c_str(),         // argv[0]: ARM64 dynamic linker
            "--library-path",          // ARM64 lib search path (libc.so.6 buradan)
            lib_path.c_str(),
            box64_bin.c_str(),         // box64 (ARM64)
            wine_bin.c_str(),          // wine (x86-64, box64 emüle eder)
            explorer_cmd.c_str(),
            desktop_arg.c_str(),
            exe_path.c_str(),          // setup.exe
            nullptr
        };
        LOGI("launchExe (via interp): %s --library-path %s %s %s %s %s %s",
             ld_interp.c_str(), lib_path.c_str(),
             box64_bin.c_str(), wine_bin.c_str(), explorer_cmd.c_str(), desktop_arg.c_str(), exe_path.c_str());
    } else {
        // Fallback: doğrudan exec (patchelf interpreter yeterli olmalı)
        argv = {
            box64_bin.c_str(),
            wine_bin.c_str(),
            explorer_cmd.c_str(),
            desktop_arg.c_str(),
            exe_path.c_str(),
            nullptr
        };
        LOGI("launchExe (direct): %s %s %s %s %s",
             box64_bin.c_str(), wine_bin.c_str(), explorer_cmd.c_str(), desktop_arg.c_str(), exe_path.c_str());
    }

    // ── Fork → Exec ─────────────────────────────────────────
    pid_t pid = fork();

    if (pid < 0) {
        result.error_msg = "fork() başarısız";
        LOGE("%s", result.error_msg.c_str());
        return result;
    }

    if (pid == 0) {
        // ─── ÇOCUK PROCESS ───────────────────────────────────
        chdir(working_dir.c_str());

        // Düşük öncelik (Android UI thread'i etkilemesin)
        setpriority(PRIO_PROCESS, 0, 5);

        // Log dosyasına stdout/stderr yönlendir
        const std::string log_path = wine_prefix + "/wine.log";
        int log_fd = open(log_path.c_str(),
                          O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd >= 0) {
            dprintf(log_fd, "=== ExeLite Starting Box64 ===\nBox64: %s\nWine: %s\nExe: %s\n\n",
                    box64_bin.c_str(), wine_bin.c_str(), exe_path.c_str());
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }

        // /tmp BIND MOUNT YOK — Android app process'i CLONE_NEWNS yapamaz.
        // Bunun yerine:
        //  - TMPDIR, XDG_RUNTIME_DIR, WINESERVER_SOCKET_DIRECTORY env var'ları
        //    internal_files_dir/tmp'ye işaret ediyor
        //  - ld-linux --library-path ile box64 başlatılıyor (libc.so.6 sorununu çözer)
        //  - DISPLAY=:0 ile X11 socket kullanılıyor
        //
        // Gerçek X11 socket: internal_files_dir/tmp/.X11-unix/X0
        // Wine bu socket'i DISPLAY=:0 üzerinden bulmak için /tmp'ye bakar.
        // Çözüm: /tmp/.X11-unix'e internal path'ten symlink oluştur
        const std::string fake_tmp    = internal_files_dir + "/tmp";
        const std::string sys_x11_dir = "/tmp/.X11-unix";
        const std::string our_x11_dir = fake_tmp + "/.X11-unix";
        mkdir(fake_tmp.c_str(), 0777);
        // /tmp/.X11-unix yoksa oluştur ve symlink ile köprüle
        struct stat st;
        if (stat("/tmp", &st) == 0) {
            // /tmp yazılabilir — socket'e symlink kur
            mkdir("/tmp/.X11-unix", 0777);
            // X0 symlink
            const std::string real_x0 = our_x11_dir + "/X0";
            const std::string sys_x0  = sys_x11_dir + "/X0";
            unlink(sys_x0.c_str());
            symlink(real_x0.c_str(), sys_x0.c_str());
            dprintf(STDOUT_FILENO, "[ExeLite] X11 symlink: %s -> %s\n",
                    sys_x0.c_str(), real_x0.c_str());
            // SM0 symlink
            mkdir("/tmp/.sysvshm", 0777);
            const std::string real_sm = fake_tmp + "/.sysvshm/SM0";
            const std::string sys_sm  = "/tmp/.sysvshm/SM0";
            unlink(sys_sm.c_str());
            symlink(real_sm.c_str(), sys_sm.c_str());
        } else {
            dprintf(STDOUT_FILENO, "[ExeLite] /tmp erisim yok (errno=%d), devam...\n", errno);
        }

        // use_interp'e göre doğru binary'yi exec et
        const char* exec_bin = use_interp ? ld_interp.c_str() : box64_bin.c_str();
        execve(exec_bin,
               const_cast<char* const*>(argv.data()),
               const_cast<char* const*>(envp.data()));

        // execve başarısız olduysa buraya düşer
        LOGE("execve başarısız: %s", strerror(errno));
        _exit(1);
    }

    // ─── ANA PROCESS ─────────────────────────────────────────
    wine_pid_  = pid;
    box64_pid_ = pid; // Box64 ve Wine aynı PID'de başlar (Box64 wine'ı exec eder)

    result.success   = true;
    result.wine_pid  = wine_pid_;
    result.box64_pid = box64_pid_;

    LOGI("launchExe: Başarılı — PID=%d", wine_pid_);
    return result;
}

// ── Process Çalışıyor mu? ────────────────────────────────────
bool ProcessManager::isRunning() const {
    if (wine_pid_ <= 0) return false;
    // waitpid ile zombie process'leri temizle (WNOHANG: bloklamadan)
    int status = 0;
    pid_t ret = waitpid(wine_pid_, &status, WNOHANG);
    if (ret == wine_pid_) {
        // Child process sonlandı (zombie temizlendi)
        LOGI("isRunning: Process %d sonlandı (waitpid ile temizlendi)", wine_pid_);
        return false;
    }
    if (ret == -1 && errno == ECHILD) {
        // Bu PID bizim child'imiz değil veya zaten temizlenmiş
        return false;
    }
    // ::kill(pid, 0) → process var mı kontrol eder, sinyal göndermez
    return (::kill(wine_pid_, 0) == 0);
}

// ── Durdur ──────────────────────────────────────────────────
void ProcessManager::kill() {
    if (wine_pid_ > 0) {
        // Önce nazikçe sor
        ::kill(wine_pid_, SIGTERM);
        usleep(500000); // 500ms bekle
        // Hâlâ çalışıyorsa zorla öldür
        if (::kill(wine_pid_, 0) == 0) {
            ::kill(wine_pid_, SIGKILL);
        }
        // Child process'i waitpid ile temizle (zombie önleme)
        int status = 0;
        waitpid(wine_pid_, &status, 0);
        LOGI("kill: Process %d sonlandırıldı ve temizlendi.", wine_pid_);
        wine_pid_  = -1;
        box64_pid_ = -1;
    }
}

// ── RAM Kullanımı ────────────────────────────────────────────
int64_t ProcessManager::getMemoryUsageMb() const {
    if (wine_pid_ <= 0) return 0;

    // /proc/<pid>/status dosyasından VmRSS oku
    std::string path = "/proc/" + std::to_string(wine_pid_) + "/status";
    std::ifstream f(path);
    std::string line;

    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            long long kb = 0;
            sscanf(line.c_str(), "VmRSS: %lld kB", &kb);
            return static_cast<int64_t>(kb / 1024); // MB'a çevir
        }
    }
    return 0;
}

} // namespace exelite
