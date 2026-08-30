# ============================================================
# ExeLite Engine — Ana CMakeLists.txt
# ARM64 Android için Box64 + Wine JNI Bridge
# ============================================================
cmake_minimum_required(VERSION 3.22.1)
project(exelite_engine CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Optimizasyon Bayrakları ──────────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -O2 -fvisibility=hidden")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -fvisibility=hidden")
endif()

# ── Kaynak Dosyaları ─────────────────────────────────────────
set(ENGINE_SOURCES
    engine/exelite_jni.cpp        # JNI giriş noktası
    engine/engine_core.cpp         # Motor yönetimi
    engine/process_manager.cpp     # Wine/Box64 process yönetimi
    engine/input_bridge.cpp        # Gamepad → DirectInput/XInput köprüsü
    engine/render_bridge.cpp       # SDL2/SurfaceView render köprüsü
    engine/wine_config.cpp         # Wine prefix yapılandırması
    bridge/jni_utils.cpp           # JNI yardımcı fonksiyonlar
)

# ── Shared Library (Android .so) ─────────────────────────────
add_library(
    exelite_engine
    SHARED
    ${ENGINE_SOURCES}
)

# ── Android Sistem Kütüphaneleri ─────────────────────────────
find_library(log-lib         log)
find_library(android-lib     android)
find_library(EGL-lib         EGL)
find_library(GLESv3-lib      GLESv3)

target_link_libraries(
    exelite_engine
    ${log-lib}
    ${android-lib}
    ${EGL-lib}
    ${GLESv3-lib}
)

# ── Include Paths ─────────────────────────────────────────────
target_include_directories(exelite_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge
    ${CMAKE_CURRENT_SOURCE_DIR}/box64/src
    ${CMAKE_CURRENT_SOURCE_DIR}/wine/src
)

# ── Derleme Tanımlamaları ─────────────────────────────────────
target_compile_definitions(exelite_engine PRIVATE
    ANDROID_PLATFORM=1
    ARM64_TARGET=1
    EXELITE_VERSION="0.1.0"
)

# ── Box64 Alt Projesi (Sonra eklenecek - şimdi stub) ─────────
# Gerçek Box64 entegrasyonu için:
# add_subdirectory(box64)
# target_link_libraries(exelite_engine box64)

# ── Winlator XServer Native Library ──────────────────────────
add_subdirectory(winlator)

# ── wineserver_wrapper: /tmp bind-mount yapan ARM64 native yürütücü ──
add_executable(wineserver_wrapper
    engine/wineserver_wrapper.c
)
# Strip symbols for smaller binary and rename to .so for packaging
set_target_properties(wineserver_wrapper PROPERTIES
    LINK_FLAGS "-Wl,--strip-all"
    OUTPUT_NAME "wineserver_wrapper"
    PREFIX "lib"
    SUFFIX ".so"
)

# ── libfaketmp.so: Bionic linker LD_PRELOAD /tmp rewrite hook ──
add_library(faketmp SHARED engine/faketmp.c)
target_link_libraries(faketmp dl)
set_target_properties(faketmp PROPERTIES
    LINK_FLAGS "-Wl,--strip-all"
    OUTPUT_NAME "faketmp"
    PREFIX "lib"
    SUFFIX ".so"
)
