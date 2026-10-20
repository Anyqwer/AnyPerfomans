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

#pragma pack(push, 1)
struct ADLOD8SingleInitSetting {
    int featureID;
    int minValue;
    int maxValue;
    int defaultValue;
};

struct ADLOD8InitSetting {
    int count;
    int overdrive8Capabilities;
    ADLOD8SingleInitSetting od8SettingTable[64];
};

struct ADLOD8CurrentSetting {
    int count;
    int Od8SettingTable[64];
};

struct ADLOD8SingleSetSetting {
    int value;
    int requested;
    int reset;
};

struct ADLOD8SetSetting {
    int count;
    ADLOD8SingleSetSetting od8SettingTable[64];
};
#pragma pack(pop)

typedef int (*pfn_ADL2_Overdrive8_Init_Setting_Get)(void*, int, ADLOD8InitSetting*);
typedef int (*pfn_ADL2_Overdrive8_Current_Setting_Get)(void*, int, ADLOD8CurrentSetting*);
typedef int (*pfn_ADL2_Overdrive8_Setting_Set)(void*, int, ADLOD8SetSetting*, ADLOD8CurrentSetting*);

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

        // Detect Overdrive 8 support and cache factory V/F defaults & safe limits
        auto pInitOD8 = (pfn_ADL2_Overdrive8_Init_Setting_Get)GetProcAddress(h_adl_, "ADL2_Overdrive8_Init_Setting_Get");
        if (pInitOD8) {
            for (int adapter = 0; adapter < 8; ++adapter) {
                ADLOD8InitSetting init_settings{};
                init_settings.count = 64;
                if (pInitOD8(adl_context_, adapter, &init_settings) == 0 && init_settings.count > 0) {
                    adl_adapter_index_ = adapter;
                    has_adl_od8_ = true;
                    for (int i = 0; i < init_settings.count && i < 64; ++i) {
                        const auto& s = init_settings.od8SettingTable[i];
                        if (i == 0) { // OD8_GFXCLK_FMAX
                            adl_od8_def_core_mhz_ = s.defaultValue;
                            adl_od8_max_core_mhz_ = s.maxValue;
                            adl_od8_min_core_mhz_ = s.minValue;
                        } else if (i == 1) { // OD8_GFXCLK_FMIN
                            adl_od8_min_core_mhz_ = s.defaultValue;
                        } else if (i == 2) { // OD8_GFXCLK_FREQ1
                            adl_od8_p1_freq_mhz_ = s.defaultValue;
                        } else if (i == 3) { // OD8_GFXCLK_VOLTAGE1
                            adl_od8_p1_volt_mv_ = s.defaultValue;
                        } else if (i == 4) { // OD8_GFXCLK_FREQ2
                            adl_od8_p2_freq_mhz_ = s.defaultValue;
                        } else if (i == 5) { // OD8_GFXCLK_VOLTAGE2
                            adl_od8_p2_volt_mv_ = s.defaultValue;
                        } else if (i == 7) { // OD8_GFXCLK_VOLTAGE3 (Peak operating voltage)
                            adl_od8_def_volt_mv_ = s.defaultValue;
                            adl_od8_min_volt_mv_ = s.minValue;
                            adl_od8_max_volt_mv_ = s.maxValue;
                        } else if (i == 8) { // OD8_UCLK_FMAX (Memory clock)
                            adl_od8_def_mem_mhz_ = s.defaultValue;
                            adl_od8_min_mem_mhz_ = s.minValue;
                            adl_od8_max_mem_mhz_ = s.maxValue;
                        } else if (i == 9) { // OD8_POWER_PERCENTAGE
                            adl_od8_min_power_pct_ = s.minValue;
                            adl_od8_max_power_pct_ = s.maxValue;
                        } else if (i == 15) { // OD8_ZERO_RPM_CONTROL
                            adl_od8_def_zero_rpm_ = s.defaultValue;
                        } else if (i >= 19 && i <= 27 && (i % 2 == 1)) {
                            int node_idx = (i - 19) / 2;
                            if (node_idx >= 0 && node_idx < 5) {
                                adl_od8_def_fan_temps_[node_idx] = s.defaultValue;
                                if (i + 1 < init_settings.count) {
                                    adl_od8_def_fan_speeds_[node_idx] = init_settings.od8SettingTable[i + 1].defaultValue;
                                }
                            }
                        }
                    }
                    break;
                }
            }
        }

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

    // 2. AMD Fan Control
    if (has_adl_ && has_adl_od8_ && adl_context_ && h_adl_) {
        // Native Overdrive 8 for Navi / RDNA (RX 5000 / 6000 series)
        auto pCurOD8 = (pfn_ADL2_Overdrive8_Current_Setting_Get)GetProcAddress(h_adl_, "ADL2_Overdrive8_Current_Setting_Get");
        auto pSetOD8 = (pfn_ADL2_Overdrive8_Setting_Set)GetProcAddress(h_adl_, "ADL2_Overdrive8_Setting_Set");
        if (pCurOD8 && pSetOD8) {
            ADLOD8CurrentSetting cur{};
            cur.count = 64;
            if (pCurOD8(adl_context_, adl_adapter_index_, &cur) == 0 && cur.count > 0) {
                ADLOD8SetSetting set_req{};
                set_req.count = 64;
                for (int i = 0; i < 64; ++i) {
                    set_req.od8SettingTable[i].value = cur.Od8SettingTable[i];
                    set_req.od8SettingTable[i].requested = 0;
                    set_req.od8SettingTable[i].reset = 0;
                }

                // Zero-RPM (index 15): If setting > 0%, disable Zero-RPM so fans spin immediately
                set_req.od8SettingTable[15].value = (speed_pct == 0) ? 1 : 0;
                set_req.od8SettingTable[15].requested = 1;

                // Fan curve speeds (indices 20, 22, 24, 26, 28)
                for (int i = 19; i <= 28; ++i) {
                    set_req.od8SettingTable[i].requested = 1;
                }
                for (int node = 0; node < 5; ++node) {
                    set_req.od8SettingTable[19 + node * 2].value = adl_od8_def_fan_temps_[node];
                    set_req.od8SettingTable[20 + node * 2].value = speed_pct;
                }

                ADLOD8CurrentSetting out_cur{};
                out_cur.count = 64;
                if (pSetOD8(adl_context_, adl_adapter_index_, &set_req, &out_cur) == 0) {
                    last_fan_speed_ = speed_pct;
                    is_manual_fan_ = true;
                    success = true;
                }
            }
        }
    } else if (has_adl_ && adl_context_ && h_adl_) {
        // Fallback for Polaris / Vega GCN cards (proper 6-field ADLODNFanControl)
        auto pSetFan = (pfn_ADL2_OverdriveN_FanControl_Set)GetProcAddress(h_adl_, "ADL2_OverdriveN_FanControl_Set");
        if (pSetFan) {
            struct ADLODNFanControl {
                int iMode;       // 1 = user defined
                int iFanSpeedPercent;
                int iMinFanLimit;
                int iTargetFanSpeed;
                int iTargetTemperature;
                int iMinPerformanceClock;
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

    // 1. NVIDIA Auto Restore via NVAPI
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

    // 2. AMD Auto Restore (VBIOS defaults)
    if (has_adl_ && has_adl_od8_ && adl_context_ && h_adl_) {
        auto pCurOD8 = (pfn_ADL2_Overdrive8_Current_Setting_Get)GetProcAddress(h_adl_, "ADL2_Overdrive8_Current_Setting_Get");
        auto pSetOD8 = (pfn_ADL2_Overdrive8_Setting_Set)GetProcAddress(h_adl_, "ADL2_Overdrive8_Setting_Set");
        if (pCurOD8 && pSetOD8) {
            ADLOD8CurrentSetting cur{};
            cur.count = 64;
            if (pCurOD8(adl_context_, adl_adapter_index_, &cur) == 0 && cur.count > 0) {
                ADLOD8SetSetting set_req{};
                set_req.count = 64;
                for (int i = 0; i < 64; ++i) {
                    set_req.od8SettingTable[i].value = cur.Od8SettingTable[i];
                    set_req.od8SettingTable[i].requested = 0;
                    set_req.od8SettingTable[i].reset = 0;
                }

                // Restore stock Zero RPM
                set_req.od8SettingTable[15].value = adl_od8_def_zero_rpm_;
                set_req.od8SettingTable[15].requested = 1;

                // Restore factory fan curve
                for (int i = 19; i <= 28; ++i) {
                    set_req.od8SettingTable[i].requested = 1;
                }
                for (int node = 0; node < 5; ++node) {
                    set_req.od8SettingTable[19 + node * 2].value = adl_od8_def_fan_temps_[node];
                    set_req.od8SettingTable[20 + node * 2].value = adl_od8_def_fan_speeds_[node];
                }

                ADLOD8CurrentSetting out_cur{};
                out_cur.count = 64;
                if (pSetOD8(adl_context_, adl_adapter_index_, &set_req, &out_cur) == 0) {
                    restored = true;
                }
            }
        }
    } else if (has_adl_ && adl_context_ && h_adl_) {
        auto pSetFan = (pfn_ADL2_OverdriveN_FanControl_Set)GetProcAddress(h_adl_, "ADL2_OverdriveN_FanControl_Set");
        if (pSetFan) {
            struct ADLODNFanControl {
                int iMode;
                int iFanSpeedPercent;
                int iMinFanLimit;
                int iTargetFanSpeed;
                int iTargetTemperature;
                int iMinPerformanceClock;
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

bool HardwareController::apply_hardware_fan_curve(const FanCurveConfig& cfg) {
    if (!cfg.enabled || cfg.mode == 0) {
        return restore_auto_fans();
    }
    if (cfg.mode == 1) {
        return set_fan_speed(cfg.fixed_speed_pct);
    }

    // Direct hardware VBIOS curve flashing on RDNA
    if (has_adl_ && has_adl_od8_ && adl_context_ && h_adl_) {
        auto pCurOD8 = (pfn_ADL2_Overdrive8_Current_Setting_Get)GetProcAddress(h_adl_, "ADL2_Overdrive8_Current_Setting_Get");
        auto pSetOD8 = (pfn_ADL2_Overdrive8_Setting_Set)GetProcAddress(h_adl_, "ADL2_Overdrive8_Setting_Set");
        if (pCurOD8 && pSetOD8) {
            ADLOD8CurrentSetting cur{};
            cur.count = 64;
            if (pCurOD8(adl_context_, adl_adapter_index_, &cur) == 0 && cur.count > 0) {
                ADLOD8SetSetting set_req{};
                set_req.count = 64;
                for (int i = 0; i < 64; ++i) {
                    set_req.od8SettingTable[i].value = cur.Od8SettingTable[i];
                    set_req.od8SettingTable[i].requested = 0;
                    set_req.od8SettingTable[i].reset = 0;
                }

                set_req.od8SettingTable[15].value = cfg.zero_rpm_enabled ? 1 : 0;
                set_req.od8SettingTable[15].requested = 1;

                for (int i = 19; i <= 28; ++i) {
                    set_req.od8SettingTable[i].requested = 1;
                }

                for (size_t node = 0; node < 5 && node < cfg.points.size(); ++node) {
                    set_req.od8SettingTable[19 + node * 2].value = std::clamp(cfg.points[node].temp_c, 25, 100);
                    set_req.od8SettingTable[20 + node * 2].value = std::clamp(cfg.points[node].fan_pct, 0, 100);
                }

                ADLOD8CurrentSetting out_cur{};
                out_cur.count = 64;
                if (pSetOD8(adl_context_, adl_adapter_index_, &set_req, &out_cur) == 0) {
                    is_manual_fan_ = true;
                    return true;
                }
            }
        }
    }
    return false;
}

void HardwareController::update_fan_curve(const FanCurveConfig& cfg, int current_gpu_temp, int current_hotspot_temp, int current_vram_temp) {
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

    // Mode 2: Multi-source custom fan curve (Edge, Hotspot, or VRAM)
    int eval_temp = current_gpu_temp;
    if (cfg.temp_source == 1 && current_hotspot_temp > 0) {
        eval_temp = current_hotspot_temp;
    } else if (cfg.temp_source == 2 && current_vram_temp > 0) {
        eval_temp = current_vram_temp;
    }

    // Zero-RPM mode: shut off fans below 45C if enabled
    if (cfg.zero_rpm_enabled && eval_temp < 45) {
        if (last_fan_speed_ != 0) {
            set_fan_speed(0);
        }
        last_fan_temp_ = eval_temp;
        return;
    }

    int target_pct = 50;
    if (cfg.points.empty()) {
        target_pct = 50;
    } else if (eval_temp <= cfg.points.front().temp_c) {
        target_pct = cfg.points.front().fan_pct;
    } else if (eval_temp >= cfg.points.back().temp_c) {
        target_pct = cfg.points.back().fan_pct;
    } else {
        for (size_t i = 0; i + 1 < cfg.points.size(); ++i) {
            if (eval_temp >= cfg.points[i].temp_c && eval_temp <= cfg.points[i + 1].temp_c) {
                double span_t = cfg.points[i + 1].temp_c - cfg.points[i].temp_c;
                double span_f = cfg.points[i + 1].fan_pct - cfg.points[i].fan_pct;
                double ratio = span_t > 0.0 ? (eval_temp - cfg.points[i].temp_c) / span_t : 0.0;
                target_pct = static_cast<int>(std::round(cfg.points[i].fan_pct + ratio * span_f));
                break;
            }
        }
    }
    target_pct = std::clamp(target_pct, 0, 100);

    // Apply hysteresis when cooling down to prevent rapid oscillatory fan spinups
    if (target_pct < last_fan_speed_ && (last_fan_temp_ - eval_temp) < cfg.hysteresis_c) {
        return;
    }

    if (target_pct != last_fan_speed_) {
        set_fan_speed(target_pct);
        last_fan_temp_ = eval_temp;
    }
}

bool HardwareController::check_and_handle_safe_mode() {
    const wchar_t* canary_file = L"anyperf_oc_canary.tmp";
    DWORD attr = GetFileAttributesW(canary_file);
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        safe_mode_engaged_ = true;
        last_oc_status_ = HardwareOcStatus::NotApplied;
        last_oc_message_ = "[SAFE MODE ENGAGED] Previous session crash or TDR detected. Overclock settings reset to factory defaults.";
        DeleteFileW(canary_file);
        reset_overclock();
        restore_auto_fans();
        return true;
    }
    return false;
}

void HardwareController::mark_startup_begin() {
    const wchar_t* canary_file = L"anyperf_oc_canary.tmp";
    HANDLE hFile = CreateFileW(canary_file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        const char* msg = "AnyPerfomans Overclock Canary\n";
        DWORD written = 0;
        WriteFile(hFile, msg, static_cast<DWORD>(strlen(msg)), &written, nullptr);
        CloseHandle(hFile);
    }
}

void HardwareController::mark_startup_stable() {
    const wchar_t* canary_file = L"anyperf_oc_canary.tmp";
    DeleteFileW(canary_file);
}

#pragma pack(push, 8)
struct NV_GPU_PSTATE20_CLOCK_ENTRY_V1 {
    unsigned int domainId;
    unsigned int typeId;
    unsigned int bIsEditable : 1;
    unsigned int reserved : 31;
    struct {
        int value;
        struct {
            int min;
            int max;
        } valueRange;
    } freqDelta_kHz;
    union {
        struct {
            unsigned int freq_kHz;
        } single;
        struct {
            unsigned int minFreq_kHz;
            unsigned int maxFreq_kHz;
            unsigned int domainId;
            unsigned int minVoltage_uV;
            unsigned int maxVoltage_uV;
        } range;
    } data;
};

struct NV_GPU_PSTATE20_BASE_VOLTAGE_ENTRY_V1 {
    unsigned int domainId;
    unsigned int bIsEditable : 1;
    unsigned int reserved : 31;
    int voltDelta_uV;
    union {
        struct {
            unsigned int volt_uV;
        } single;
    } data;
};

struct NV_GPU_PERF_PSTATES20_INFO_V1 {
    unsigned int version;
    unsigned int bIsEditable : 1;
    unsigned int reserved : 31;
    unsigned int numPstates;
    unsigned int numClocks;
    unsigned int numBaseVoltages;
    struct {
        unsigned int pstateId;
        unsigned int bIsEditable : 1;
        unsigned int reserved : 31;
        NV_GPU_PSTATE20_CLOCK_ENTRY_V1 clocks[8];
        NV_GPU_PSTATE20_BASE_VOLTAGE_ENTRY_V1 baseVoltages[4];
    } pstates[16];
};
#pragma pack(pop)

bool HardwareController::apply_overclock(const OverclockConfig& oc) {
    if (!oc.enabled) {
        return reset_overclock();
    }

    bool applied = false;

    // 1. NVIDIA Overclocking & P-States 2.0 via NVAPI
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

            // Apply Core & Memory Clock Offsets via NvAPI_GPU_SetPstates20
            auto pfnSetPstates = (int(*)(void*, void*))q(NVAPI_ID_GPU_SET_PSTATES20);
            if (pfnSetPstates) {
                NV_GPU_PERF_PSTATES20_INFO_V1 pstates{};
                pstates.version = sizeof(NV_GPU_PERF_PSTATES20_INFO_V1) | (1 << 16);
                pstates.numPstates = 1;
                pstates.numClocks = 2;
                pstates.bIsEditable = 1;

                pstates.pstates[0].pstateId = 0; // P0 performance state
                pstates.pstates[0].bIsEditable = 1;

                // Domain 0: Graphics Core Clock
                pstates.pstates[0].clocks[0].domainId = 0;
                pstates.pstates[0].clocks[0].typeId = 0; // single frequency delta
                pstates.pstates[0].clocks[0].bIsEditable = 1;
                pstates.pstates[0].clocks[0].freqDelta_kHz.value = oc.core_clock_offset_mhz * 1000;

                // Domain 4: Memory Clock
                pstates.pstates[0].clocks[1].domainId = 4;
                pstates.pstates[0].clocks[1].typeId = 0;
                pstates.pstates[0].clocks[1].bIsEditable = 1;
                pstates.pstates[0].clocks[1].freqDelta_kHz.value = oc.memory_clock_offset_mhz * 1000;

                int res = pfnSetPstates(nv_physical_gpu_, &pstates);
                if (res == 0) {
                    last_oc_status_ = HardwareOcStatus::FullyApplied;
                    last_oc_message_ = "NVIDIA NVAPI: Clock offsets and power limits applied successfully.";
                    applied = true;
                } else {
                    last_oc_status_ = HardwareOcStatus::PowerLimitOnly;
                    last_oc_message_ = "NVIDIA NVAPI: Power limit applied. P-States returned code " + std::to_string(res);
                    applied = true;
                }
            }
        }
    }

    // 2. AMD Overdrive 8 (Navi / RDNA) Hardware Overclock & Undervolt
    if (has_adl_ && has_adl_od8_ && adl_context_ && h_adl_) {
        auto pCurOD8 = (pfn_ADL2_Overdrive8_Current_Setting_Get)GetProcAddress(h_adl_, "ADL2_Overdrive8_Current_Setting_Get");
        auto pSetOD8 = (pfn_ADL2_Overdrive8_Setting_Set)GetProcAddress(h_adl_, "ADL2_Overdrive8_Setting_Set");

        if (pCurOD8 && pSetOD8) {
            ADLOD8CurrentSetting cur{};
            cur.count = 64;
            if (pCurOD8(adl_context_, adl_adapter_index_, &cur) == 0 && cur.count > 0) {
                ADLOD8SetSetting set_req{};
                set_req.count = 64;
                for (int i = 0; i < 64; ++i) {
                    set_req.od8SettingTable[i].value = cur.Od8SettingTable[i];
                    set_req.od8SettingTable[i].requested = 0;
                    set_req.od8SettingTable[i].reset = 0;
                }

                // Compute target frequencies & voltage based on factory defaults + user offset or custom curve
                int p1_f = oc.custom_vf_enabled ? std::clamp(oc.p1_freq_mhz, adl_od8_min_core_mhz_, adl_od8_max_core_mhz_) : adl_od8_p1_freq_mhz_;
                int p1_v = oc.custom_vf_enabled ? std::clamp(oc.p1_volt_mv, adl_od8_min_volt_mv_, adl_od8_max_volt_mv_) : adl_od8_p1_volt_mv_;
                int p2_f = oc.custom_vf_enabled ? std::clamp(oc.p2_freq_mhz, adl_od8_min_core_mhz_, adl_od8_max_core_mhz_) : adl_od8_p2_freq_mhz_;
                int p2_v = oc.custom_vf_enabled ? std::clamp(oc.p2_volt_mv, adl_od8_min_volt_mv_, adl_od8_max_volt_mv_) : adl_od8_p2_volt_mv_;
                int target_core = oc.custom_vf_enabled ? std::clamp(oc.p3_freq_mhz, adl_od8_min_core_mhz_, adl_od8_max_core_mhz_)
                                                       : std::clamp(adl_od8_def_core_mhz_ + oc.core_clock_offset_mhz, adl_od8_min_core_mhz_, adl_od8_max_core_mhz_);
                int target_volt = oc.custom_vf_enabled ? std::clamp(oc.p3_volt_mv, adl_od8_min_volt_mv_, adl_od8_max_volt_mv_)
                                                       : std::clamp(adl_od8_def_volt_mv_ + oc.voltage_offset_mv, adl_od8_min_volt_mv_, adl_od8_max_volt_mv_);
                int target_mem = std::clamp(adl_od8_def_mem_mhz_ + oc.memory_clock_offset_mhz, adl_od8_min_mem_mhz_, adl_od8_max_mem_mhz_);
                int power_offset = std::clamp(oc.power_limit_pct - 100, adl_od8_min_power_pct_, adl_od8_max_power_pct_);

                // Feature 1: GFXCLK Fmax & Fmin
                set_req.od8SettingTable[0].value = target_core;
                set_req.od8SettingTable[0].requested = 1;
                set_req.od8SettingTable[1].value = adl_od8_min_core_mhz_;
                set_req.od8SettingTable[1].requested = 1;

                // Feature 2: 3-point V/F curve (All 6 points must be requested together)
                for (int i = 2; i <= 7; ++i) {
                    set_req.od8SettingTable[i].requested = 1;
                }
                set_req.od8SettingTable[2].value = p1_f;
                set_req.od8SettingTable[3].value = p1_v;
                set_req.od8SettingTable[4].value = p2_f;
                set_req.od8SettingTable[5].value = p2_v;
                set_req.od8SettingTable[6].value = target_core;
                set_req.od8SettingTable[7].value = target_volt;

                // Feature 4: Memory clock
                set_req.od8SettingTable[8].value = target_mem;
                set_req.od8SettingTable[8].requested = 1;

                // Feature 8: Power Limit offset %
                set_req.od8SettingTable[9].value = power_offset;
                set_req.od8SettingTable[9].requested = 1;

                ADLOD8CurrentSetting out_cur{};
                out_cur.count = 64;
                int res = pSetOD8(adl_context_, adl_adapter_index_, &set_req, &out_cur);
                if (res == 0) {
                    last_oc_status_ = HardwareOcStatus::FullyApplied;
                    char buf[256];
                    snprintf(buf, sizeof(buf), "AMD Overdrive 8: Applied Core %d MHz, Mem %d MHz, Peak V/F %d mV, Power %+d%%.",
                             out_cur.Od8SettingTable[0], out_cur.Od8SettingTable[8], out_cur.Od8SettingTable[7], out_cur.Od8SettingTable[9]);
                    last_oc_message_ = buf;
                    applied = true;
                } else {
                    last_oc_status_ = HardwareOcStatus::PowerLimitOnly;
                    last_oc_message_ = "AMD Overdrive 8: Set failed with error code " + std::to_string(res);
                }
            }
        }
    } else if (has_adl_ && adl_context_ && h_adl_) {
        // Fallback for older Polaris/Vega GPUs via OverdriveN
        auto pSetPower = (pfn_ADL2_OverdriveN_PowerLimit_Set)GetProcAddress(h_adl_, "ADL2_OverdriveN_PowerLimit_Set");
        if (pSetPower) {
            int power_percent = oc.power_limit_pct - 100; // e.g. +10%
            int ret = pSetPower(adl_context_, adl_adapter_index_, &power_percent);
            if (ret == 0) {
                last_oc_status_ = HardwareOcStatus::PowerLimitOnly;
                last_oc_message_ = "AMD ADL (OverdriveN): Power limit applied.";
                applied = true;
            }
        }
    }

    return applied;
}

bool HardwareController::reset_overclock() {
    bool reset_done = false;

    if (has_nvapi_ && nv_physical_gpu_ && h_nvapi_) {
        auto q = (pfn_nvapi_QueryInterface)GetProcAddress(h_nvapi_, "nvapi_QueryInterface");
        if (q) {
            // 1. Reset Power Limit to 100%
            struct NV_GPU_POWER_LIMIT_ENTRY { unsigned int limitValue; };
            struct NV_GPU_POWER_LIMITS {
                unsigned int version;
                unsigned int flags;
                NV_GPU_POWER_LIMIT_ENTRY entries[4];
            } pl{};
            pl.version = sizeof(NV_GPU_POWER_LIMITS) | (1 << 16);
            pl.entries[0].limitValue = 100000; // 100%
            auto pfnSetPower = (int(*)(void*, void*))q(NVAPI_ID_GPU_POWER_SET_LIMITS);
            if (pfnSetPower) pfnSetPower(nv_physical_gpu_, &pl);

            // 2. Reset Clock Offsets to 0
            auto pfnSetPstates = (int(*)(void*, void*))q(NVAPI_ID_GPU_SET_PSTATES20);
            if (pfnSetPstates) {
                NV_GPU_PERF_PSTATES20_INFO_V1 pstates{};
                pstates.version = sizeof(NV_GPU_PERF_PSTATES20_INFO_V1) | (1 << 16);
                pstates.numPstates = 1;
                pstates.numClocks = 2;
                pstates.bIsEditable = 1;
                pstates.pstates[0].pstateId = 0;
                pstates.pstates[0].bIsEditable = 1;

                pstates.pstates[0].clocks[0].domainId = 0;
                pstates.pstates[0].clocks[0].bIsEditable = 1;
                pstates.pstates[0].clocks[0].freqDelta_kHz.value = 0;

                pstates.pstates[0].clocks[1].domainId = 4;
                pstates.pstates[0].clocks[1].bIsEditable = 1;
                pstates.pstates[0].clocks[1].freqDelta_kHz.value = 0;

                pfnSetPstates(nv_physical_gpu_, &pstates);
            }
            reset_done = true;
        }
    }

    if (has_adl_ && has_adl_od8_ && adl_context_ && h_adl_) {
        auto pCurOD8 = (pfn_ADL2_Overdrive8_Current_Setting_Get)GetProcAddress(h_adl_, "ADL2_Overdrive8_Current_Setting_Get");
        auto pSetOD8 = (pfn_ADL2_Overdrive8_Setting_Set)GetProcAddress(h_adl_, "ADL2_Overdrive8_Setting_Set");
        if (pCurOD8 && pSetOD8) {
            ADLOD8CurrentSetting cur{};
            cur.count = 64;
            if (pCurOD8(adl_context_, adl_adapter_index_, &cur) == 0 && cur.count > 0) {
                ADLOD8SetSetting set_req{};
                set_req.count = 64;
                for (int i = 0; i < 64; ++i) {
                    set_req.od8SettingTable[i].value = cur.Od8SettingTable[i];
                    set_req.od8SettingTable[i].requested = 0;
                    set_req.od8SettingTable[i].reset = 0;
                }

                // Restore factory defaults
                for (int i = 0; i <= 9; ++i) {
                    set_req.od8SettingTable[i].requested = 1;
                }
                set_req.od8SettingTable[0].value = adl_od8_def_core_mhz_;
                set_req.od8SettingTable[1].value = adl_od8_min_core_mhz_;
                set_req.od8SettingTable[2].value = adl_od8_p1_freq_mhz_;
                set_req.od8SettingTable[3].value = adl_od8_p1_volt_mv_;
                set_req.od8SettingTable[4].value = adl_od8_p2_freq_mhz_;
                set_req.od8SettingTable[5].value = adl_od8_p2_volt_mv_;
                set_req.od8SettingTable[6].value = adl_od8_def_core_mhz_;
                set_req.od8SettingTable[7].value = adl_od8_def_volt_mv_;
                set_req.od8SettingTable[8].value = adl_od8_def_mem_mhz_;
                set_req.od8SettingTable[9].value = 0; // 0% power limit offset

                ADLOD8CurrentSetting out_cur{};
                out_cur.count = 64;
                pSetOD8(adl_context_, adl_adapter_index_, &set_req, &out_cur);
                reset_done = true;
            }
        }
    } else if (has_adl_ && adl_context_ && h_adl_) {
        auto pSetPower = (pfn_ADL2_OverdriveN_PowerLimit_Set)GetProcAddress(h_adl_, "ADL2_OverdriveN_PowerLimit_Set");
        if (pSetPower) {
            int zero_offset = 0;
            pSetPower(adl_context_, adl_adapter_index_, &zero_offset);
            reset_done = true;
        }
    }

    last_oc_status_ = HardwareOcStatus::NotApplied;
    last_oc_message_ = "Hardware clock offsets and power limits reset to factory VBIOS defaults.";
    return reset_done;
}

} // namespace anyperf
