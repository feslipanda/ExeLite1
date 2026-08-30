// ============================================================
// render_bridge.cpp — Wine → Android SurfaceView Render
//
// X11 protokolü üzerinden Wine'ın pencere çıkışını alıp
// Android SurfaceView'a aktarır.
//
// İki mod desteklenir:
// 1) Shared Memory (SHM): Wine'ın framebuffer'ını doğrudan okur
// 2) VirPipe: Virtio-GPU benzeri render pipeline (gelecek)
//
// Şu an: ANativeWindow pixel buffer üzerinden yazma modu.
// ============================================================
#include "render_bridge.h"

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <cstring>
#include <algorithm>
#include <thread>
#include <atomic>
#include <chrono>

#ifndef __NR_memfd_create
  #if defined(__aarch64__)
    #define __NR_memfd_create 279
  #elif defined(__arm__)
    #define __NR_memfd_create 385
  #elif defined(__x86_64__)
    #define __NR_memfd_create 319
  #elif defined(__i386__)
    #define __NR_memfd_create 356
  #endif
#endif

#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002U
#endif
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

#define LOG_TAG "ExeLite.Render"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

namespace exelite {

// ── Shared Memory Framebuffer ─────────────────────────────────
// Wine, X11 server üzerinden framebuffer'ı paylaşımlı belleğe yazar.
// Biz bu belleği okuyup ANativeWindow'a kopyalıyoruz.
static const char*   SHM_NAME       = "/exelite_framebuffer";
static int           g_shm_fd       = -1;
static uint8_t*      g_shm_ptr      = nullptr;
static size_t        g_shm_size     = 0;

// Render thread
static std::thread         g_render_thread;
static std::atomic<bool>   g_render_running { false };

// FPS hesaplama
static std::atomic<int32_t> g_fps_counter  { 0 };
static std::atomic<int32_t> g_current_fps  { 0 };

// ── Shared Memory Oluştur ─────────────────────────────────────
static bool createSharedMemory(int32_t width, int32_t height) {
    // RGBA (4 byte/piksel) + 16 byte header (width, height, frame_counter, flags)
    g_shm_size = (size_t)(width * height * 4) + 16;

#if defined(__NR_memfd_create)
    g_shm_fd = syscall(__NR_memfd_create, "exelite_framebuffer", MFD_ALLOW_SEALING);
#endif
    if (g_shm_fd < 0) {
        g_shm_fd = open("/dev/ashmem", O_RDWR);
    }
    if (g_shm_fd < 0) {
        LOGE("createSharedMemory: memfd_create / ashmem başarısız");
        return false;
    }

    if (ftruncate(g_shm_fd, (off_t)g_shm_size) < 0) {
        LOGE("createSharedMemory: ftruncate başarısız");
        close(g_shm_fd);
        g_shm_fd = -1;
        return false;
    }

    g_shm_ptr = (uint8_t*)mmap(nullptr, g_shm_size,
                                PROT_READ | PROT_WRITE,
                                MAP_SHARED, g_shm_fd, 0);
    if (g_shm_ptr == MAP_FAILED) {
        LOGE("createSharedMemory: mmap başarısız");
        close(g_shm_fd);
        g_shm_fd  = -1;
        g_shm_ptr = nullptr;
        return false;
    }

    // Header yaz: [0-3]=width, [4-7]=height, [8-11]=frame_counter, [12-15]=flags
    memcpy(g_shm_ptr + 0,  &width,  4);
    memcpy(g_shm_ptr + 4,  &height, 4);
    int32_t zero = 0;
    memcpy(g_shm_ptr + 8,  &zero,   4); // frame_counter
    memcpy(g_shm_ptr + 12, &zero,   4); // flags

    LOGI("createSharedMemory: %dx%d (%zu bytes)", width, height, g_shm_size);
    return true;
}

// ── Render Thread ─────────────────────────────────────────────
// Framebuffer'ı SHM'den okuyup ANativeWindow'a kopyalar
static void renderLoop(ANativeWindow* window, int32_t width, int32_t height) {
    LOGI("renderLoop: Başladı — %dx%d", width, height);

    ANativeWindow_setBuffersGeometry(window, width, height, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);

    auto fps_start = std::chrono::steady_clock::now();
    int32_t last_frame = -1;

    while (g_render_running.load()) {
        // SHM'den frame counter oku
        int32_t frame_counter = 0;
        if (g_shm_ptr) {
            memcpy(&frame_counter, g_shm_ptr + 8, 4);
        }

        // Yeni frame yoksa bekle (CPU tasarrufu)
        if (frame_counter == last_frame) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            continue;
        }
        last_frame = frame_counter;

        // ANativeWindow buffer'ı kilitle
        ANativeWindow_Buffer buffer;
        if (ANativeWindow_lock(window, &buffer, nullptr) != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // SHM framebuffer → ANativeWindow kopyala
        if (g_shm_ptr) {
            const uint8_t* src = g_shm_ptr + 16; // Header'ı atla
            uint8_t*       dst = (uint8_t*)buffer.bits;
            int32_t src_stride = width * 4;
            int32_t dst_stride = buffer.stride * 4;

            for (int y = 0; y < height && y < buffer.height; ++y) {
                memcpy(dst + y * dst_stride,
                       src + y * src_stride,
                       std::min(src_stride, dst_stride));
            }
        }

        ANativeWindow_unlockAndPost(window);
        g_fps_counter++;

        // FPS hesapla (her saniye)
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - fps_start);
        if (elapsed.count() >= 1) {
            g_current_fps.store(g_fps_counter.load());
            g_fps_counter = 0;
            fps_start = now;
        }
    }

    LOGI("renderLoop: Bitti.");
}

// ── Singleton ───────────────────────────────────────────────
RenderBridge& RenderBridge::instance() {
    static RenderBridge inst;
    return inst;
}

EngineError RenderBridge::initialize(int32_t width, int32_t height) {
    width_  = width;
    height_ = height;
    LOGI("initialize: %dx%d", width, height);

    // Shared memory oluştur
    if (!createSharedMemory(width, height)) {
        LOGE("initialize: SHM oluşturulamadı — fallback modda devam");
        // SHM olmadan da devam edebiliriz, sadece siyah ekran olur
    }

    return EngineError::NONE;
}

void RenderBridge::setWindow(ANativeWindow* window) {
    // Mevcut render thread'i durdur
    if (g_render_running.load()) {
        g_render_running = false;
        if (g_render_thread.joinable()) {
            g_render_thread.join();
        }
    }

    // Eski pencereyi serbest bırak
    if (window_ && window_ != window) {
        ANativeWindow_release(window_);
    }

    window_ = window;

    if (window) {
        LOGI("setWindow: Render thread başlatılıyor");
        g_render_running = true;
        g_render_thread = std::thread(renderLoop, window, width_, height_);
    } else {
        LOGI("setWindow: Pencere kapatıldı");
    }
}

void RenderBridge::destroy() {
    g_render_running = false;
    if (g_render_thread.joinable()) {
        g_render_thread.join();
    }

    if (g_shm_ptr && g_shm_ptr != MAP_FAILED) {
        munmap(g_shm_ptr, g_shm_size);
        g_shm_ptr = nullptr;
    }
    if (g_shm_fd >= 0) {
        close(g_shm_fd);
        g_shm_fd = -1;
    }

    if (window_) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }

    LOGI("destroy: Render temizlendi.");
}

int32_t RenderBridge::getCurrentFps() const {
    return g_current_fps.load();
}

} // namespace exelite
