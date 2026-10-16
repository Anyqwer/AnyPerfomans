#pragma once

#include <windows.h>
#include <vector>
#include <cstdint>
#include <string>

namespace anyperf {

struct CpuMetrics {
    int package_temp_c = 0;              // CPU Package / ACPI Temperature in °C
    int package_power_w = 0;             // CPU Package Power (Watts)
    double total_usage_percent = 0.0;    // Overall CPU load % (0 - 100)
    
    // Per-core detailed telemetry
    std::vector<double> per_core_usage;  // Usage % for each logical core [Core 0, Core 1, ...]
    int max_core_index = 0;              // Index of the most heavily loaded core
    double max_core_usage = 0.0;         // Utilization % of that most loaded core
    
    std::wstring cpu_model_name;         // e.g. "AMD Ryzen 5 5600X" or "Intel Core i7-13700K"
    uint32_t logical_core_count = 0;
};

class CpuMonitor {
public:
    CpuMonitor();
    ~CpuMonitor();

    // Sample per-core utilization and hardware thermal sensors (call every 250 - 500ms)
    void update();

    const CpuMetrics& get_metrics() const { return metrics_; }

private:
    void init_cpu_info();
    void update_per_core_usage();
    void update_thermal_and_power();

    CpuMetrics metrics_;

    // Native NT API for microsecond-precision per-core metrics
    typedef struct _SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION {
        LARGE_INTEGER IdleTime;
        LARGE_INTEGER KernelTime;
        LARGE_INTEGER UserTime;
        LARGE_INTEGER DpcTime;
        LARGE_INTEGER InterruptTime;
        ULONG         InterruptCount;
    } SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION;

    typedef NTSTATUS(WINAPI* pfnNtQuerySystemInformation)(
        ULONG SystemInformationClass,
        PVOID SystemInformation,
        ULONG SystemInformationLength,
        PULONG ReturnLength
    );

    pfnNtQuerySystemInformation pfn_NtQuerySystemInformation_ = nullptr;
    std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> prev_core_perf_;
    bool has_initial_sample_ = false;

    // Thermal queries
    bool wmi_initialized_ = false;
    void* wmi_services_ = nullptr;
    void* wmi_locator_ = nullptr;
    bool init_wmi();
    void query_wmi_temperature();
    void cleanup_wmi();
};

} // namespace anyperf
