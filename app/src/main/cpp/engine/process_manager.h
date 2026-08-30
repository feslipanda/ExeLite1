// ============================================================
// process_manager.h — Wine + Box64 Process Yönetimi
// ============================================================
#pragma once

#include "engine_types.h"
#include <string>

namespace exelite {

struct LaunchResult {
    bool        success   { false };
    int32_t     wine_pid  { -1 };
    int32_t     box64_pid { -1 };
    std::string error_msg;
};

class ProcessManager {
public:
    static ProcessManager& instance();

    LaunchResult launchExe(
        const std::string& exe_path,
        const std::string& working_dir,
        const std::string& internal_files_dir,
        const std::string& wine_prefix,
        int64_t            max_memory_mb
    );

    bool    isRunning() const;
    void    kill();
    int64_t getMemoryUsageMb() const;

private:
    ProcessManager() = default;

    int32_t wine_pid_  { -1 };
    int32_t box64_pid_ { -1 };
};

} // namespace exelite
