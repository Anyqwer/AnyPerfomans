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
        hud_.output_to_dx11_overlay = j.value("output_to_dx11_overlay", true);

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
        hud_.show_gpu_vram = j.value("show_gpu_vram", true);
        hud_.show_gpu_clock = j.value("show_gpu_clock", true);
        hud_.show_gpu_power = j.value("show_gpu_power", false);

        hud_.show_cpu_usage = j.value("show_cpu_usage", true);
        hud_.show_system_ram = j.value("show_system_ram", true);
        hud_.show_pagefile = j.value("show_pagefile", true);
        hud_.show_secondary_apps = j.value("show_secondary_apps", true);

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
        {"show_gpu_vram", hud_.show_gpu_vram},
        {"show_gpu_clock", hud_.show_gpu_clock},
        {"show_gpu_power", hud_.show_gpu_power},

        {"show_cpu_usage", hud_.show_cpu_usage},
        {"show_system_ram", hud_.show_system_ram},
        {"show_pagefile", hud_.show_pagefile},
        {"show_secondary_apps", hud_.show_secondary_apps},

        {"toggle_overlay_key", static_cast<int>(hotkeys_.toggle_overlay_key)},
        {"toggle_benchmark_key", static_cast<int>(hotkeys_.toggle_benchmark_key)}
    };

    file << j.dump(2) << std::endl;
    file.close();
    return true;
}

} // namespace anyperf
