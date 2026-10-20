#pragma once

#include "anyperf/types.hpp"
#include "anyperf/ring_buffer.hpp"
#include "anyperf/process_monitor.hpp"
#include "anyperf/thread_profiler.hpp"
#include "anyperf/config_manager.hpp"
#include "anyperf/gpu_monitor.hpp"
#include "anyperf/cpu_monitor.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace anyperf {

class EtwTracker;
class Dx11Backend;
class RTSSBridge;
class HardwareController;
struct AppFonts;

class DashboardView {
public:
    DashboardView();
    ~DashboardView() = default;

    void render(
        ProcessMonitor& proc_mon,
        ThreadProfiler& thread_prof,
        EtwTracker& etw_tracker,
        GpuMonitor& gpu_mon,
        CpuMonitor& cpu_mon,
        HardwareController& hw_ctrl,
        ConfigManager& config_mgr,
        RTSSBridge& rtss_bridge,
        Dx11Backend& backend,
        const FrameStats& frame_stats,
        OverlayMode& current_mode
    );

    // Feed new telemetry sample into ring buffers for all tracked processes
    void push_metrics_sample(
        const std::vector<ProcessMetrics>& targets,
        const FrameStats& frame_stats,
        const GpuMetrics& gpu_metrics
    );

    // Benchmark Controls
    bool is_benchmarking() const { return is_benchmarking_; }
    double get_benchmark_elapsed_sec() const;
    void start_benchmark();
    void stop_benchmark(const ProcessMonitor& proc_mon, const GpuMetrics& gpu, const FrameStats& frames);
    void toggle_benchmark(const ProcessMonitor& proc_mon, const GpuMetrics& gpu, const FrameStats& frames);

private:
    void render_header_bar(
        ProcessMonitor& proc_mon,
        EtwTracker& etw_tracker,
        GpuMonitor& gpu_mon,
        const AppFonts& fonts,
        OverlayMode& current_mode
    );
    void render_navigation_bar(ConfigManager& config_mgr, const AppFonts& fonts);
    void render_process_tags_bar(ProcessMonitor& proc_mon, const AppFonts& fonts);
    void render_summary_cards(
        const ProcessMonitor& proc_mon,
        const EtwTracker& etw_tracker,
        const GpuMetrics& gpu,
        const CpuMetrics& cpu,
        const FrameStats& frame_stats,
        const AppFonts& fonts
    );
    void render_tuning_tab(
        HardwareController& hw_ctrl,
        ConfigManager& config_mgr,
        const ProcessMonitor& proc_mon,
        const GpuMetrics& gpu,
        const FrameStats& frame_stats,
        const AppFonts& fonts
    );
    void render_telemetry_tab(
        ThreadProfiler& thread_prof,
        const ProcessMonitor& proc_mon,
        const GpuMetrics& gpu,
        const AppFonts& fonts
    );
    void render_osd_sensors_tab(
        ConfigManager& config_mgr,
        RTSSBridge& rtss_bridge,
        const AppFonts& fonts
    );
    void render_vf_curve_editor_modal(
        HardwareController& hw_ctrl,
        ConfigManager& config_mgr,
        const GpuMetrics& gpu,
        const AppFonts& fonts
    );
    void render_rtss_mini_hud(
        const ProcessMonitor& proc_mon,
        const EtwTracker& etw_tracker,
        const GpuMetrics& gpu,
        const CpuMetrics& cpu,
        const ConfigManager& config_mgr,
        Dx11Backend& backend,
        const FrameStats& frames,
        OverlayMode& current_mode
    );

    // Buffers for real-time ImPlot curves
    RingBuffer<float, 300> frametime_history_;
    RingBuffer<float, 300> gpu_usage_history_;
    
    // Per-PID timeseries history
    std::unordered_map<DWORD, std::unique_ptr<RingBuffer<float, 300>>> cpu_histories_;
    std::unordered_map<DWORD, std::unique_ptr<RingBuffer<float, 300>>> ram_histories_mb_;

    std::vector<ProcessInfo> cached_proc_list_;
    char process_search_query_[128] = "";
    DWORD selected_inspector_pid_ = 0;

    int active_tab_index_ = 0; // 0 = Tuning & OC, 1 = Telemetry & Threads, 2 = OSD & Sensors
    bool show_vf_curve_ = true;
    bool show_fan_curve_ = false;
    bool show_curve_editor_modal_ = false; // MSI Afterburner-style standalone V/F Curve Window

    // A/B Benchmark state
    bool is_benchmarking_ = false;
    double benchmark_start_time_ = 0.0;
    int benchmark_duration_sec_ = 30;
    std::vector<BenchmarkRecord> benchmark_history_;
    std::vector<float> current_benchmark_frametimes_;
    std::wstring last_generated_report_path_;
};

} // namespace anyperf
