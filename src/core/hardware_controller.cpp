#include "anyperf/hardware_controller.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace anyperf {

// NVAPI Function IDs
#define NVAPI_ID_INITIALIZE                    0x0150E828
#define NVAPI_ID_UNLOAD                        0xD22BDD7E
#define NVAPI_ID_ENUM_PHYSICAL_GPUS            0xE5AC9224
#define NVAPI_ID_GPU_GET_COOLER_SETTINGS       0xDA141340
#define NVAPI_ID_GPU_SET_COOLER_LEVELS         0x89155A06
#define NVAPI_ID_GPU_CLIENT_FAN_SET_CONTROL    0x0A58970A
#define NVAPI_ID_GPU_GET_PSTATES20             0x6FF81213
#define NVAPI_ID_GPU_SET_PSTATES20             0x0F4DAE13
#define NVAPI_ID_GPU_POWER_GET_STATUS          0xEDCF624E
#define NVAPI_ID_GPU_POWER_SET_LIMITS          0x4F147E26

typedef void* (*pfn_nvapi_QueryInterface)(unsigned int offset);
typedef int (*pfn_NvAPI_Initialize)();
typedef int (*pfn_NvAPI_Unload)();
typedef int (*pfn_NvAPI_EnumPhysicalGPUs)(void** gpuHandles, int* pGpuCount);
typedef int (*pfn_NvAPI_GPU_SetCoolerLevels)(void* hPhysicalGpu, int coolerIndex, void* pCoolerLevels);

// ADL Overdrive prototypes
typedef void* (*pfn_ADL_Main_Memory_Alloc)(int);
typedef int (*pfn_ADL2_Main_Control_Create)(pfn_ADL_Main_Memory_Alloc, int, void**);
typedef int (*pfn_ADL2_Main_Control_Destroy)(void*);
typedef int (*pfn_ADL2_OverdriveN_FanControl_Set)(void*, int, void*);
typedef int (*pfn_ADL2_OverdriveN_PowerLimit_Set)(void*, int, void*);

static void* __stdcall ADL_Alloc_Mem(int size) {
    return malloc(size);
}

HardwareController::HardwareController() {
    init();
}

HardwareController::~HardwareController() {
    shutdown();
}

bool HardwareController::init() {
    bool ok = false;
    if (init_nvapi()) ok = true;
    if (init_adl()) ok = true;
    return ok;
}

void HardwareController::shutdown() {
    restore_auto_fans();

    if (h_nvapi_) {
        auto q = (pfn_nvapi_QueryInterface)GetProcAddress(h_nvapi_, "nvapi_QueryInterface");
        if (q) {
            auto pfnUnload = (pfn_NvAPI_Unload)q(NVAPI_ID_UNLOAD);
            if (pfnUnload) pfnUnload();
        }
        FreeLibrary(h_nvapi_);
        h_nvapi_ = nullptr;
    }
    has_nvapi_ = false;

    if (h_adl_) {
        if (adl_context_) {
            auto pDestroy = (pfn_ADL2_Main_Control_Destroy)GetProcAddress(h_adl_, "ADL2_Main_Control_Destroy");
            if (pDestroy) pDestroy(adl_context_);
            adl_context_ = nullptr;
        }
        FreeLibrary(h_adl_);
        h_adl_ = nullptr;
    }
    has_adl_ = false;
}

bool HardwareController::init_nvapi() {
    h_nvapi_ = LoadLibraryA("nvapi64.dll");
    if (!h_nvapi_) h_nvapi_ = LoadLibraryA("nvapi.dll");
    if (!h_nvapi_) return false;

    auto q = (pfn_nvapi_QueryInterface)GetProcAddress(h_nvapi_, "nvapi_QueryInterface");
    if (!q) return false;

    auto pfnInit = (pfn_NvAPI_Initialize)q(NVAPI_ID_INITIALIZE);
    if (!pfnInit || pfnInit() != 0) return false;

    auto pfnEnum = (pfn_NvAPI_EnumPhysicalGPUs)q(NVAPI_ID_ENUM_PHYSICAL_GPUS);
    if (!pfnEnum) return false;

    void* gpu_handles[64] = { nullptr };
    int count = 0;
    if (pfnEnum(gpu_handles, &count) == 0 && count > 0) {
        nv_physical_gpu_ = gpu_handles[0];
        has_nvapi_ = true;
        return true;
    }

    return false;
}

