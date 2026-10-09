#include "anyperf/gpu_monitor.hpp"
#include <dxgi1_4.h>
#include <iostream>

#pragma comment(lib, "dxgi.lib")

namespace anyperf {

struct NvmlUtilization {
    unsigned int gpu;
    unsigned int memory;
};

struct NvmlMemory {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

GpuMonitor::GpuMonitor() {
    if (!init_nvml()) {
        update_dxgi_fallback();
    }
}

GpuMonitor::~GpuMonitor() {
    if (nvmlShutdown_ && h_nvml_) {
        nvmlShutdown_();
    }
    if (h_nvml_) {
        FreeLibrary(h_nvml_);
        h_nvml_ = nullptr;
    }
}

bool GpuMonitor::init_nvml() {
    h_nvml_ = LoadLibraryA("nvml.dll");
    if (!h_nvml_) {
        h_nvml_ = LoadLibraryA("C:\\Windows\\System32\\nvml.dll");
    }
    if (!h_nvml_) {
        h_nvml_ = LoadLibraryA("C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
    }

    if (!h_nvml_) return false;

    nvmlInit_ = reinterpret_cast<pfn_nvmlInit_v2>(GetProcAddress(h_nvml_, "nvmlInit_v2"));
    nvmlShutdown_ = reinterpret_cast<pfn_nvmlShutdown>(GetProcAddress(h_nvml_, "nvmlShutdown"));
    nvmlDeviceGetHandleByIndex_ = reinterpret_cast<pfn_nvmlDeviceGetHandleByIndex_v2>(GetProcAddress(h_nvml_, "nvmlDeviceGetHandleByIndex_v2"));
    nvmlDeviceGetName_ = reinterpret_cast<pfn_nvmlDeviceGetName>(GetProcAddress(h_nvml_, "nvmlDeviceGetName"));
    nvmlDeviceGetUtilizationRates_ = reinterpret_cast<pfn_nvmlDeviceGetUtilizationRates>(GetProcAddress(h_nvml_, "nvmlDeviceGetUtilizationRates"));
    nvmlDeviceGetTemperature_ = reinterpret_cast<pfn_nvmlDeviceGetTemperature>(GetProcAddress(h_nvml_, "nvmlDeviceGetTemperature"));
    nvmlDeviceGetMemoryInfo_ = reinterpret_cast<pfn_nvmlDeviceGetMemoryInfo>(GetProcAddress(h_nvml_, "nvmlDeviceGetMemoryInfo"));
    nvmlDeviceGetClockInfo_ = reinterpret_cast<pfn_nvmlDeviceGetClockInfo>(GetProcAddress(h_nvml_, "nvmlDeviceGetClockInfo"));
    nvmlDeviceGetPowerUsage_ = reinterpret_cast<pfn_nvmlDeviceGetPowerUsage>(GetProcAddress(h_nvml_, "nvmlDeviceGetPowerUsage"));

    if (!nvmlInit_ || !nvmlDeviceGetHandleByIndex_ || nvmlInit_() != 0) {
        FreeLibrary(h_nvml_);
        h_nvml_ = nullptr;
        return false;
    }

    if (nvmlDeviceGetHandleByIndex_(0, &nvml_device_) != 0) {
        return false;
    }

    char nameBuffer[128] = {};
    if (nvmlDeviceGetName_ && nvmlDeviceGetName_(nvml_device_, nameBuffer, sizeof(nameBuffer)) == 0) {
        int wLen = MultiByteToWideChar(CP_UTF8, 0, nameBuffer, -1, nullptr, 0);
        if (wLen > 0) {
            std::wstring wName(wLen - 1, 0);
            MultiByteToWideChar(CP_UTF8, 0, nameBuffer, -1, &wName[0], wLen);
            metrics_.adapter_name = wName;
        }
    }

    metrics_.has_nvml = true;
    metrics_.is_available = true;
    return true;
}

void GpuMonitor::update_nvml() {
    if (!nvml_device_) return;

    // 1. Core and Memory Controller Utilization
    if (nvmlDeviceGetUtilizationRates_) {
        NvmlUtilization util{};
        if (nvmlDeviceGetUtilizationRates_(nvml_device_, &util) == 0) {
            metrics_.core_usage_percent = static_cast<int>(util.gpu);
            metrics_.memory_controller_percent = static_cast<int>(util.memory);
        }
    }

    // 2. Temperature (Sensor 0 = GPU Core)
    if (nvmlDeviceGetTemperature_) {
        unsigned int temp = 0;
        if (nvmlDeviceGetTemperature_(nvml_device_, 0, &temp) == 0) {
            metrics_.temperature_c = static_cast<int>(temp);
        }
    }

    // 3. VRAM Memory Info
    if (nvmlDeviceGetMemoryInfo_) {
        NvmlMemory mem{};
        if (nvmlDeviceGetMemoryInfo_(nvml_device_, &mem) == 0) {
            metrics_.vram_used_bytes = mem.used;
            metrics_.vram_total_bytes = mem.total;
        }
    }

    // 4. Clocks (0 = Graphics / SM, 2 = Memory)
    if (nvmlDeviceGetClockInfo_) {
        unsigned int clock_core = 0;
        if (nvmlDeviceGetClockInfo_(nvml_device_, 0, &clock_core) == 0) {
            metrics_.core_clock_mhz = static_cast<int>(clock_core);
        }
        unsigned int clock_mem = 0;
        if (nvmlDeviceGetClockInfo_(nvml_device_, 2, &clock_mem) == 0) {
            metrics_.memory_clock_mhz = static_cast<int>(clock_mem);
        }
    }

    // 5. Power Usage in milliwatts -> Watts
    if (nvmlDeviceGetPowerUsage_) {
        unsigned int mw = 0;
        if (nvmlDeviceGetPowerUsage_(nvml_device_, &mw) == 0) {
            metrics_.power_watts = static_cast<int>(mw / 1000);
        }
    }
}

void GpuMonitor::update_dxgi_fallback() {
    IDXGIFactory1* pFactory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&pFactory)))) return;

    IDXGIAdapter1* pAdapter = nullptr;
    if (SUCCEEDED(pFactory->EnumAdapters1(0, &pAdapter))) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(pAdapter->GetDesc1(&desc))) {
            metrics_.adapter_name = desc.Description;
            metrics_.vram_total_bytes = desc.DedicatedVideoMemory;
            metrics_.is_available = true;
        }

        // Check Windows 10/11 IDXGIAdapter3 for real-time dedicated video memory usage
        IDXGIAdapter3* pAdapter3 = nullptr;
        if (SUCCEEDED(pAdapter->QueryInterface(IID_PPV_ARGS(&pAdapter3)))) {
            DXGI_QUERY_VIDEO_MEMORY_INFO memInfo{};
            if (SUCCEEDED(pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memInfo))) {
                metrics_.vram_used_bytes = memInfo.CurrentUsage;
                if (memInfo.Budget > 0) {
                    metrics_.vram_total_bytes = memInfo.Budget;
                }
            }
            pAdapter3->Release();
        }

        pAdapter->Release();
    }
    pFactory->Release();
}

void GpuMonitor::update() {
    if (metrics_.has_nvml) {
        update_nvml();
    } else {
        update_dxgi_fallback();
    }
}

} // namespace anyperf
