#pragma once

#include <windows.h>
#include <string>
#include <cstdint>

namespace anyperf {

enum class HudPosition {
    TopLeft = 0,
    TopRight = 1,
    BottomLeft = 2,
    BottomRight = 3,
    Custom = 4
};

struct HudConfig {
    HudPosition position = HudPosition::TopLeft;
    int custom_x = 20;
    int custom_y = 20;
    float bg_opacity = 0.85f;
    float font_scale = 1.0f;

    // Metric Toggles
    bool show_fps = true;
    bool show_1pct_low = true;
    bool show_01pct_low = true;
    bool show_frametime_text = true;

    // Frametime Graph Settings
    bool show_frametime_graph = true;
    float frametime_graph_height = 45.0f;
    float frametime_graph_max_ms = 33.3f; // 0.0f for auto-scale

    // Hardware Telemetry Toggles
    bool show_gpu_usage = true;
    bool show_gpu_temp = true;
    bool show_gpu_vram = true;
    bool show_gpu_clock = true;
    bool show_gpu_power = false;

    bool show_cpu_usage = true;
    bool show_system_ram = true;
    bool show_secondary_apps = true;
};

struct HotkeyConfig {
    UINT toggle_overlay_key = VK_F11;
    UINT toggle_benchmark_key = VK_F10;
};

class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager() = default;

    bool load_from_file(const std::wstring& filename = L"anyperf_config.json");
    bool save_to_file(const std::wstring& filename = L"anyperf_config.json");

    HudConfig& get_hud() { return hud_; }
    const HudConfig& get_hud() const { return hud_; }

    HotkeyConfig& get_hotkeys() { return hotkeys_; }
    const HotkeyConfig& get_hotkeys() const { return hotkeys_; }

    static std::string key_to_string(UINT vk_code);

private:
    HudConfig hud_;
    HotkeyConfig hotkeys_;
    std::wstring current_file_ = L"anyperf_config.json";
};

} // namespace anyperf
