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

    // Fan Curve evaluation loop (call every 500ms - 1000ms)
    void update_fan_curve(const FanCurveConfig& cfg, int current_gpu_temp);

    // Overclocking / Undervolting
    bool apply_overclock(const OverclockConfig& oc);
    bool reset_overclock();

    int get_current_fan_speed_pct() const { return last_fan_speed_; }
    bool is_fan_manual() const { return is_manual_fan_; }

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

    // Fan Loop State
    int last_fan_speed_ = -1;
    int last_fan_temp_ = 0;
    bool is_manual_fan_ = false;
};

} // namespace anyperf
