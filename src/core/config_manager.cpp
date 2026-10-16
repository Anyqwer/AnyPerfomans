#include "anyperf/config_manager.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace anyperf {

void ConfigManager::init_default_sensors(std::vector<SensorConfigItem>& sensors) {
    sensors = {
        { "gpu_usage", "GPU", true, false, "00E5FF" },
        { "gpu_clock", "GPU Clock", true, false, "00E5FF" },
        { "gpu_temp", "GPU Temp", true, false, "00E5FF" },
        { "gpu_hotspot", "Hotspot", true, false, "FF9100" },
        { "gpu_vram_temp", "VRAM Temp", true, false, "FFB300" },
        { "gpu_power", "GPU Power", false, false, "00E5FF" },
        { "gpu_vram", "VRAM", true, false, "00E5FF" },
        { "gpu_mem_clock", "Mem Clock", false, false, "00E5FF" },
        { "cpu_usage", "CPU", true, false, "5CC8FF" },
        { "cpu_temp", "CPU Temp", true, false, "5CC8FF" },
        { "cpu_power", "CPU Power", true, false, "5CC8FF" },
        { "cpu_cores", "CPU Cores", false, false, "5CC8FF" },
        { "ram_usage", "RAM", true, false, "FFB300" },
        { "pagefile", "Pagefile", false, false, "FFB300" },
        { "fps", "FPS", true, false, "00E676" },
        { "frametime", "Frametime", true, true, "00E676" },
        { "bottleneck", "Limit", true, false, "FF3B30" }
    };
}

ConfigManager::ConfigManager() {
    for (int i = 0; i < NUM_PROFILES; ++i) {
        profiles_[i].name = "Profile " + std::to_string(i + 1);
        init_default_sensors(profiles_[i].hud.sensors);
    }
    load_from_file();
}

void ConfigManager::switch_profile(int index) {
    if (index >= 0 && index < NUM_PROFILES) {
        active_profile_index_ = index;
    }
}

ProfileConfig& ConfigManager::get_profile(int index) {
    if (index >= 0 && index < NUM_PROFILES) {
        return profiles_[index];
    }
    return profiles_[active_profile_index_];
}

const ProfileConfig& ConfigManager::get_profile(int index) const {
    if (index >= 0 && index < NUM_PROFILES) {
        return profiles_[index];
    }
    return profiles_[active_profile_index_];
}

std::string ConfigManager::key_to_string(UINT vk_code) {
    if (vk_code >= VK_F1 && vk_code <= VK_F24) {
        return "F" + std::to_string(vk_code - VK_F1 + 1);
    }
    switch (vk_code) {
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_HOME: return "Home";
        case VK_END: return "End";
        case VK_PRIOR: return "Page Up";
        case VK_NEXT: return "Page Down";
        case VK_PAUSE: return "Pause";
        case VK_SCROLL: return "Scroll Lock";
        case VK_TAB: return "Tab";
        case VK_OEM_3: return "` (Tilde)";
        default: break;
    }
    if ((vk_code >= '0' && vk_code <= '9') || (vk_code >= 'A' && vk_code <= 'Z')) {
        return std::string(1, static_cast<char>(vk_code));
    }
    return "Key 0x" + std::to_string(vk_code);
}

