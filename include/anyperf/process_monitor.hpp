#pragma once

#include "anyperf/types.hpp"
#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>

namespace anyperf {

class ProcessMonitor {
public:
    ProcessMonitor();
    ~ProcessMonitor();

    // Enumerate active processes in the system
    static std::vector<ProcessInfo> enumerate_processes();

    // Set target processes
    void set_game_pid(DWORD pid);
    void set_aux_pid(DWORD pid);

    // Auto-search for cs2.exe
    bool auto_detect_game();

    // Sample telemetry (call periodically, e.g. every 100ms - 500ms)
    void update();

    // Get current metrics
    const ProcessMetrics& get_game_metrics() const { return game_metrics_; }
    const ProcessMetrics& get_aux_metrics() const { return aux_metrics_; }

    DWORD get_game_pid() const { return game_pid_; }
    DWORD get_aux_pid() const { return aux_pid_; }

private:
    void sample_process(DWORD pid, HANDLE hProcess, ProcessMetrics& metrics, uint64_t system_delta_time_100ns);
    HANDLE open_process_safe(DWORD pid);

    DWORD game_pid_ = 0;
    DWORD aux_pid_ = 0;

    HANDLE h_game_ = nullptr;
    HANDLE h_aux_ = nullptr;

    ProcessMetrics game_metrics_;
    ProcessMetrics aux_metrics_;

    // Previous system times for CPU % calculation
    FILETIME prev_idle_time_{};
    FILETIME prev_kernel_time_{};
    FILETIME prev_user_time_{};

    // Previous process times
    std::unordered_map<DWORD, FILETIME> prev_proc_kernel_;
    std::unordered_map<DWORD, FILETIME> prev_proc_user_;
    std::unordered_map<DWORD, uint64_t> prev_proc_cycles_;

    std::chrono::steady_clock::time_point last_sample_time_;
};

} // namespace anyperf
