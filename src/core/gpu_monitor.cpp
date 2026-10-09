#include "anyperf/gpu_monitor.hpp"
#include <dxgi1_4.h>
#include <pdhmsg.h>
#include <vector>
#include <cmath>
#include <iostream>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "pdh.lib")

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

// ADL Structs
static void* __stdcall ADL_Main_Memory_Alloc(int iSize) {
    return malloc(iSize);
}

struct AdlAdapterInfo {
    int iSize;
    int iAdapterIndex;
    char strUDID[256];
    int iBusNumber;
    int iDeviceNumber;
    int iFunctionNumber;
    int iVendorID;
    char strAdapterName[256];
    char strDisplayName[256];
    int iPresent;
    int iExist;
    char strDriverPath[256];
    char strDriverPathExt[256];
    char strPNPString[256];
    int iOSDisplayIndex;
};

GpuMonitor::GpuMonitor() {
    update_dxgi();
    init_nvml();
    init_pdh();
    init_adl();
}

GpuMonitor::~GpuMonitor() {
    if (nvmlShutdown_ && h_nvml_) {
        nvmlShutdown_();
    }
    if (h_nvml_) {
        FreeLibrary(h_nvml_);
        h_nvml_ = nullptr;
    }
    if (pdh_query_) {
        PdhCloseQuery(pdh_query_);
        pdh_query_ = nullptr;
    }
    if (h_adl_) {
        FreeLibrary(h_adl_);
        h_adl_ = nullptr;
    }
}