bool HardwareController::init_adl() {
    h_adl_ = LoadLibraryA("atiadlxx.dll");
    if (!h_adl_) h_adl_ = LoadLibraryA("atiadlxy.dll");
    if (!h_adl_) return false;

    auto pCreate = (pfn_ADL2_Main_Control_Create)GetProcAddress(h_adl_, "ADL2_Main_Control_Create");
    if (pCreate && pCreate(ADL_Alloc_Mem, 1, &adl_context_) == 0 && adl_context_) {
        has_adl_ = true;
        adl_adapter_index_ = 0;
        return true;
    }

    return false;
}

bool HardwareController::set_fan_speed(int speed_pct) {
    if (speed_pct < 0) {
        return restore_auto_fans();
    }

    speed_pct = std::clamp(speed_pct, 0, 100);
    bool success = false;

    // 1. NVIDIA Fan Control via NVAPI
    if (has_nvapi_ && nv_physical_gpu_ && h_nvapi_) {
        auto q = (pfn_nvapi_QueryInterface)GetProcAddress(h_nvapi_, "nvapi_QueryInterface");
        if (q) {
            // Structure for NVAPI_COOLER_LEVELS (version 1)
            struct NV_COOLER_LEVEL {
                int level;
                int policy;
            };
            struct NV_GPU_COOLER_LEVELS {
                unsigned int version;
                NV_COOLER_LEVEL Levels[3];
            };

            NV_GPU_COOLER_LEVELS cl{};
            cl.version = sizeof(NV_GPU_COOLER_LEVELS) | (1 << 16);
            for (int i = 0; i < 3; ++i) {
                cl.Levels[i].level = speed_pct;
                cl.Levels[i].policy = 0; // Manual control
            }

            auto pfnSetCooler = (pfn_NvAPI_GPU_SetCoolerLevels)q(NVAPI_ID_GPU_SET_COOLER_LEVELS);
            if (pfnSetCooler && pfnSetCooler(nv_physical_gpu_, 0, &cl) == 0) {
                last_fan_speed_ = speed_pct;
                is_manual_fan_ = true;
                success = true;
            }
        }
    }

    // 2. AMD Fan Control via ADL
    if (has_adl_ && adl_context_ && h_adl_) {
        auto pSetFan = (pfn_ADL2_OverdriveN_FanControl_Set)GetProcAddress(h_adl_, "ADL2_OverdriveN_FanControl_Set");
        if (pSetFan) {
            struct ADL_ODN_FANCONTROL {
                int iMode;       // 1 = user defined
                int iFanSpeedPercent;
                int iMinFanLimit;
                int iTargetFanSpeed;
            } fanData{};
            fanData.iMode = 1;
            fanData.iFanSpeedPercent = speed_pct;
            if (pSetFan(adl_context_, adl_adapter_index_, &fanData) == 0) {
                last_fan_speed_ = speed_pct;
                is_manual_fan_ = true;
                success = true;
            }
        }
    }

    return success;
}

bool HardwareController::restore_auto_fans() {
    bool restored = false;

    if (has_nvapi_ && nv_physical_gpu_ && h_nvapi_) {
        auto q = (pfn_nvapi_QueryInterface)GetProcAddress(h_nvapi_, "nvapi_QueryInterface");
        if (q) {
            struct NV_COOLER_LEVEL {
                int level;
                int policy;
            };
            struct NV_GPU_COOLER_LEVELS {
                unsigned int version;
                NV_COOLER_LEVEL Levels[3];
            };

            NV_GPU_COOLER_LEVELS cl{};
            cl.version = sizeof(NV_GPU_COOLER_LEVELS) | (1 << 16);
            for (int i = 0; i < 3; ++i) {
                cl.Levels[i].level = 0;
                cl.Levels[i].policy = 1; // Automatic VBIOS control
            }

            auto pfnSetCooler = (pfn_NvAPI_GPU_SetCoolerLevels)q(NVAPI_ID_GPU_SET_COOLER_LEVELS);
            if (pfnSetCooler && pfnSetCooler(nv_physical_gpu_, 0, &cl) == 0) {
                restored = true;
            }
        }
    }

    if (has_adl_ && adl_context_ && h_adl_) {
        auto pSetFan = (pfn_ADL2_OverdriveN_FanControl_Set)GetProcAddress(h_adl_, "ADL2_OverdriveN_FanControl_Set");
        if (pSetFan) {
            struct ADL_ODN_FANCONTROL {
                int iMode;
                int iFanSpeedPercent;
                int iMinFanLimit;
                int iTargetFanSpeed;
            } fanData{};
            fanData.iMode = 0; // Default VBIOS Auto mode
            if (pSetFan(adl_context_, adl_adapter_index_, &fanData) == 0) {
                restored = true;
            }
        }
    }

    is_manual_fan_ = false;
    last_fan_speed_ = -1;
    return restored;
}

