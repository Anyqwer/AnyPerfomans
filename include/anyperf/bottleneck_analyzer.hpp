#pragma once

#include "anyperf/gpu_monitor.hpp"
#include "anyperf/cpu_monitor.hpp"
#include "anyperf/types.hpp"
#include <string>

namespace anyperf {

enum class BottleneckType {
    None,
    GpuBound,          // GPU load >= 94%
    CpuSingleCore,     // A single CPU core >= 88% while GPU < 88%
    CpuMultiCore,      // Overall CPU >= 80% while GPU < 85%
    RamBound,          // System RAM > 92%
    VramBound,         // Dedicated VRAM > 95%
    FpsCapped          // Framerate limited / VSync
};

struct BottleneckInfo {
    BottleneckType type = BottleneckType::None;
    std::string description = "Balanced / Uncapped";
    std::string short_badge = "OK";
    int limiting_core_index = -1;
    double limit_percentage = 0.0;
};

inline BottleneckInfo detect_bottleneck(
    const GpuMetrics& gpu,
    const CpuMetrics& cpu,
    double current_fps,
    size_t ram_used,
    size_t ram_total,
    double frametime_ms = 0.0
) {
    BottleneckInfo info;

    if (current_fps <= 0.0) {
        info.type = BottleneckType::None;
        info.description = "Idle";
        info.short_badge = "IDLE";
        return info;
    }

    // 1. GPU Saturation Check (GPU >= 94%)
    if (gpu.core_usage_percent >= 94) {
        info.type = BottleneckType::GpuBound;
        info.limit_percentage = static_cast<double>(gpu.core_usage_percent);
        info.description = "GPU Bound (" + std::to_string(gpu.core_usage_percent) + "%)";
        info.short_badge = "GPU " + std::to_string(gpu.core_usage_percent) + "%";
        return info;
    }

    // 2. VRAM Saturation Check (VRAM >= 95%)
    if (gpu.vram_total_bytes > 0) {
        double vram_pct = (static_cast<double>(gpu.vram_used_bytes) / static_cast<double>(gpu.vram_total_bytes)) * 100.0;
        if (vram_pct >= 95.0) {
            info.type = BottleneckType::VramBound;
            info.limit_percentage = vram_pct;
            info.description = "VRAM Saturated (" + std::to_string(static_cast<int>(vram_pct)) + "%)";
            info.short_badge = "VRAM";
            return info;
        }
    }

    // 3. System RAM Saturation Check (RAM >= 92%)
    if (ram_total > 0) {
        double ram_pct = (static_cast<double>(ram_used) / static_cast<double>(ram_total)) * 100.0;
        if (ram_pct >= 92.0) {
            info.type = BottleneckType::RamBound;
            info.limit_percentage = ram_pct;
            info.description = "System RAM Saturated (" + std::to_string(static_cast<int>(ram_pct)) + "%)";
            info.short_badge = "RAM";
            return info;
        }
    }

    // 4. Single-Core CPU Bottleneck (One logical core >= 88% while GPU < 88%)
    if (cpu.max_core_usage >= 88.0 && gpu.core_usage_percent < 88) {
        info.type = BottleneckType::CpuSingleCore;
        info.limiting_core_index = cpu.max_core_index;
        info.limit_percentage = cpu.max_core_usage;
        info.description = "CPU Core #" + std::to_string(cpu.max_core_index) + " Bound (" + std::to_string(static_cast<int>(cpu.max_core_usage)) + "%)";
        info.short_badge = "CPU-C" + std::to_string(cpu.max_core_index);
        return info;
    }

    // 5. Multi-Core CPU Bottleneck (Total CPU >= 80% while GPU < 85%)
    if (cpu.total_usage_percent >= 80.0 && gpu.core_usage_percent < 85) {
        info.type = BottleneckType::CpuMultiCore;
        info.limit_percentage = cpu.total_usage_percent;
        info.description = "CPU Multi-Core Bound (" + std::to_string(static_cast<int>(cpu.total_usage_percent)) + "%)";
        info.short_badge = "CPU ALL";
        return info;
    }

    // 6. FPS Capped / V-Sync Check (Neither GPU nor CPU saturated, rock-steady framerate)
    if (gpu.core_usage_percent < 85 && cpu.max_core_usage < 80.0 && current_fps >= 25.0) {
        int round_fps = static_cast<int>(std::round(current_fps));
        bool common_cap = (round_fps == 30 || round_fps == 60 || round_fps == 75 || round_fps == 90 || 
                           round_fps == 120 || round_fps == 144 || round_fps == 165 || round_fps == 240);
        if (common_cap || (frametime_ms > 0.0 && std::abs((1000.0 / frametime_ms) - current_fps) < 0.8)) {
            info.type = BottleneckType::FpsCapped;
            info.limit_percentage = current_fps;
            info.description = "FPS Capped (" + std::to_string(round_fps) + " FPS)";
            info.short_badge = "CAP " + std::to_string(round_fps);
            return info;
        }
    }

    return info;
}

} // namespace anyperf
