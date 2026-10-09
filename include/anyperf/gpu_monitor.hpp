#pragma once

#include <windows.h>
#include <pdh.h>
#include <string>
#include <cstdint>

namespace anyperf {

struct GpuMetrics {
    std::wstring adapter_name = L"Generic GPU";
    bool is_available = false;
    bool has_nvml = false;
    bool has_pdh = false;
    bool has_adl = false;

    // Core Metrics
    int core_usage_percent = 0;        // GPU Core load % (0-100)
    int memory_controller_percent = 0; // VRAM controller load %
    int temperature_c = 0;             // Core temperature in °C

    // Memory (VRAM)
    size_t vram_used_bytes = 0;        // Live VRAM in use (bytes)
    size_t vram_total_bytes = 0;       // Physical Dedicated VRAM (bytes)

    // Clocks & Power
    int core_clock_mhz = 0;
    int memory_clock_mhz = 0;
    int power_watts = 0;
};

class GpuMonitor {
public:
    GpuMonitor();
    ~GpuMonitor();

    // Query GPU telemetry (call every 250ms - 500ms)
    void update();

    const GpuMetrics& get_metrics() const { return metrics_; }
    bool is_nvml_loaded() const { return metrics_.has_nvml; }
    bool is_amd_adl_loaded() const { return metrics_.has_adl; }
    bool is_pdh_active() const { return metrics_.has_pdh; }

private:
    bool init_nvml();
    void update_nvml();

    bool init_pdh();
    void update_pdh();

    bool init_adl();
    void update_adl();

    void update_dxgi();

    GpuMetrics metrics_;

    // NVIDIA NVML
    HMODULE h_nvml_ = nullptr;
    void* nvml_device_ = nullptr;

    using pfn_nvmlInit_v2 = int(*)();
    using pfn_nvmlShutdown = int(*)();
    using pfn_nvmlDeviceGetHandleByIndex_v2 = int(*)(unsigned int, void**);
    using pfn_nvmlDeviceGetName = int(*)(void*, char*, unsigned int);
    using pfn_nvmlDeviceGetUtilizationRates = int(*)(void*, void*);
    using pfn_nvmlDeviceGetTemperature = int(*)(void*, int, unsigned int*);
    using pfn_nvmlDeviceGetMemoryInfo = int(*)(void*, void*);
    using pfn_nvmlDeviceGetClockInfo = int(*)(void*, int, unsigned int*);
    using pfn_nvmlDeviceGetPowerUsage = int(*)(void*, unsigned int*);

    pfn_nvmlInit_v2 nvmlInit_ = nullptr;
    pfn_nvmlShutdown nvmlShutdown_ = nullptr;
    pfn_nvmlDeviceGetHandleByIndex_v2 nvmlDeviceGetHandleByIndex_ = nullptr;
    pfn_nvmlDeviceGetName nvmlDeviceGetName_ = nullptr;
    pfn_nvmlDeviceGetUtilizationRates nvmlDeviceGetUtilizationRates_ = nullptr;
    pfn_nvmlDeviceGetTemperature nvmlDeviceGetTemperature_ = nullptr;
    pfn_nvmlDeviceGetMemoryInfo nvmlDeviceGetMemoryInfo_ = nullptr;
    pfn_nvmlDeviceGetClockInfo nvmlDeviceGetClockInfo_ = nullptr;
    pfn_nvmlDeviceGetPowerUsage nvmlDeviceGetPowerUsage_ = nullptr;

    // Windows PDH Universal GPU Engine Counters
    PDH_HQUERY pdh_query_ = nullptr;
    PDH_HCOUNTER pdh_gpu_counter_ = nullptr;
    PDH_HCOUNTER pdh_vram_counter_ = nullptr;
    bool pdh_initialized_ = false;

    // AMD Display Library (ADL)
    HMODULE h_adl_ = nullptr;
    void* adl_context_ = nullptr;
    int adl_adapter_index_ = -1;
};

} // namespace anyperf
