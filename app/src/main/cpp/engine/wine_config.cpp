// ============================================================
// wine_config.cpp — Wine Prefix Kurulumu
//
// İlk çalışmada:
//   1. /data/data/.../files/wine_prefix/ dizin yapısını oluştur
//   2. DXVK DLL'lerini system32/syswow64 içine kopyala
//   3. Gerekli registry girdilerini yaz (user.reg, system.reg)
// ============================================================
#include "wine_config.h"

#include <android/log.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <cstring>

#define LOG_TAG "ExeLite.Wine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace exelite {

WineConfig& WineConfig::instance() {
    static WineConfig inst;
    return inst;
}

// ── Yardımcı: Dizin Oluştur ──────────────────────────────────
static bool mkdirs(const std::string& path) {
    // Özyinelemeli mkdir
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] == '/') {
            std::string sub = path.substr(0, i);
            mkdir(sub.c_str(), 0755);
        }
    }
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

// ── Prefix Kurulum ───────────────────────────────────────────
EngineError WineConfig::setupPrefix(const std::string& prefix_path, bool dxvk) {

    // Zaten kurulu mu?
    std::string marker = prefix_path + "/.exelite_setup_done";
    if (access(marker.c_str(), F_OK) == 0) {
        LOGI("setupPrefix: Prefix zaten kurulu, atlıyoruz.");
        return EngineError::NONE;
    }

    LOGI("setupPrefix: Yeni prefix kuruluyor → %s", prefix_path.c_str());

    EngineError err;

    err = createDirectoryStructure(prefix_path);
    if (err != EngineError::NONE) return err;

    if (dxvk) {
        err = copyDxvkDlls(prefix_path);
        if (err != EngineError::NONE) return err;
    }

    err = writeRegistryEntries(prefix_path);
    if (err != EngineError::NONE) return err;

    // Kurulum tamamlandı işareti bırak
    std::ofstream f(marker);
    f << "ExeLite Wine Prefix v1\n";
    f.close();

    LOGI("setupPrefix: Tamamlandı.");
    return EngineError::NONE;
}

// ── Dizin Yapısı ─────────────────────────────────────────────
EngineError WineConfig::createDirectoryStructure(const std::string& p) {
    const std::vector<std::string> dirs = {
        p,
        p + "/drive_c",
        p + "/drive_c/windows",
        p + "/drive_c/windows/system32",
        p + "/drive_c/windows/syswow64",
        p + "/drive_c/users",
        p + "/drive_c/users/user",
        p + "/drive_c/users/user/Desktop",
        p + "/drive_c/users/user/Documents",
        p + "/drive_c/users/user/AppData/Roaming",
        p + "/drive_c/users/user/AppData/Local",
        p + "/drive_c/Program Files",
        p + "/drive_c/Program Files (x86)",
        p + "/drive_c/temp",
        p + "/tmp",
        p + "/dosdevices",
    };

    for (auto& d : dirs) {
        mkdirs(d);
    }

    // Dosdevices symlink'leri
    std::string c_link = p + "/dosdevices/c:";
    if (access(c_link.c_str(), F_OK) != 0) {
        symlink("../drive_c", c_link.c_str());
    }

    std::string d_link = p + "/dosdevices/d:";
    if (access(d_link.c_str(), F_OK) != 0) {
        symlink("/storage/emulated/0", d_link.c_str());
    }

    std::string z_link = p + "/dosdevices/z:";
    if (access(z_link.c_str(), F_OK) != 0) {
        symlink("/", z_link.c_str());
    }

    return EngineError::NONE;
}

// ── DXVK DLL Kopyalama ────────────────────────────────────────────
EngineError WineConfig::copyDxvkDlls(const std::string& prefix_path) {
    // Kotlin tarafı (EngineManager.copyDxvkToPrefix) bu işi yapıyor.
    LOGI("copyDxvkDlls: Kotlin katmanı tarafından yönetiliyor.");
    return EngineError::NONE;
}

// ── Registry Girdileri ───────────────────────────────────────────
EngineError WineConfig::writeRegistryEntries(const std::string& p) {
    std::string sys_path = p + "/system.reg";
    // Eğer system.reg zaten varsa (örn. container_pattern şablonundan çıkarılmışsa), üzerine yazma!
    if (access(sys_path.c_str(), F_OK) == 0) {
        LOGI("writeRegistryEntries: system.reg zaten mevcut, korunuyor.");
        return EngineError::NONE;
    }

    // system.reg — temel Windows ayarları
    std::ofstream sys(sys_path);
    sys << "WINE REGISTRY Version 2\n\n";
    sys << "[Software\\Wine\\Drivers]\n";
    sys << "\"Audio\"=\"pulse,alsa\"\n\n";
    sys << "[Software\\Wine\\DirectInput]\n";
    sys << "\"MouseWarpOverride\"=\"disable\"\n\n";
    sys << "[Software\\Wine]\n";
    sys << "\"Version\"=\"win10\"\n";
    sys.close();

    std::string usr_path = p + "/user.reg";
    if (access(usr_path.c_str(), F_OK) != 0) {
        std::ofstream usr(usr_path);
        usr << "WINE REGISTRY Version 2\n\n";
        usr << "[Software\\Wine\\AppDefaults]\n";
        usr.close();
    }

    LOGI("writeRegistryEntries: Tamamlandı.");
    return EngineError::NONE;
}

} // namespace exelite
