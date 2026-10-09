#pragma once

#include "anyperf/types.hpp"
#include <vector>
#include <unordered_map>
#include <chrono>
#include <string>

namespace anyperf {

class ProcessMonitor {
public:
    ProcessMonitor();
    ~ProcessMonitor();

    // Enumerate active processes in the system
    static std::vector<ProcessInfo> enumerate_processes();
    static std::wstring get_process_name_by_pid(DWORD pid);

    // Dynamic Multi-Process Target Management (N >= 2)
    bool add_target(DWORD pid, const std::wstring& name = L"", bool is_primary = false);
    bool remove_target(DWORD pid);
    void clear_targets();
    void set_primary_target(DWORD pid);

    // Queries
    bool is_monitored(DWORD pid) const;
    const std::vector<ProcessMetrics>& get_monitored_targets() const { return monitored_targets_; }
    const ProcessMetrics* get_primary_target() const;
    const ProcessMetrics* get_target(DWORD pid) const;

    // Periodic telemetry update
    void update();

    // Aggregates
    double get_total_secondary_cpu() const;
    size_t get_total_secondary_ram() const;

private:
    HANDLE open_process_safe(DWORD pid);
    void sample_process(HANDLE hProcess, ProcessMetrics& metrics, uint64_t system_delta_time_100ns);
    ProcessColor assign_palette_color(size_t index);

    std::vector<ProcessMetrics> monitored_targets_;
    std::unordered_map<DWORD, HANDLE> open_handles_;

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
