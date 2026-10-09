#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>

namespace anyperf {

struct ProcessInfo {
    DWORD pid = 0;
    std::wstring name;
    std::wstring path;
};

struct ProcessColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct ProcessMetrics {
    DWORD pid = 0;
    std::wstring name;
    std::string alias;
    bool is_alive = false;
    bool is_primary = false; // Primary application (e.g. main game or main benchmark target)

    // UI Color coding
    ProcessColor color{ 0.35f, 0.75f, 1.0f, 1.0f };

    // CPU Metrics
    uint64_t last_cycles = 0;
    uint64_t cycle_delta = 0;
    double cpu_percent = 0.0;           // Normalized percentage [0.0 .. 100.0]

    // Memory Metrics (bytes)
    size_t private_working_set = 0;    // RAM physically used exclusively
    size_t working_set = 0;            // Total physical RAM
    size_t commit_charge = 0;          // Committed memory (Private Bytes)

    // Thread & Handle counts
    uint32_t thread_count = 0;
    uint32_t handle_count = 0;
};

struct ThreadMetrics {
    DWORD tid = 0;
    uint64_t last_cycles = 0;
    uint64_t cycle_delta = 0;
    double cpu_percent = 0.0;
    uintptr_t start_address = 0;
    std::wstring module_name;
};

struct FrameStats {
    double current_fps = 0.0;
    double avg_fps = 0.0;
    double one_percent_low = 0.0;
    double zero_one_percent_low = 0.0;
    double frametime_ms = 0.0;
    uint64_t total_frames = 0;
};

enum class OverlayMode {
    Dashboard,      // Standalone interactive window with full charts and thread view
    MiniHud,        // Compact transparent borderless HUD over the desktop/game
    Hidden
};

struct BenchmarkProcessSummary {
    DWORD pid = 0;
    std::wstring name;
    double avg_cpu_percent = 0.0;
    size_t avg_ram_mb = 0;
};

struct BenchmarkRecord {
    std::wstring session_name;
    double avg_fps = 0.0;
    double one_percent_low = 0.0;
    double avg_frametime_ms = 0.0;
    double duration_seconds = 0.0;
    std::vector<BenchmarkProcessSummary> process_summaries;
};

} // namespace anyperf
