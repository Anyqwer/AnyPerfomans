#pragma once

#include "anyperf/types.hpp"
#include "anyperf/ring_buffer.hpp"
#include "anyperf/process_monitor.hpp"
#include "anyperf/thread_profiler.hpp"
#include <string>
#include <vector>

namespace anyperf {

class DashboardView {
public:
    DashboardView();
    ~DashboardView() = default;

    void render(
        ProcessMonitor& proc_mon,
        ThreadProfiler& thread_prof,
        const FrameStats& frame_stats,
        OverlayMode& current_mode
    );

    // Feed new telemetry sample into ring buffers
    void push_metrics_sample(
        const ProcessMetrics& game_metrics,
        const ProcessMetrics& aux_metrics,
        const FrameStats& frame_stats
    );

private:
    void render_top_bar(ProcessMonitor& proc_mon, OverlayMode& current_mode);
    void render_summary_cards(
        const ProcessMetrics& game_metrics,
        const ProcessMetrics& aux_metrics,
        const FrameStats& frame_stats
    );
    void render_telemetry_plots();
    void render_thread_inspector(ThreadProfiler& thread_prof, DWORD target_pid);
    void render_benchmark_tab(
        const ProcessMetrics& game,
        const ProcessMetrics& aux,
        const FrameStats& frames
    );
    void render_mini_hud(
        const ProcessMetrics& game,
        const ProcessMetrics& aux,
        const FrameStats& frames,
        OverlayMode& current_mode
    );

    // Buffers for real-time ImPlot curves (last 240 samples = ~12-24 seconds)
    RingBuffer<float, 300> frametime_history_;
    RingBuffer<float, 300> game_cpu_history_;
    RingBuffer<float, 300> aux_cpu_history_;
    RingBuffer<float, 300> game_ram_history_mb_;
    RingBuffer<float, 300> aux_ram_history_mb_;

    std::vector<ProcessInfo> cached_proc_list_;
    int selected_game_index_ = -1;
    int selected_aux_index_ = -1;
    char search_filter_[128] = "";

    // A/B Benchmark state
    bool is_benchmarking_ = false;
    double benchmark_start_time_ = 0.0;
    int benchmark_duration_sec_ = 30;
    std::vector<BenchmarkRecord> benchmark_history_;
};

} // namespace anyperf
