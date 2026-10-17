#pragma once

#include <windows.h>
#include <string>
#include <vector>
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

struct SensorConfigItem {
    std::string id;              // e.g. "gpu_usage", "gpu_temp", "fps", etc.
    std::string label;           // Custom user name, e.g. "GPU", "RTX 4090", "FPS"
    bool enabled = true;         // Show in OSD / HUD
    bool show_graph = false;     // Embed hardware line graph in RTSS / HUD
    std::string color = "00E5FF";// Hex color code (RRGGBB)
};

struct HudConfig {
    HudPosition position = HudPosition::TopLeft;
    int custom_x = 20;
    int custom_y = 20;
    float bg_opacity = 0.85f;
    float font_scale = 1.0f;

    // Output Target Toggles
    bool output_to_rtss = true;
    bool output_to_dx11_overlay = false; // When RTSS is active, default standalone overlay to false
    bool close_rtss_on_exit = true;      // Auto terminate RTSS.exe on program exit

    // Sensor Display Modes (MSI Afterburner style)
    SensorDisplayMode mode_fps = SensorDisplayMode::TextOnly;
    SensorDisplayMode mode_frametime = SensorDisplayMode::TextAndGraph;
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
    bool rtss_graph_filled = false;         // false = clean sharp line graph, true = filled
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
    float frametime_graph_max_ms = 33.3f;

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

    // Reorderable Sensor List (Afterburner style drag/reorder)
    std::vector<SensorConfigItem> sensors;

    // System Integration & Tray (MSI Afterburner MAX)
    bool minimize_to_tray_on_close = true;
    bool show_tray_temperature = true;
    bool tray_temp_is_gpu = true;
    bool autostart_with_windows = false;
    bool start_minimized_to_tray = false;
    // Compact Multi-Metric Groupings (MSI Afterburner MAX)
    bool group_gpu_on_one_line = false;      // Compact GPU line: GPU: 65C | 1750MHz | 99% | 115W
    bool group_vram_on_one_line = false;     // Compact VRAM line: VRAM: 4850 / 6144 MB | 72C | 1750MHz
    bool group_cpu_on_one_line = false;      // Compact CPU line: CPU: 62C | 48.5% | 65W
    bool group_ram_on_one_line = false;      // Compact RAM line: RAM: 14.2 / 31.9 GB | Page: 16.5 GB
    bool group_fps_on_one_line = false;      // Compact FPS line: FPS: 144 | 6.9ms (Avg 142, 1% Low 115)

    // CPU Per-Core Display Customization (MSI Afterburner style)
    bool cpu_cores_in_column = false;        // true = Each core in its own row/column (в столбик), false = Grouped
    bool group_cpu_cores_on_one_line = true; // true = All cores in 1 line, false = wrap by row
    int cpu_cores_per_row = 8;               // Number of cores per line when wrapped
    bool rtss_osd_visible = true;            // Live toggle for RTSS OSD visibility
};

// Fan Curve Point
struct FanPoint {
    int temp_c = 0;
    int fan_pct = 0;
};

// Fan Control Configuration
struct FanCurveConfig {
    bool enabled = false;
    int mode = 0; // 0 = Auto (VBIOS default), 1 = Fixed Manual %, 2 = Custom Curve
    int fixed_speed_pct = 50;
    int hysteresis_c = 3;
    bool zero_rpm_enabled = true;
    int temp_source = 0; // 0 = GPU Core (Edge), 1 = GPU Hotspot (Junction), 2 = VRAM
    std::vector<FanPoint> points = {
        { 30, 0 },
        { 50, 40 },
        { 65, 60 },
        { 80, 80 },
        { 90, 100 }
    };
};

// Voltage / Frequency Point
struct VfPoint {
    int voltage_mv = 700;
    int freq_offset_mhz = 0;
};

// Overclocking / Undervolting Configuration
struct OverclockConfig {
    bool enabled = false;
    int core_clock_offset_mhz = 0;     // -500 .. +500 MHz
    int memory_clock_offset_mhz = 0;   // -1000 .. +2000 MHz
    int power_limit_pct = 100;         // 50 .. 130 %
    int temp_limit_c = 83;             // 65 .. 90 °C
    int voltage_offset_mv = 0;         // 0 .. 100 mV
    bool apply_at_startup = false;     // Apply OC and Fan profile on Windows startup
    std::vector<VfPoint> vf_curve_points = {
        { 700, 0 }, { 750, 0 }, { 800, 0 }, { 850, 0 },
        { 900, 0 }, { 950, 0 }, { 1000, 0 }, { 1050, 0 }, { 1100, 0 }
    };
};

struct ProfileConfig {
    std::string name = "Profile 1";
    HudConfig hud;
    FanCurveConfig fan;
    OverclockConfig oc;
    int rtss_framerate_limit = 0; // 0 = Uncapped
};

struct HotkeyConfig {
    UINT toggle_overlay_key = VK_F11;   // Toggle AnyPerfomans Dashboard window
    UINT toggle_rtss_osd_key = VK_F12;  // Toggle RTSS In-Game OSD overlay visibility
    UINT toggle_benchmark_key = VK_F10; // Toggle Benchmark Run
    bool enable_profile_hotkeys = true; // Ctrl+Shift+1..5
};

class ConfigManager {
public:
    static constexpr int NUM_PROFILES = 5;

    ConfigManager();
    ~ConfigManager() = default;

    bool load_from_file(const std::wstring& filename = L"anyperf_config.json");
    bool save_to_file(const std::wstring& filename = L"anyperf_config.json");

    // Profile Management (5 Slots: 0..4)
    int get_active_profile_index() const { return active_profile_index_; }
    void switch_profile(int index);
    ProfileConfig& get_profile(int index);
    const ProfileConfig& get_profile(int index) const;

    // Active Profile Accessors (Seamless backward compatibility)
    HudConfig& get_hud() { return profiles_[active_profile_index_].hud; }
    const HudConfig& get_hud() const { return profiles_[active_profile_index_].hud; }

    FanCurveConfig& get_fan() { return profiles_[active_profile_index_].fan; }
    const FanCurveConfig& get_fan() const { return profiles_[active_profile_index_].fan; }

    OverclockConfig& get_oc() { return profiles_[active_profile_index_].oc; }
    const OverclockConfig& get_oc() const { return profiles_[active_profile_index_].oc; }

    int get_framerate_limit() const { return profiles_[active_profile_index_].rtss_framerate_limit; }
    void set_framerate_limit(int limit) { profiles_[active_profile_index_].rtss_framerate_limit = limit; }

    HotkeyConfig& get_hotkeys() { return hotkeys_; }
    const HotkeyConfig& get_hotkeys() const { return hotkeys_; }

    static std::string key_to_string(UINT vk_code);
    static void init_default_sensors(std::vector<SensorConfigItem>& sensors);

private:
    int active_profile_index_ = 0;
    ProfileConfig profiles_[NUM_PROFILES];
    HotkeyConfig hotkeys_;
    std::wstring current_file_ = L"anyperf_config.json";
};

} // namespace anyperf
