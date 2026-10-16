#pragma once

#include "anyperf/types.hpp"
#include "anyperf/gpu_monitor.hpp"
#include "anyperf/config_manager.hpp"
#include "anyperf/process_monitor.hpp"
#include <windows.h>
#include <string>
#include <vector>

namespace anyperf {

class RTSSBridge {
public:
    RTSSBridge();
    ~RTSSBridge();

    // Connection lifecycle
    bool connect();
    void disconnect();
    bool is_connected() const { return is_connected_; }

    // Send formatted telemetry to RTSS On-Screen Display
    bool update_osd(
        const ProcessMonitor& proc_mon,
        const GpuMetrics& gpu,
        const FrameStats& frames,
        const ConfigManager& config_mgr,
        bool is_benchmarking,
        double benchmark_elapsed_sec
    );

    // Clear and release RTSS OSD slot
    void clear_osd();

    // Query active 3D foreground app detected by RTSS (if any)
    DWORD get_foreground_process_id() const { return foreground_pid_; }
    std::string get_foreground_process_name() const { return foreground_name_; }

private:
    bool write_to_shared_memory(const char* text);

    HANDLE h_map_file_ = nullptr;
    void* map_addr_ = nullptr;
    bool is_connected_ = false;

    DWORD foreground_pid_ = 0;
    std::string foreground_name_;
};

} // namespace anyperf