static void parse_hud_json(const json& j, HudConfig& hud) {
    hud.position = static_cast<HudPosition>(j.value("position", static_cast<int>(HudPosition::TopLeft)));
    hud.custom_x = j.value("custom_x", 20);
    hud.custom_y = j.value("custom_y", 20);
    hud.bg_opacity = j.value("bg_opacity", 0.85f);
    hud.font_scale = j.value("font_scale", 1.0f);

    hud.output_to_rtss = j.value("output_to_rtss", true);
    hud.output_to_dx11_overlay = j.value("output_to_dx11_overlay", false);
    hud.close_rtss_on_exit = j.value("close_rtss_on_exit", true);

    hud.mode_fps = static_cast<SensorDisplayMode>(j.value("mode_fps", static_cast<int>(SensorDisplayMode::TextOnly)));
    hud.mode_frametime = static_cast<SensorDisplayMode>(j.value("mode_frametime", static_cast<int>(SensorDisplayMode::TextAndGraph)));
    hud.mode_gpu = static_cast<SensorDisplayMode>(j.value("mode_gpu", static_cast<int>(SensorDisplayMode::TextOnly)));
    hud.mode_cpu = static_cast<SensorDisplayMode>(j.value("mode_cpu", static_cast<int>(SensorDisplayMode::TextOnly)));
    hud.mode_ram = static_cast<SensorDisplayMode>(j.value("mode_ram", static_cast<int>(SensorDisplayMode::TextOnly)));

    hud.label_gpu = j.value("label_gpu", "GPU");
    hud.label_cpu = j.value("label_cpu", "CPU");
    hud.label_ram = j.value("label_ram", "RAM");
    hud.label_fps = j.value("label_fps", "FPS");
    hud.label_frametime = j.value("label_frametime", "FT");

    hud.color_gpu = j.value("color_gpu", "00E5FF");
    hud.color_cpu = j.value("color_cpu", "5CC8FF");
    hud.color_ram = j.value("color_ram", "FFB300");
    hud.color_fps = j.value("color_fps", "00E676");
    hud.color_frametime = j.value("color_frametime", "00E676");
    hud.color_values = j.value("color_values", "FFFFFF");
    hud.color_stutter = j.value("color_stutter", "FF1744");

    hud.rtss_graph_filled = j.value("rtss_graph_filled", false);
    hud.rtss_graph_width = j.value("rtss_graph_width", -32);
    hud.rtss_graph_height = j.value("rtss_graph_height", -4);
    hud.rtss_frametime_max_ms = j.value("rtss_frametime_max_ms", 50.0f);

    hud.show_fps = j.value("show_fps", true);
    hud.show_1pct_low = j.value("show_1pct_low", true);
    hud.show_01pct_low = j.value("show_01pct_low", true);
    hud.show_001pct_low = j.value("show_001pct_low", false);
    hud.show_min_max = j.value("show_min_max", true);
    hud.show_stutters = j.value("show_stutters", true);
    hud.show_frametime_text = j.value("show_frametime_text", true);

    hud.show_frametime_graph = j.value("show_frametime_graph", true);
    hud.frametime_graph_height = j.value("frametime_graph_height", 45.0f);
    hud.frametime_graph_max_ms = j.value("frametime_graph_max_ms", 33.3f);

    hud.show_gpu_usage = j.value("show_gpu_usage", true);
    hud.show_gpu_temp = j.value("show_gpu_temp", true);
    hud.show_gpu_hotspot = j.value("show_gpu_hotspot", true);
    hud.show_gpu_vram_temp = j.value("show_gpu_vram_temp", true);
    hud.show_gpu_vram = j.value("show_gpu_vram", true);
    hud.show_gpu_clock = j.value("show_gpu_clock", true);
    hud.show_gpu_power = j.value("show_gpu_power", false);

    hud.show_cpu_usage = j.value("show_cpu_usage", true);
    hud.show_cpu_temp = j.value("show_cpu_temp", true);
    hud.show_cpu_power = j.value("show_cpu_power", true);
    hud.show_cpu_per_core = j.value("show_cpu_per_core", false);
    hud.show_system_ram = j.value("show_system_ram", true);
    hud.show_pagefile = j.value("show_pagefile", true);
    hud.show_secondary_apps = j.value("show_secondary_apps", true);

    hud.show_bottleneck_indicator = j.value("show_bottleneck_indicator", true);
    hud.color_bottleneck = j.value("color_bottleneck", "FF3B30");

    hud.minimize_to_tray_on_close = j.value("minimize_to_tray_on_close", true);
    hud.show_tray_temperature = j.value("show_tray_temperature", true);
    hud.tray_temp_is_gpu = j.value("tray_temp_is_gpu", true);
    hud.autostart_with_windows = j.value("autostart_with_windows", false);
    hud.start_minimized_to_tray = j.value("start_minimized_to_tray", false);

    if (j.contains("sensors") && j["sensors"].is_array()) {
        hud.sensors.clear();
        for (const auto& sj : j["sensors"]) {
            SensorConfigItem item;
            item.id = sj.value("id", "");
            item.label = sj.value("label", "");
            item.enabled = sj.value("enabled", true);
            item.show_graph = sj.value("show_graph", false);
            item.color = sj.value("color", "00E5FF");
            hud.sensors.push_back(item);
        }
    }
    if (hud.sensors.empty()) {
        ConfigManager::init_default_sensors(hud.sensors);
    }
}

