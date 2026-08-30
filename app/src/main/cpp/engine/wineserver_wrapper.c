// wineserver_wrapper.c
// ARM64 native binary olarak derlenir.
// box64 tarafından wineserver yerine çağrılır.
// Görevi: kendi mount namespace'inde /tmp'yi yazılabilir yapar,
// sonra gerçek wineserver'ı box64 ile başlatır.
//
// Derleme (CMakeLists.txt'e eklendi):
//   add_executable(wineserver_wrapper wineserver_wrapper.c)
//   target_link_libraries(wineserver_wrapper log)

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sched.h>
#include <errno.h>

#define LOGI(...) dprintf(STDOUT_FILENO, "[ExeLite.WSW] " __VA_ARGS__); dprintf(STDOUT_FILENO, "\n")
#define LOGE(...) dprintf(STDERR_FILENO, "[ExeLite.WSW] ERROR: " __VA_ARGS__); dprintf(STDERR_FILENO, "\n")

int main(int argc, char *argv[], char *envp[]) {
    // Ortam değişkenlerinden paths al
    const char* wine_prefix        = getenv("WINEPREFIX");
    const char* box64_bin          = getenv("BOX64_BIN");
    const char* wineserver         = getenv("REAL_WINESERVER");
    const char* internal_files_dir = getenv("INTERNAL_FILES_DIR");

    if (!wine_prefix || !box64_bin || !wineserver) {
        LOGE("Eksik env var: WINEPREFIX=%s BOX64_BIN=%s REAL_WINESERVER=%s",
             wine_prefix ? wine_prefix : "NULL",
             box64_bin   ? box64_bin   : "NULL",
             wineserver  ? wineserver  : "NULL");
        return 1;
    }

    // /tmp için fake klasör oluştur (X11 ve SHM socket'lerinin bulunduğu internal_files_dir/tmp)
    char fake_tmp[512];
    if (internal_files_dir) {
        snprintf(fake_tmp, sizeof(fake_tmp), "%s/tmp", internal_files_dir);
    } else {
        snprintf(fake_tmp, sizeof(fake_tmp), "%s/../tmp", wine_prefix);
    }
    mkdir(fake_tmp, 0777);
    LOGI("fake_tmp: %s", fake_tmp);

    // Kendi mount namespace'ini oluştur (root gerekmez, Android 8+)
    if (unshare(CLONE_NEWNS) != 0) {
        LOGE("unshare(CLONE_NEWNS) başarısız: %s (errno=%d)", strerror(errno), errno);
        // Namespace olmadan devam et (muhtemelen başarısız olacak ama dene)
    } else {
        // Root mount'u private yap
        mount("none", "/", NULL, MS_REC | MS_PRIVATE, NULL);

        // fake_tmp -> /tmp bind mount
        if (mount(fake_tmp, "/tmp", NULL, MS_BIND, NULL) == 0) {
            LOGI("/tmp bind mount basarili: %s -> /tmp", fake_tmp);
        } else {
            LOGE("/tmp bind mount BASARISIZ: %s (errno=%d)", strerror(errno), errno);
        }
    }

    // gerçek wineserver'ı box64 ile çalıştır
    // argv[0] = wineserver_wrapper, geri kalanı wineserver'a ilet
    char* new_argv[256];
    new_argv[0] = (char*)box64_bin;
    new_argv[1] = (char*)wineserver;
    // wineserver'a gelen tüm argümanları ilet (genellikle boş)
    for (int i = 1; i < argc && i < 254; i++) {
        new_argv[i + 1] = argv[i];
    }
    new_argv[argc + 1] = NULL;

    LOGI("exec: %s %s ...", box64_bin, wineserver);
    execve(box64_bin, new_argv, envp);

    // execve başarısız
    LOGE("execve başarısız: %s", strerror(errno));
    return 1;
}
