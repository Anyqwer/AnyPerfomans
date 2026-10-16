#include "anyperf/config_manager.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace anyperf {

ConfigManager::ConfigManager() {
    load_from_file();
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

bool ConfigManager::load_from_file(const std::wstring& filename) {
    current_file_ = filename;
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    try {
        json j = json::parse(file);

        hud_.position = static_cast<HudPosition>(j.value("position", static_cast<int>(HudPosition::TopLeft)));
        hud_.custom_x = j.value("custom_x", 20);
        hud_.custom_y = j.value("custom_y", 20);
        hud_.bg_opacity = j.value("bg_opacity", 0.85f);
        hud_.font_scale = j.value("font_scale", 1.0f);

        hud_.output_to_rtss = j.value("output_to_rtss", true);
        hud_.output_to_dx11_overlay = j.value("output_to_dx11_overlay", false);
        hud_.close_rtss_on_exit = j.value("close_rtss_on_exit", true);

        hud_.mode_fps = static_cast<SensorDisplayMode>(j.value("mode_fps", static_cast<int>(SensorDisplayMode::TextOnly)));
        hud_.mode_frametime = static_cast<SensorDisplayMode>(j.value("mode_frametime", static_cast<int>(SensorDisplayMode::TextAndGraph)));
        hud_.mode_gpu = static_cast<SensorDisplayMode>(j.value("mode_gpu", static_cast<int>(SensorDisplayMode::TextOnly)));
        hud_.mode_cpu = static_cast<SensorDisplayMode>(j.value("mode_cpu", static_cast<int>(SensorDisplayMode::TextOnly)));
        hud_.mode_ram = static_cast<SensorDisplayMode>(j.value("mode_ram", static_cast<int>(SensorDisplayMode::TextOnly)));

        hud_.label_gpu = j.value("label_gpu", "GPU");
        hud_.label_cpu = j.value("label_cpu", "CPU");
        hud_.label_ram = j.value("label_ram", "RAM");
        hud_.label_fps = j.value("label_fps", "FPS");
        hud_.label_frametime = j.value("label_frametime", "FT");

        hud_.color_gpu = j.value("color_gpu", "00E5FF");
        hud_.color_cpu = j.value("color_cpu", "5CC8FF");
        hud_.color_ram = j.value("color_ram", "FFB300");
        hud_.color_fps = j.value("color_fps", "00E676");
        hud_.color_frametime = j.value("color_frametime", "00E676");
        hud_.color_values = j.value("color_values", "FFFFFF");
        hud_.color_stutter = j.value("color_stutter", "FF1744");

        hud_.rtss_graph_filled = j.value("rtss_graph_filled", false);
        hud_.rtss_graph_width = j.value("rtss_graph_width", -32);
        hud_.rtss_graph_height = j.value("rtss_graph_height", -4);
        hud_.rtss_frametime_max_ms = j.value("rtss_frametime_max_ms", 50.0f);

        hud_.show_fps = j.value("show_fps", true);
        hud_.show_1pct_low = j.value("show_1pct_low", true);
        hud_.show_01pct_low = j.value("show_01pct_low", true);
        hud_.show_001pct_low = j.value("show_001pct_low", false);
        hud_.show_min_max = j.value("show_min_max", true);
        hud_.show_stutters = j.value("show_stutters", true);
        hud_.show_frametime_text = j.value("show_frametime_text", true);

        hud_.show_frametime_graph = j.value("show_frametime_graph", true);
        hud_.frametime_graph_height = j.value("frametime_graph_height", 45.0f);
        hud_.frametime_graph_max_ms = j.value("frametime_graph_max_ms", 33.3f);

        hud_.show_gpu_usage = j.value("show_gpu_usage", true);
        hud_.show_gpu_temp = j.value("show_gpu_temp", true);
        hud_.show_gpu_hotspot = j.value("show_gpu_hotspot", true);
        hud_.show_gpu_vram_temp = j.value("show_gpu_vram_temp", true);
        hud_.show_gpu_vram = j.value("show_gpu_vram", true);
        hud_.show_gpu_clock = j.value("show_gpu_clock", true);
        hud_.show_gpu_power = j.value("show_gpu_power", false);

        hud_.show_cpu_usage = j.value("show_cpu_usage", true);
        hud_.show_cpu_temp = j.value("show_cpu_temp", true);
        hud_.show_cpu_power = j.value("show_cpu_power", true);
        hud_.show_cpu_per_core = j.value("show_cpu_per_core", false);
        hud_.show_system_ram = j.value("show_system_ram", true);
        hud_.show_pagefile = j.value("show_pagefile", true);
        hud_.show_secondary_apps = j.value("show_secondary_apps", true);

        hud_.show_bottleneck_indicator = j.value("show_bottleneck_indicator", true);
        hud_.color_bottleneck = j.value("color_bottleneck", "FF3B30");

        hud_.minimize_to_tray_on_close = j.value("minimize_to_tray_on_close", true);
        hud_.show_tray_temperature = j.value("show_tray_temperature", true);
        hud_.tray_temp_is_gpu = j.value("tray_temp_is_gpu", true);
        hud_.autostart_with_windows = j.value("autostart_with_windows", false);
        hud_.start_minimized_to_tray = j.value("start_minimized_to_tray", false);

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

    json j = {
        {"position", static_cast<int>(hud_.position)},
        {"custom_x", hud_.custom_x},
        {"custom_y", hud_.custom_y},
        {"bg_opacity", hud_.bg_opacity},
        {"font_scale", hud_.font_scale},

        {"output_to_rtss", hud_.output_to_rtss},
        {"output_to_dx11_overlay", hud_.output_to_dx11_overlay},
        {"close_rtss_on_exit", hud_.close_rtss_on_exit},

        {"mode_fps", static_cast<int>(hud_.mode_fps)},
        {"mode_frametime", static_cast<int>(hud_.mode_frametime)},
        {"mode_gpu", static_cast<int>(hud_.mode_gpu)},
        {"mode_cpu", static_cast<int>(hud_.mode_cpu)},
        {"mode_ram", static_cast<int>(hud_.mode_ram)},

        {"label_gpu", hud_.label_gpu},
        {"label_cpu", hud_.label_cpu},
        {"label_ram", hud_.label_ram},
        {"label_fps", hud_.label_fps},
        {"label_frametime", hud_.label_frametime},

        {"color_gpu", hud_.color_gpu},
        {"color_cpu", hud_.color_cpu},
        {"color_ram", hud_.color_ram},
        {"color_fps", hud_.color_fps},
        {"color_frametime", hud_.color_frametime},
        {"color_values", hud_.color_values},
        {"color_stutter", hud_.color_stutter},

        {"rtss_graph_filled", hud_.rtss_graph_filled},
        {"rtss_graph_width", hud_.rtss_graph_width},
        {"rtss_graph_height", hud_.rtss_graph_height},
        {"rtss_frametime_max_ms", hud_.rtss_frametime_max_ms},

        {"show_fps", hud_.show_fps},
        {"show_1pct_low", hud_.show_1pct_low},
        {"show_01pct_low", hud_.show_01pct_low},
        {"show_001pct_low", hud_.show_001pct_low},
        {"show_min_max", hud_.show_min_max},
        {"show_stutters", hud_.show_stutters},
        {"show_frametime_text", hud_.show_frametime_text},

        {"show_frametime_graph", hud_.show_frametime_graph},
        {"frametime_graph_height", hud_.frametime_graph_height},
        {"frametime_graph_max_ms", hud_.frametime_graph_max_ms},

        {"show_gpu_usage", hud_.show_gpu_usage},
        {"show_gpu_temp", hud_.show_gpu_temp},
        {"show_gpu_hotspot", hud_.show_gpu_hotspot},
        {"show_gpu_vram_temp", hud_.show_gpu_vram_temp},
        {"show_gpu_vram", hud_.show_gpu_vram},
        {"show_gpu_clock", hud_.show_gpu_clock},
        {"show_gpu_power", hud_.show_gpu_power},

        {"show_cpu_usage", hud_.show_cpu_usage},
        {"show_cpu_temp", hud_.show_cpu_temp},
        {"show_cpu_power", hud_.show_cpu_power},
        {"show_cpu_per_core", hud_.show_cpu_per_core},
        {"show_system_ram", hud_.show_system_ram},
        {"show_pagefile", hud_.show_pagefile},
        {"show_secondary_apps", hud_.show_secondary_apps},

        {"show_bottleneck_indicator", hud_.show_bottleneck_indicator},
        {"color_bottleneck", hud_.color_bottleneck},

        {"minimize_to_tray_on_close", hud_.minimize_to_tray_on_close},
        {"show_tray_temperature", hud_.show_tray_temperature},
        {"tray_temp_is_gpu", hud_.tray_temp_is_gpu},
        {"autostart_with_windows", hud_.autostart_with_windows},
        {"start_minimized_to_tray", hud_.start_minimized_to_tray},

        {"toggle_overlay_key", static_cast<int>(hotkeys_.toggle_overlay_key)},
        {"toggle_benchmark_key", static_cast<int>(hotkeys_.toggle_benchmark_key)}
    };

    file << j.dump(2) << std::endl;
    file.close();
    return true;
}

} // namespace anyperf
