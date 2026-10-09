#pragma once

#include "anyperf/types.hpp"
#include "anyperf/ring_buffer.hpp"
#include "anyperf/process_monitor.hpp"
#include "anyperf/thread_profiler.hpp"
#include "anyperf/config_manager.hpp"
#include "anyperf/gpu_monitor.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace anyperf {

class EtwTracker;
class Dx11Backend;

class DashboardView {
public:
    DashboardView();
    ~DashboardView() = default;

    void render(
        ProcessMonitor& proc_mon,
        ThreadProfiler& thread_prof,
        EtwTracker& etw_tracker,
        GpuMonitor& gpu_mon,
        ConfigManager& config_mgr,
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

private:
    void render_top_bar(ProcessMonitor& proc_mon, EtwTracker& etw_tracker, OverlayMode& current_mode);
    void render_process_tags_bar(ProcessMonitor& proc_mon);
    void render_summary_cards(
        const ProcessMonitor& proc_mon,
        const EtwTracker& etw_tracker,
        const GpuMetrics& gpu,
        const FrameStats& frame_stats
    );
    void render_telemetry_plots(const ProcessMonitor& proc_mon, const GpuMetrics& gpu);
    void render_thread_inspector(
        ThreadProfiler& thread_prof,
        const ProcessMonitor& proc_mon
    );
    void render_benchmark_tab(
        const ProcessMonitor& proc_mon,
        const GpuMetrics& gpu,
        const FrameStats& frames
    );
    void render_settings_tab(
        ConfigManager& config_mgr
    );
    void render_rtss_mini_hud(
        const ProcessMonitor& proc_mon,
        const EtwTracker& etw_tracker,
        const GpuMetrics& gpu,
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

    // A/B Benchmark state
    bool is_benchmarking_ = false;
    double benchmark_start_time_ = 0.0;
    int benchmark_duration_sec_ = 30;
    std::vector<BenchmarkRecord> benchmark_history_;
    std::vector<float> current_benchmark_frametimes_;
    std::wstring last_generated_report_path_;
};

} // namespace anyperf