bool GpuMonitor::init_nvml() {
    h_nvml_ = LoadLibraryA("nvml.dll");
    if (!h_nvml_) h_nvml_ = LoadLibraryA("C:\\Windows\\System32\\nvml.dll");
    if (!h_nvml_) h_nvml_ = LoadLibraryA("C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
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

    if (nvmlDeviceGetUtilizationRates_) {
        NvmlUtilization util{};
        if (nvmlDeviceGetUtilizationRates_(nvml_device_, &util) == 0) {
            metrics_.core_usage_percent = static_cast<int>(util.gpu);
            metrics_.memory_controller_percent = static_cast<int>(util.memory);
        }
    }

    if (nvmlDeviceGetTemperature_) {
        unsigned int temp = 0;
        if (nvmlDeviceGetTemperature_(nvml_device_, 0, &temp) == 0) {
            metrics_.temperature_c = static_cast<int>(temp);
        }
    }

    if (nvmlDeviceGetMemoryInfo_) {
        NvmlMemory mem{};
        if (nvmlDeviceGetMemoryInfo_(nvml_device_, &mem) == 0) {
            metrics_.vram_used_bytes = mem.used;
            metrics_.vram_total_bytes = mem.total;
        }
    }

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

    if (nvmlDeviceGetPowerUsage_) {
        unsigned int mw = 0;
        if (nvmlDeviceGetPowerUsage_(nvml_device_, &mw) == 0) {
            metrics_.power_watts = static_cast<int>(mw / 1000);
        }
    }
}

bool GpuMonitor::init_pdh() {
    if (PdhOpenQuery(nullptr, 0, &pdh_query_) != ERROR_SUCCESS) {
        return false;
    }

    PDH_STATUS s1 = PdhAddEnglishCounterA(pdh_query_, "\\GPU Engine(*)\\Utilization Percentage", 0, &pdh_gpu_counter_);
    PDH_STATUS s2 = PdhAddEnglishCounterA(pdh_query_, "\\GPU Process Memory(*)\\Dedicated Usage", 0, &pdh_vram_counter_);

    if (s1 == ERROR_SUCCESS) {
        (void)s2;
        PdhCollectQueryData(pdh_query_);
        pdh_initialized_ = true;
        metrics_.has_pdh = true;
        return true;
    }

    return false;
}

void GpuMonitor::update_pdh() {
    if (!pdh_initialized_ || !pdh_query_) return;

    if (PdhCollectQueryData(pdh_query_) != ERROR_SUCCESS) return;

    // 1. GPU Utilization Percentage (Sum all active engine utilization)
    if (pdh_gpu_counter_) {
        DWORD bufSize = 0;
        DWORD itemCount = 0;
        PDH_STATUS status = PdhGetFormattedCounterArrayA(pdh_gpu_counter_, PDH_FMT_DOUBLE, &bufSize, &itemCount, nullptr);
        if (status == PDH_MORE_DATA && bufSize > 0) {
            std::vector<BYTE> buffer(bufSize);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_A*>(buffer.data());
            if (PdhGetFormattedCounterArrayA(pdh_gpu_counter_, PDH_FMT_DOUBLE, &bufSize, &itemCount, items) == ERROR_SUCCESS) {
                double totalLoad = 0.0;
                for (DWORD i = 0; i < itemCount; ++i) {
                    if (items[i].FmtValue.CStatus == ERROR_SUCCESS && items[i].FmtValue.doubleValue > 0.0) {
                        totalLoad += items[i].FmtValue.doubleValue;
                    }
                }
                if (totalLoad > 100.0) totalLoad = 100.0;

                // Set if not set by NVML or NVML reports 0
                if (!metrics_.has_nvml || metrics_.core_usage_percent == 0) {
                    metrics_.core_usage_percent = static_cast<int>(std::round(totalLoad));
                }
            }
        }
    }

    // 2. Dedicated VRAM In Use
    if (pdh_vram_counter_) {
        DWORD bufSize = 0;
        DWORD itemCount = 0;
        PDH_STATUS status = PdhGetFormattedCounterArrayA(pdh_vram_counter_, PDH_FMT_LARGE, &bufSize, &itemCount, nullptr);
        if (status == PDH_MORE_DATA && bufSize > 0) {
            std::vector<BYTE> buffer(bufSize);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_A*>(buffer.data());
            if (PdhGetFormattedCounterArrayA(pdh_vram_counter_, PDH_FMT_LARGE, &bufSize, &itemCount, items) == ERROR_SUCCESS) {
                unsigned long long totalVram = 0;
                for (DWORD i = 0; i < itemCount; ++i) {
                    if (items[i].FmtValue.CStatus == ERROR_SUCCESS && items[i].FmtValue.largeValue > 0) {
                        totalVram += items[i].FmtValue.largeValue;
                    }
                }
                if (!metrics_.has_nvml || metrics_.vram_used_bytes == 0) {
                    metrics_.vram_used_bytes = static_cast<size_t>(totalVram);
                }
            }
        }
    }
}

bool GpuMonitor::init_adl() {
    h_adl_ = LoadLibraryA("atiadlxx.dll");
    if (!h_adl_) h_adl_ = LoadLibraryA("atiadlxy.dll");
    if (!h_adl_) return false;

    using pfn_ADL2_Main_Control_Create = int(*)(void*(__stdcall*)(int), int, void**);
    auto pCreate = reinterpret_cast<pfn_ADL2_Main_Control_Create>(GetProcAddress(h_adl_, "ADL2_Main_Control_Create"));
    if (pCreate && pCreate(ADL_Main_Memory_Alloc, 1, &adl_context_) == 0 && adl_context_) {
        metrics_.has_adl = true;
        return true;
    }

    return false;
}

void GpuMonitor::update_adl() {
    if (!h_adl_ || !adl_context_) return;

    // Optional temperature probe via ADL2 if available
    auto pTemp = reinterpret_cast<int(*)(void*, int, int, int*)>(GetProcAddress(h_adl_, "ADL2_OverdriveN_Temperature_Get"));
    if (pTemp) {
        int tempVal = 0;
        if (pTemp(adl_context_, 0, 1, &tempVal) == 0 && tempVal > 0) {
            metrics_.temperature_c = tempVal / 1000;
        }
    }
}

void GpuMonitor::update_dxgi() {
    IDXGIFactory1* pFactory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&pFactory)))) return;

    IDXGIAdapter1* pAdapter = nullptr;
    if (SUCCEEDED(pFactory->EnumAdapters1(0, &pAdapter))) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(pAdapter->GetDesc1(&desc))) {
            metrics_.adapter_name = desc.Description;
            // Physical dedicated VRAM (e.g. 6144 MB for RX 5600 XT)
            if (desc.DedicatedVideoMemory > 0) {
                metrics_.vram_total_bytes = desc.DedicatedVideoMemory;
            }
            metrics_.is_available = true;
        }
        pAdapter->Release();
    }
    pFactory->Release();
}

void GpuMonitor::update() {
    update_dxgi();

    if (metrics_.has_nvml) {
        update_nvml();
    }

    update_pdh();

    if (metrics_.has_adl) {
        update_adl();
    }
}

} // namespace anyperf