static json serialize_hud_json(const HudConfig& hud) {
    json j = {
        {"position", static_cast<int>(hud.position)},
        {"custom_x", hud.custom_x},
        {"custom_y", hud.custom_y},
        {"bg_opacity", hud.bg_opacity},
        {"font_scale", hud.font_scale},

        {"output_to_rtss", hud.output_to_rtss},
        {"output_to_dx11_overlay", hud.output_to_dx11_overlay},
        {"close_rtss_on_exit", hud.close_rtss_on_exit},

        {"mode_fps", static_cast<int>(hud.mode_fps)},
        {"mode_frametime", static_cast<int>(hud.mode_frametime)},
        {"mode_gpu", static_cast<int>(hud.mode_gpu)},
        {"mode_cpu", static_cast<int>(hud.mode_cpu)},
        {"mode_ram", static_cast<int>(hud.mode_ram)},

        {"label_gpu", hud.label_gpu},
        {"label_cpu", hud.label_cpu},
        {"label_ram", hud.label_ram},
        {"label_fps", hud.label_fps},
        {"label_frametime", hud.label_frametime},

        {"color_gpu", hud.color_gpu},
        {"color_cpu", hud.color_cpu},
        {"color_ram", hud.color_ram},
        {"color_fps", hud.color_fps},
        {"color_frametime", hud.color_frametime},
        {"color_values", hud.color_values},
        {"color_stutter", hud.color_stutter},

        {"rtss_graph_filled", hud.rtss_graph_filled},
        {"rtss_graph_width", hud.rtss_graph_width},
        {"rtss_graph_height", hud.rtss_graph_height},
        {"rtss_frametime_max_ms", hud.rtss_frametime_max_ms},

        {"show_fps", hud.show_fps},
        {"show_1pct_low", hud.show_1pct_low},
        {"show_01pct_low", hud.show_01pct_low},
        {"show_001pct_low", hud.show_001pct_low},
        {"show_min_max", hud.show_min_max},
        {"show_stutters", hud.show_stutters},
        {"show_frametime_text", hud.show_frametime_text},

        {"show_frametime_graph", hud.show_frametime_graph},
        {"frametime_graph_height", hud.frametime_graph_height},
        {"frametime_graph_max_ms", hud.frametime_graph_max_ms},

        {"show_gpu_usage", hud.show_gpu_usage},
        {"show_gpu_temp", hud.show_gpu_temp},
        {"show_gpu_hotspot", hud.show_gpu_hotspot},
        {"show_gpu_vram_temp", hud.show_gpu_vram_temp},
        {"show_gpu_vram", hud.show_gpu_vram},
        {"show_gpu_clock", hud.show_gpu_clock},
        {"show_gpu_power", hud.show_gpu_power},

        {"show_cpu_usage", hud.show_cpu_usage},
        {"show_cpu_temp", hud.show_cpu_temp},
        {"show_cpu_power", hud.show_cpu_power},
        {"show_cpu_per_core", hud.show_cpu_per_core},
        {"show_system_ram", hud.show_system_ram},
        {"show_pagefile", hud.show_pagefile},
        {"show_secondary_apps", hud.show_secondary_apps},

        {"show_bottleneck_indicator", hud.show_bottleneck_indicator},
        {"color_bottleneck", hud.color_bottleneck},

        {"minimize_to_tray_on_close", hud.minimize_to_tray_on_close},
        {"show_tray_temperature", hud.show_tray_temperature},
        {"tray_temp_is_gpu", hud.tray_temp_is_gpu},
        {"autostart_with_windows", hud.autostart_with_windows},
        {"start_minimized_to_tray", hud.start_minimized_to_tray}
    };

    json sensors_arr = json::array();
    for (const auto& s : hud.sensors) {
        sensors_arr.push_back({
            {"id", s.id},
            {"label", s.label},
            {"enabled", s.enabled},
            {"show_graph", s.show_graph},
            {"color", s.color}
        });
    }
    j["sensors"] = sensors_arr;
    return j;
}

static void parse_fan_json(const json& j, FanCurveConfig& fan) {
    fan.enabled = j.value("enabled", false);
    fan.mode = j.value("mode", 0);
    fan.fixed_speed_pct = j.value("fixed_speed_pct", 50);
    fan.hysteresis_c = j.value("hysteresis_c", 3);
    fan.zero_rpm_enabled = j.value("zero_rpm_enabled", true);

    if (j.contains("points") && j["points"].is_array()) {
        fan.points.clear();
        for (const auto& pj : j["points"]) {
            fan.points.push_back({ pj.value("temp_c", 50), pj.value("fan_pct", 50) });
        }
    }
}

static json serialize_fan_json(const FanCurveConfig& fan) {
    json points_arr = json::array();
    for (const auto& p : fan.points) {
        points_arr.push_back({
            {"temp_c", p.temp_c},
            {"fan_pct", p.fan_pct}
        });
    }
    return json{
        {"enabled", fan.enabled},
        {"mode", fan.mode},
        {"fixed_speed_pct", fan.fixed_speed_pct},
        {"hysteresis_c", fan.hysteresis_c},
        {"zero_rpm_enabled", fan.zero_rpm_enabled},
        {"points", points_arr}
    };
}

