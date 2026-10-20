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
    init_dxgi();
    init_nvml();
    init_adl();
    if (!metrics_.has_nvml && !metrics_.has_adl) {
        init_pdh();
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
    if (pdh_query_) {
        PdhCloseQuery(pdh_query_);
        pdh_query_ = nullptr;
    }
    if (h_adl_) {
        FreeLibrary(h_adl_);
        h_adl_ = nullptr;
    }
    if (cached_dxgi_adapter3_) {
        reinterpret_cast<IDXGIAdapter3*>(cached_dxgi_adapter3_)->Release();
        cached_dxgi_adapter3_ = nullptr;
    }
    if (cached_dxgi_factory_) {
        cached_dxgi_factory_->Release();
        cached_dxgi_factory_ = nullptr;
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
    nvmlDeviceGetFieldValues_ = reinterpret_cast<pfn_nvmlDeviceGetFieldValues>(GetProcAddress(h_nvml_, "nvmlDeviceGetFieldValues"));

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
    metrics_.is_nvidia = true;
    metrics_.is_available = true;
    return true;
}

struct NvmlFieldValue {
    unsigned int fieldId;
    unsigned int scopeId;
    long long timestamp;
    long long latencyUsec;
    int valueType;
    int nvmlSt;
    union {
        double dVal;
        unsigned int uiVal;
        unsigned long long ullVal;
        signed long long sllVal;
        char str[256];
    } value;
};

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

    // Extended Sensor queries via NVML Field Values (Field ID 82 = Memory Temp, 83 = Hotspot Temp)
    if (nvmlDeviceGetFieldValues_) {
        NvmlFieldValue fields[3]{};
        fields[0].fieldId = 82; // NVML_FI_DEV_MEMORY_TEMP
        fields[1].fieldId = 81; // NVML_FI_DEV_GPU_TEMP
        fields[2].fieldId = 83; // NVML_FI_DEV_GPU_HOTSPOT_TEMP
        if (nvmlDeviceGetFieldValues_(nvml_device_, 3, fields) == 0) {
            if (fields[0].nvmlSt == 0 && fields[0].value.sllVal > 0 && fields[0].value.sllVal < 150) {
                metrics_.vram_temp_c = static_cast<int>(fields[0].value.sllVal);
            }
            if (fields[2].nvmlSt == 0 && fields[2].value.sllVal > 0 && fields[2].value.sllVal < 150) {
                metrics_.hotspot_temp_c = static_cast<int>(fields[2].value.sllVal);
                has_hw_hotspot_ = true;
            }
        }
    }

    // Hotspot temperature: only report if a genuine hardware sensor was returned by NVML / ADL PMLog.
    // Never simulate or estimate hotspot from core load.
    if (!has_hw_hotspot_) {
        metrics_.hotspot_temp_c = 0;
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
    if (!pCreate || pCreate(ADL_Main_Memory_Alloc, 1, &adl_context_) != 0 || !adl_context_) {
        return false;
    }

    adl_pmlog_query_ = reinterpret_cast<pfn_ADL2_New_QueryPMLogData_Get>(
        GetProcAddress(h_adl_, "ADL2_New_QueryPMLogData_Get")
    );

    using pfn_ADL2_Adapter_NumberOfAdapters_Get = int(*)(void*, int*);
    using pfn_ADL2_Adapter_AdapterInfo_Get = int(*)(void*, void*, int);
    auto pNumAdapters = reinterpret_cast<pfn_ADL2_Adapter_NumberOfAdapters_Get>(GetProcAddress(h_adl_, "ADL2_Adapter_NumberOfAdapters_Get"));
    auto pAdapterInfo = reinterpret_cast<pfn_ADL2_Adapter_AdapterInfo_Get>(GetProcAddress(h_adl_, "ADL2_Adapter_AdapterInfo_Get"));

    adl_adapter_index_ = 0;
    if (pNumAdapters && pAdapterInfo) {
        int numAdapters = 0;
        if (pNumAdapters(adl_context_, &numAdapters) == 0 && numAdapters > 0) {
            std::vector<AdlAdapterInfo> adapters(numAdapters);
            if (pAdapterInfo(adl_context_, adapters.data(), static_cast<int>(sizeof(AdlAdapterInfo) * numAdapters)) == 0) {
                for (int i = 0; i < numAdapters; ++i) {
                    if (adapters[i].iPresent) {
                        adl_adapter_index_ = adapters[i].iAdapterIndex;
                        break;
                    }
                }
            }
        }
    }

    metrics_.has_adl = true;
    metrics_.is_amd = true;
    metrics_.is_available = true;
    return true;
}

void GpuMonitor::update_adl() {
    if (!h_adl_ || !adl_context_) return;

    int adapter_idx = (adl_adapter_index_ >= 0) ? adl_adapter_index_ : 0;

    // 1. High-speed ADL PMLog query (Radeon RX 5000 / 6000 / 7000 Series, RDNA 1/2/3)
    if (adl_pmlog_query_) {
        struct ADLPMLogDataOutput {
            int size;
            struct { int supported; int value; } sensors[256];
        } pmLog{};
        pmLog.size = sizeof(pmLog);

        if (adl_pmlog_query_(adl_context_, adapter_idx, &pmLog) == 0) {
            // Sensor 1: Core Clock (MHz)
            if (pmLog.sensors[1].supported && pmLog.sensors[1].value > 0) {
                metrics_.core_clock_mhz = pmLog.sensors[1].value;
            }
            // Sensor 2: Memory Clock (MHz)
            if (pmLog.sensors[2].supported && pmLog.sensors[2].value > 0) {
                metrics_.memory_clock_mhz = pmLog.sensors[2].value;
            }
            // Sensor 8: Edge / Core Temperature (C)
            if (pmLog.sensors[8].supported && pmLog.sensors[8].value > 0) {
                metrics_.temperature_c = pmLog.sensors[8].value;
            }
            // Sensor 27 / 9: Junction / Hotspot Temperature (C)
            if (pmLog.sensors[27].supported && pmLog.sensors[27].value > 0) {
                metrics_.hotspot_temp_c = pmLog.sensors[27].value;
                has_hw_hotspot_ = true;
            } else if (pmLog.sensors[9].supported && pmLog.sensors[9].value > 0) {
                metrics_.hotspot_temp_c = pmLog.sensors[9].value;
                has_hw_hotspot_ = true;
            }
            // Sensor 10: Memory / VRAM Temperature (C)
            if (pmLog.sensors[10].supported && pmLog.sensors[10].value > 0) {
                metrics_.vram_temp_c = pmLog.sensors[10].value;
            }
            // Sensor 19: Core Load (%)
            if (pmLog.sensors[19].supported) {
                metrics_.core_usage_percent = pmLog.sensors[19].value;
            }
            // Sensor 20: Memory Controller Load (%)
            if (pmLog.sensors[20].supported) {
                metrics_.memory_controller_percent = pmLog.sensors[20].value;
            }
            // Sensor 21: GFX Core Voltage (mV) - Official AMD ADL_PMLOG_GFX_VOLTAGE
            if (pmLog.sensors[21].supported && pmLog.sensors[21].value > 0) {
                metrics_.voltage_mv = pmLog.sensors[21].value;
            }
            // Sensor 23: Board / ASIC Power (Watts)
            if (pmLog.sensors[23].supported && pmLog.sensors[23].value > 0) {
                metrics_.power_watts = pmLog.sensors[23].value;
            }
            // Sensor 25: Fan Speed (RPM)
            if (pmLog.sensors[25].supported && pmLog.sensors[25].value > 0) {
                metrics_.fan_speed_rpm = pmLog.sensors[25].value;
            }
            // Sensor 26: Fan Speed (%)
            if (pmLog.sensors[26].supported && pmLog.sensors[26].value >= 0) {
                metrics_.fan_speed_percent = pmLog.sensors[26].value;
            }
            return;
        }
    }

    // 2. Fallback for legacy GCN / OverdriveN cards
    auto pTemp = reinterpret_cast<int(*)(void*, int, int, int*)>(GetProcAddress(h_adl_, "ADL2_OverdriveN_Temperature_Get"));
    if (pTemp) {
        int tempVal = 0;
        if (pTemp(adl_context_, adapter_idx, 1, &tempVal) == 0 && tempVal > 0) {
            metrics_.temperature_c = tempVal / 1000;
        }
        tempVal = 0;
        if (pTemp(adl_context_, adapter_idx, 2, &tempVal) == 0 && tempVal > 0) {
            metrics_.vram_temp_c = tempVal / 1000;
        }
        tempVal = 0;
        if (pTemp(adl_context_, adapter_idx, 3, &tempVal) == 0 && tempVal > 0) {
            metrics_.hotspot_temp_c = tempVal / 1000;
            has_hw_hotspot_ = true;
        }
    }
}

void GpuMonitor::init_dxgi() {
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&cached_dxgi_factory_)))) return;

    IDXGIAdapter1* pAdapter = nullptr;
    if (SUCCEEDED(cached_dxgi_factory_->EnumAdapters1(0, &pAdapter))) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(pAdapter->GetDesc1(&desc))) {
            metrics_.adapter_name = desc.Description;
            if (desc.DedicatedVideoMemory > 0) {
                metrics_.vram_total_bytes = desc.DedicatedVideoMemory;
            }
            metrics_.is_available = true;
        }
        IDXGIAdapter3* pAdapter3 = nullptr;
        if (SUCCEEDED(pAdapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&pAdapter3)))) {
            cached_dxgi_adapter3_ = pAdapter3;
        }
        pAdapter->Release();
    }
}

void GpuMonitor::update_vram_dxgi() {
    if (cached_dxgi_adapter3_) {
        auto* pAdapter3 = reinterpret_cast<IDXGIAdapter3*>(cached_dxgi_adapter3_);
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if (SUCCEEDED(pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
            metrics_.vram_used_bytes = static_cast<size_t>(info.CurrentUsage);
            if (info.Budget > 0 && metrics_.vram_total_bytes == 0) {
                metrics_.vram_total_bytes = static_cast<size_t>(info.Budget);
            }
        }
    }
}

void GpuMonitor::update() {
    update_vram_dxgi();

    if (metrics_.has_nvml) {
        update_nvml();
    } else if (metrics_.has_adl) {
        update_adl();
    } else if (metrics_.has_pdh) {
        update_pdh();
    }
}

} // namespace anyperf