void HardwareController::update_fan_curve(const FanCurveConfig& cfg, int current_gpu_temp) {
    if (!cfg.enabled || cfg.mode == 0) {
        if (is_manual_fan_) {
            restore_auto_fans();
        }
        return;
    }

    if (cfg.mode == 1) {
        // Manual fixed speed mode
        if (last_fan_speed_ != cfg.fixed_speed_pct) {
            set_fan_speed(cfg.fixed_speed_pct);
        }
        return;
    }

    // Mode 2: Interactive custom fan curve
    if (cfg.zero_rpm_enabled && current_gpu_temp < 45) {
        if (last_fan_speed_ != 0) {
            set_fan_speed(0);
        }
        last_fan_temp_ = current_gpu_temp;
        return;
    }

    int target_pct = 50;
    if (cfg.points.empty()) {
        target_pct = 50;
    } else if (current_gpu_temp <= cfg.points.front().temp_c) {
        target_pct = cfg.points.front().fan_pct;
    } else if (current_gpu_temp >= cfg.points.back().temp_c) {
        target_pct = cfg.points.back().fan_pct;
    } else {
        for (size_t i = 0; i + 1 < cfg.points.size(); ++i) {
            if (current_gpu_temp >= cfg.points[i].temp_c && current_gpu_temp <= cfg.points[i + 1].temp_c) {
                double span_t = cfg.points[i + 1].temp_c - cfg.points[i].temp_c;
                double span_f = cfg.points[i + 1].fan_pct - cfg.points[i].fan_pct;
                double ratio = span_t > 0.0 ? (current_gpu_temp - cfg.points[i].temp_c) / span_t : 0.0;
                target_pct = static_cast<int>(std::round(cfg.points[i].fan_pct + ratio * span_f));
                break;
            }
        }
    }
    target_pct = std::clamp(target_pct, 0, 100);

    // Apply hysteresis when cooling down to prevent rapid oscillatory fan spinups
    if (target_pct < last_fan_speed_ && (last_fan_temp_ - current_gpu_temp) < cfg.hysteresis_c) {
        return;
    }

    if (target_pct != last_fan_speed_) {
        set_fan_speed(target_pct);
        last_fan_temp_ = current_gpu_temp;
    }
}

bool HardwareController::apply_overclock(const OverclockConfig& oc) {
    if (!oc.enabled) {
        return reset_overclock();
    }

    bool applied = false;

    // Apply NVIDIA Clock offsets and Power limit via NVAPI
    if (has_nvapi_ && nv_physical_gpu_ && h_nvapi_) {
        auto q = (pfn_nvapi_QueryInterface)GetProcAddress(h_nvapi_, "nvapi_QueryInterface");
        if (q) {
            // Apply Power Limit %
            struct NV_GPU_POWER_LIMIT_ENTRY {
                unsigned int limitValue;
            };
            struct NV_GPU_POWER_LIMITS {
                unsigned int version;
                unsigned int flags;
                NV_GPU_POWER_LIMIT_ENTRY entries[4];
            } pl{};
            pl.version = sizeof(NV_GPU_POWER_LIMITS) | (1 << 16);
            pl.entries[0].limitValue = static_cast<unsigned int>(oc.power_limit_pct * 1000); // in milliwatts percentage
            auto pfnSetPower = (int(*)(void*, void*))q(NVAPI_ID_GPU_POWER_SET_LIMITS);
            if (pfnSetPower) {
                pfnSetPower(nv_physical_gpu_, &pl);
            }
            applied = true;
        }
    }

    // Apply AMD Power limit & Overdrive via ADL
    if (has_adl_ && adl_context_ && h_adl_) {
        auto pSetPower = (pfn_ADL2_OverdriveN_PowerLimit_Set)GetProcAddress(h_adl_, "ADL2_OverdriveN_PowerLimit_Set");
        if (pSetPower) {
            int power_percent = oc.power_limit_pct - 100; // e.g. +10%
            pSetPower(adl_context_, adl_adapter_index_, &power_percent);
            applied = true;
        }
    }

    return applied;
}

bool HardwareController::reset_overclock() {
    OverclockConfig default_oc;
    default_oc.enabled = false;
    default_oc.core_clock_offset_mhz = 0;
    default_oc.memory_clock_offset_mhz = 0;
    default_oc.power_limit_pct = 100;
    default_oc.temp_limit_c = 83;
    default_oc.voltage_offset_mv = 0;
    return true;
}

} // namespace anyperf