static void parse_oc_json(const json& j, OverclockConfig& oc) {
    oc.enabled = j.value("enabled", false);
    oc.core_clock_offset_mhz = j.value("core_clock_offset_mhz", 0);
    oc.memory_clock_offset_mhz = j.value("memory_clock_offset_mhz", 0);
    oc.power_limit_pct = j.value("power_limit_pct", 100);
    oc.temp_limit_c = j.value("temp_limit_c", 83);
    oc.voltage_offset_mv = j.value("voltage_offset_mv", 0);
    oc.apply_at_startup = j.value("apply_at_startup", false);

    if (j.contains("vf_curve") && j["vf_curve"].is_array()) {
        oc.vf_curve_points.clear();
        for (const auto& vfj : j["vf_curve"]) {
            oc.vf_curve_points.push_back({ vfj.value("voltage_mv", 800), vfj.value("freq_offset_mhz", 0) });
        }
    }
}

static json serialize_oc_json(const OverclockConfig& oc) {
    json vf_arr = json::array();
    for (const auto& p : oc.vf_curve_points) {
        vf_arr.push_back({
            {"voltage_mv", p.voltage_mv},
            {"freq_offset_mhz", p.freq_offset_mhz}
        });
    }
    return json{
        {"enabled", oc.enabled},
        {"core_clock_offset_mhz", oc.core_clock_offset_mhz},
        {"memory_clock_offset_mhz", oc.memory_clock_offset_mhz},
        {"power_limit_pct", oc.power_limit_pct},
        {"temp_limit_c", oc.temp_limit_c},
        {"voltage_offset_mv", oc.voltage_offset_mv},
        {"apply_at_startup", oc.apply_at_startup},
        {"vf_curve", vf_arr}
    };
}

bool ConfigManager::load_from_file(const std::wstring& filename) {
    current_file_ = filename;
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    try {
        json j = json::parse(file);

        active_profile_index_ = j.value("active_profile", 0);
        if (active_profile_index_ < 0 || active_profile_index_ >= NUM_PROFILES) {
            active_profile_index_ = 0;
        }

        if (j.contains("profiles") && j["profiles"].is_array()) {
            size_t idx = 0;
            for (const auto& pj : j["profiles"]) {
                if (idx >= NUM_PROFILES) break;
                profiles_[idx].name = pj.value("name", "Profile " + std::to_string(idx + 1));
                profiles_[idx].rtss_framerate_limit = pj.value("framerate_limit", 0);
                if (pj.contains("hud")) parse_hud_json(pj["hud"], profiles_[idx].hud);
                if (pj.contains("fan")) parse_fan_json(pj["fan"], profiles_[idx].fan);
                if (pj.contains("oc")) parse_oc_json(pj["oc"], profiles_[idx].oc);
                idx++;
            }
        } else {
            // Legacy single-profile migration
            parse_hud_json(j, profiles_[0].hud);
        }

        hotkeys_.toggle_overlay_key = static_cast<UINT>(j.value("toggle_overlay_key", static_cast<int>(VK_F11)));
        hotkeys_.toggle_benchmark_key = static_cast<UINT>(j.value("toggle_benchmark_key", static_cast<int>(VK_F10)));
    } catch (const json::exception& e) {
        std::cerr << "[AnyPerfomans] Config parse error: " << e.what() << std::endl;
        return false;
    }

    return true;
}

bool ConfigManager::save_to_file(const std::wstring& filename) {
    std::wstring out_name = filename.empty() ? current_file_ : filename;
    std::ofstream file(out_name);
    if (!file.is_open()) {
        return false;
    }

    json j_profiles = json::array();
    for (int i = 0; i < NUM_PROFILES; ++i) {
        j_profiles.push_back({
            {"name", profiles_[i].name},
            {"framerate_limit", profiles_[i].rtss_framerate_limit},
            {"hud", serialize_hud_json(profiles_[i].hud)},
            {"fan", serialize_fan_json(profiles_[i].fan)},
            {"oc", serialize_oc_json(profiles_[i].oc)}
        });
    }

    json j = {
        {"active_profile", active_profile_index_},
        {"profiles", j_profiles},
        {"toggle_overlay_key", static_cast<int>(hotkeys_.toggle_overlay_key)},
        {"toggle_benchmark_key", static_cast<int>(hotkeys_.toggle_benchmark_key)}
    };

    file << j.dump(2) << std::endl;
    file.close();
    return true;
}

} // namespace anyperf
