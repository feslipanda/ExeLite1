// ============================================================
// wine_config.h / wine_config.cpp — Wine Prefix Yapılandırması
// ============================================================
#pragma once
#include "engine_types.h"
#include <string>

namespace exelite {

class WineConfig {
public:
    static WineConfig& instance();

    // Wine prefix'i oluştur ve DXVK DLL'lerini yerleştir
    EngineError setupPrefix(const std::string& prefix_path, bool dxvk);

private:
    WineConfig() = default;
    EngineError createDirectoryStructure(const std::string& prefix_path);
    EngineError copyDxvkDlls           (const std::string& prefix_path);
    EngineError writeRegistryEntries   (const std::string& prefix_path);
};

} // namespace exelite
