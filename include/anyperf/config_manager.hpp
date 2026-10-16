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

enum class SensorDisplayMode {
    Disabled = 0,
    TextOnly = 1,
    GraphOnly = 2,
    TextAndGraph = 3
};

struct HudConfig {
    HudPosition position = HudPosition::TopLeft;
    int custom_x = 20;
    int custom_y = 20;
    float bg_opacity = 0.85f;
    float font_scale = 1.0f;

    // Output Target Toggles
    bool output_to_rtss = true;
    bool output_to_dx11_overlay = false; // When RTSS is active, default standalone overlay to false to avoid duplicate HUD!
    bool close_rtss_on_exit = true;      // Auto terminate RTSS.exe on program exit

    // Sensor Display Modes (MSI Afterburner style)
    SensorDisplayMode mode_fps = SensorDisplayMode::TextOnly;
    SensorDisplayMode mode_frametime = SensorDisplayMode::TextAndGraph; // Default Frametime with high-tech Line Graph!
    SensorDisplayMode mode_gpu = SensorDisplayMode::TextOnly;
    SensorDisplayMode mode_cpu = SensorDisplayMode::TextOnly;
    SensorDisplayMode mode_ram = SensorDisplayMode::TextOnly;

    // Custom Labels / Names
    std::string label_gpu = "GPU";
    std::string label_cpu = "CPU";
    std::string label_ram = "RAM";
    std::string label_fps = "FPS";
    std::string label_frametime = "FT";

    // Custom Sensor Colors (Hex RRGGBB strings for RTSS OSD & HUD)
    std::string color_gpu = "00E5FF";       // Cyan
    std::string color_cpu = "5CC8FF";       // Light Blue
    std::string color_ram = "FFB300";       // Amber / Gold
    std::string color_fps = "00E676";       // Neon Green
    std::string color_frametime = "00E676"; // Neon Green
    std::string color_values = "FFFFFF";    // Crisp White
    std::string color_stutter = "FF1744";   // Alert Red

    // RTSS Embedded Line Graph Settings
    bool rtss_graph_filled = false;         // false = clean sharp line graph (User requested!), true = filled
    int rtss_graph_width = -32;             // Width in characters (-32 chars wide)
    int rtss_graph_height = -4;             // Height in lines (-4 character lines tall)
    float rtss_frametime_max_ms = 50.0f;    // Max frametime on graph scale (50.0 ms)

    // Metric Details Toggles
    bool show_fps = true;
    bool show_1pct_low = true;
    bool show_01pct_low = true;
    bool show_001pct_low = false;
    bool show_min_max = true;
    bool show_stutters = true;
    bool show_frametime_text = true;

    // Frametime Graph Settings
    bool show_frametime_graph = true;
    float frametime_graph_height = 45.0f;
    float frametime_graph_max_ms = 33.3f; // 0.0f for auto-scale

    // Hardware Telemetry Toggles
    bool show_gpu_usage = true;
    bool show_gpu_temp = true;
    bool show_gpu_hotspot = true;
    bool show_gpu_vram_temp = true;
    bool show_gpu_vram = true;
    bool show_gpu_clock = true;
    bool show_gpu_power = false;

    bool show_cpu_usage = true;
    bool show_cpu_temp = true;
    bool show_cpu_power = true;
    bool show_cpu_per_core = false;
    bool show_system_ram = true;
    bool show_pagefile = true;
    bool show_secondary_apps = true;

    // Bottleneck Indicator
    bool show_bottleneck_indicator = true;
    std::string color_bottleneck = "FF3B30";

    // System Integration & Tray (MSI Afterburner MAX)
    bool minimize_to_tray_on_close = true;
    bool show_tray_temperature = true;
    bool tray_temp_is_gpu = true;
    bool autostart_with_windows = false;
    bool start_minimized_to_tray = false;
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
