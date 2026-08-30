// ============================================================
// render_bridge.h / render_bridge.cpp
// Wine → Android SurfaceView Render Köprüsü
// ============================================================
#pragma once
#include "engine_types.h"
#include <android/native_window.h>

namespace exelite {

class RenderBridge {
public:
    static RenderBridge& instance();

    EngineError initialize(int32_t width, int32_t height);
    void        setWindow(ANativeWindow* window);
    void        destroy();
    int32_t     getCurrentFps() const;

private:
    RenderBridge() = default;
    ANativeWindow* window_       { nullptr };
    int32_t        width_        { 1280 };
    int32_t        height_       { 720  };
    int32_t        current_fps_  { 0    };
};

} // namespace exelite
