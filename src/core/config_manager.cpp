#include "anyperf/config_manager.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>

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

static bool parse_bool(const std::string& line, const std::string& key, bool default_val) {
    auto pos = line.find("\"" + key + "\"");
    if (pos == std::string::npos) return default_val;
    auto colon = line.find(':', pos);
    if (colon == std::string::npos) return default_val;
    return line.find("true", colon) != std::string::npos;
}

static float parse_float(const std::string& line, const std::string& key, float default_val) {
    auto pos = line.find("\"" + key + "\"");
    if (pos == std::string::npos) return default_val;
    auto colon = line.find(':', pos);
    if (colon == std::string::npos) return default_val;
    try {
        return std::stof(line.substr(colon + 1));
    } catch (...) {
        return default_val;
    }
}

static int parse_int(const std::string& line, const std::string& key, int default_val) {
    auto pos = line.find("\"" + key + "\"");
    if (pos == std::string::npos) return default_val;
    auto colon = line.find(':', pos);
    if (colon == std::string::npos) return default_val;
    try {
        return std::stoi(line.substr(colon + 1));
    } catch (...) {
        return default_val;
    }
}

bool ConfigManager::load_from_file(const std::wstring& filename) {
    current_file_ = filename;
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    hud_.position = static_cast<HudPosition>(parse_int(content, "position", static_cast<int>(HudPosition::TopLeft)));
    hud_.custom_x = parse_int(content, "custom_x", 20);
    hud_.custom_y = parse_int(content, "custom_y", 20);
    hud_.bg_opacity = parse_float(content, "bg_opacity", 0.85f);
    hud_.font_scale = parse_float(content, "font_scale", 1.0f);

    hud_.show_fps = parse_bool(content, "show_fps", true);
    hud_.show_1pct_low = parse_bool(content, "show_1pct_low", true);
    hud_.show_01pct_low = parse_bool(content, "show_01pct_low", true);
    hud_.show_frametime_text = parse_bool(content, "show_frametime_text", true);

    hud_.show_frametime_graph = parse_bool(content, "show_frametime_graph", true);
    hud_.frametime_graph_height = parse_float(content, "frametime_graph_height", 45.0f);
    hud_.frametime_graph_max_ms = parse_float(content, "frametime_graph_max_ms", 33.3f);

    hud_.show_gpu_usage = parse_bool(content, "show_gpu_usage", true);
    hud_.show_gpu_temp = parse_bool(content, "show_gpu_temp", true);
    hud_.show_gpu_vram = parse_bool(content, "show_gpu_vram", true);
    hud_.show_gpu_clock = parse_bool(content, "show_gpu_clock", true);
    hud_.show_gpu_power = parse_bool(content, "show_gpu_power", false);

    hud_.show_cpu_usage = parse_bool(content, "show_cpu_usage", true);
    hud_.show_system_ram = parse_bool(content, "show_system_ram", true);
    hud_.show_secondary_apps = parse_bool(content, "show_secondary_apps", true);

    hotkeys_.toggle_overlay_key = static_cast<UINT>(parse_int(content, "toggle_overlay_key", VK_F11));
    hotkeys_.toggle_benchmark_key = static_cast<UINT>(parse_int(content, "toggle_benchmark_key", VK_F10));

    return true;
}

bool ConfigManager::save_to_file(const std::wstring& filename) {
    std::wstring out_name = filename.empty() ? current_file_ : filename;
    std::ofstream file(out_name);
    if (!file.is_open()) {
        return false;
    }

    file << "{\n";
    file << "  \"position\": " << static_cast<int>(hud_.position) << ",\n";
    file << "  \"custom_x\": " << hud_.custom_x << ",\n";
    file << "  \"custom_y\": " << hud_.custom_y << ",\n";
    file << "  \"bg_opacity\": " << hud_.bg_opacity << ",\n";
    file << "  \"font_scale\": " << hud_.font_scale << ",\n";

    file << "  \"show_fps\": " << (hud_.show_fps ? "true" : "false") << ",\n";
    file << "  \"show_1pct_low\": " << (hud_.show_1pct_low ? "true" : "false") << ",\n";
    file << "  \"show_01pct_low\": " << (hud_.show_01pct_low ? "true" : "false") << ",\n";
    file << "  \"show_frametime_text\": " << (hud_.show_frametime_text ? "true" : "false") << ",\n";

    file << "  \"show_frametime_graph\": " << (hud_.show_frametime_graph ? "true" : "false") << ",\n";
    file << "  \"frametime_graph_height\": " << hud_.frametime_graph_height << ",\n";
    file << "  \"frametime_graph_max_ms\": " << hud_.frametime_graph_max_ms << ",\n";

    file << "  \"show_gpu_usage\": " << (hud_.show_gpu_usage ? "true" : "false") << ",\n";
    file << "  \"show_gpu_temp\": " << (hud_.show_gpu_temp ? "true" : "false") << ",\n";
    file << "  \"show_gpu_vram\": " << (hud_.show_gpu_vram ? "true" : "false") << ",\n";
    file << "  \"show_gpu_clock\": " << (hud_.show_gpu_clock ? "true" : "false") << ",\n";
    file << "  \"show_gpu_power\": " << (hud_.show_gpu_power ? "true" : "false") << ",\n";

    file << "  \"show_cpu_usage\": " << (hud_.show_cpu_usage ? "true" : "false") << ",\n";
    file << "  \"show_system_ram\": " << (hud_.show_system_ram ? "true" : "false") << ",\n";
    file << "  \"show_secondary_apps\": " << (hud_.show_secondary_apps ? "true" : "false") << ",\n";

    file << "  \"toggle_overlay_key\": " << hotkeys_.toggle_overlay_key << ",\n";
    file << "  \"toggle_benchmark_key\": " << hotkeys_.toggle_benchmark_key << "\n";
    file << "}\n";

    file.close();
    return true;
}

} // namespace anyperf
