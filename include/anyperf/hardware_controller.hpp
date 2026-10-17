#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include "anyperf/config_manager.hpp"

namespace anyperf {

class HardwareController {
public:
    HardwareController();
    ~HardwareController();

    bool init();
    void shutdown();

    bool is_nvidia() const { return has_nvapi_; }
    bool is_amd() const { return has_adl_; }

    // Fan Control
    // speed_pct: 0 - 100, or -1 to restore Automatic VBIOS default
    bool set_fan_speed(int speed_pct);
    bool restore_auto_fans();
    bool apply_hardware_fan_curve(const FanCurveConfig& cfg);

    // Fan Curve evaluation loop (call every 500ms - 1000ms)
    // Evaluates selected temperature source (Core, Hotspot, or VRAM)
    void update_fan_curve(const FanCurveConfig& cfg, int current_gpu_temp, int current_hotspot_temp = 0, int current_vram_temp = 0);

    // Overclocking / Undervolting
    enum class HardwareOcStatus {
        NotApplied,
        FullyApplied,
        PowerLimitOnly,
        Unsupported
    };

    bool apply_overclock(const OverclockConfig& oc);
    bool reset_overclock();

    // TDR Crash Watchdog & Safe Mode
    bool check_and_handle_safe_mode();
    void mark_startup_begin();
    void mark_startup_stable();
    bool is_safe_mode_engaged() const { return safe_mode_engaged_; }

    HardwareOcStatus get_last_oc_status() const { return last_oc_status_; }
    const std::string& get_last_oc_message() const { return last_oc_message_; }

    int get_current_fan_speed_pct() const { return last_fan_speed_; }
    bool is_fan_manual() const { return is_manual_fan_; }

    // AMD Overdrive 8 Capabilities & Defaults
    bool has_amd_od8() const { return has_adl_od8_; }
    int get_amd_def_core_mhz() const { return adl_od8_def_core_mhz_; }
    int get_amd_min_core_mhz() const { return adl_od8_min_core_mhz_; }
    int get_amd_max_core_mhz() const { return adl_od8_max_core_mhz_; }
    int get_amd_def_mem_mhz() const { return adl_od8_def_mem_mhz_; }
    int get_amd_min_mem_mhz() const { return adl_od8_min_mem_mhz_; }
    int get_amd_max_mem_mhz() const { return adl_od8_max_mem_mhz_; }
    int get_amd_def_volt_mv() const { return adl_od8_def_volt_mv_; }
    int get_amd_min_volt_mv() const { return adl_od8_min_volt_mv_; }
    int get_amd_max_volt_mv() const { return adl_od8_max_volt_mv_; }
    int get_amd_max_power_pct() const { return adl_od8_max_power_pct_; }

private:
    bool init_nvapi();
    bool init_adl();

    // NVIDIA NVAPI handles & function pointers
    HMODULE h_nvapi_ = nullptr;
    void* nv_physical_gpu_ = nullptr;
    bool has_nvapi_ = false;

    // AMD ADL handles & function pointers
    HMODULE h_adl_ = nullptr;
    void* adl_context_ = nullptr;
    int adl_adapter_index_ = -1;
    bool has_adl_ = false;

    // AMD Overdrive 8 cached limits & factory defaults
    bool has_adl_od8_ = false;
    int adl_od8_def_core_mhz_ = 1780;
    int adl_od8_min_core_mhz_ = 800;
    int adl_od8_max_core_mhz_ = 1820;
    int adl_od8_def_mem_mhz_ = 1750;
    int adl_od8_min_mem_mhz_ = 1750;
    int adl_od8_max_mem_mhz_ = 1860;
    int adl_od8_def_volt_mv_ = 1008;
    int adl_od8_min_volt_mv_ = 800;
    int adl_od8_max_volt_mv_ = 1050;
    int adl_od8_p1_freq_mhz_ = 800;
    int adl_od8_p1_volt_mv_ = 695;
    int adl_od8_p2_freq_mhz_ = 1290;
    int adl_od8_p2_volt_mv_ = 776;
    int adl_od8_min_power_pct_ = -50;
    int adl_od8_max_power_pct_ = 10;
    int adl_od8_def_fan_temps_[5] = { 30, 50, 58, 67, 75 };
    int adl_od8_def_fan_speeds_[5] = { 25, 34, 43, 52, 74 };
    int adl_od8_def_zero_rpm_ = 1;

    // Fan Loop State
    int last_fan_speed_ = -1;
    int last_fan_temp_ = 0;
    bool is_manual_fan_ = false;

    // Safe Mode State
    bool safe_mode_engaged_ = false;

    // Overclocking State
    HardwareOcStatus last_oc_status_ = HardwareOcStatus::NotApplied;
    std::string last_oc_message_ = "No active overclock profile applied.";
};

} // namespace anyperf
