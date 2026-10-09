#include "anyperf/dashboard_view.hpp"
#include <imgui.h>
#include <implot.h>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace anyperf {

static std::string wide_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &result[0], size, nullptr, nullptr);
    return result;
}

DashboardView::DashboardView() {
    cached_proc_list_ = ProcessMonitor::enumerate_processes();
}

void DashboardView::push_metrics_sample(
    const ProcessMetrics& game_metrics,
    const ProcessMetrics& aux_metrics,
    const FrameStats& frame_stats
) {
    frametime_history_.push(static_cast<float>(frame_stats.frametime_ms));
    game_cpu_history_.push(static_cast<float>(game_metrics.cpu_percent));
    aux_cpu_history_.push(static_cast<float>(aux_metrics.cpu_percent));

    float game_mb = static_cast<float>(game_metrics.private_working_set) / (1024.0f * 1024.0f);
    float aux_mb  = static_cast<float>(aux_metrics.private_working_set) / (1024.0f * 1024.0f);

    game_ram_history_mb_.push(game_mb);
    aux_ram_history_mb_.push(aux_mb);
}

void DashboardView::render(
    ProcessMonitor& proc_mon,
    ThreadProfiler& thread_prof,
    const FrameStats& frame_stats,
    OverlayMode& current_mode
) {
    const auto& game = proc_mon.get_game_metrics();
    const auto& aux  = proc_mon.get_aux_metrics();

    if (current_mode == OverlayMode::MiniHud) {
        render_mini_hud(game, aux, frame_stats, current_mode);
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    ImGui::Begin("AnyPerfomans Dashboard", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse
    );

    render_top_bar(proc_mon, current_mode);

    ImGui::Separator();
    ImGui::Spacing();

    render_summary_cards(game, aux, frame_stats);

    ImGui::Spacing();

    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Telemetry & Real-time Charts")) {
            render_telemetry_plots();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Thread Inspector (CS2 & Aux)")) {
            render_thread_inspector(thread_prof, proc_mon.get_game_pid());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("A/B Impact Benchmark")) {
            render_benchmark_tab(game, aux, frame_stats);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void DashboardView::render_top_bar(ProcessMonitor& proc_mon, OverlayMode& current_mode) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.26f, 0.78f, 0.86f, 1.0f));
    ImGui::Text("ANYPERFOMANS");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("v0.1.0 | High-Precision Game & Process Impact Profiler");

    ImGui::SameLine(ImGui::GetWindowWidth() - 280.0f);
    if (ImGui::Button("Switch to Mini HUD [F11]", ImVec2(260, 26))) {
        current_mode = OverlayMode::MiniHud;
    }

    ImGui::Spacing();

    // Process selection controls
    ImGui::Text("Target Game:");
    ImGui::SameLine();

    std::string game_name_utf8 = proc_mon.get_game_metrics().name.empty()
        ? "Not selected"
        : wide_to_utf8(proc_mon.get_game_metrics().name) + " (PID " + std::to_string(proc_mon.get_game_pid()) + ")";

    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("##GameSelect", game_name_utf8.c_str())) {
        for (int i = 0; i < (int)cached_proc_list_.size(); ++i) {
            const auto& p = cached_proc_list_[i];
            std::string label = wide_to_utf8(p.name) + " [" + std::to_string(p.pid) + "]";
            bool is_selected = (proc_mon.get_game_pid() == p.pid);
            if (ImGui::Selectable(label.c_str(), is_selected)) {
                proc_mon.set_game_pid(p.pid);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("Auto-Detect CS2")) {
        proc_mon.auto_detect_game();
    }

    ImGui::SameLine();
    ImGui::Text(" | Aux Process / Mod:");
    ImGui::SameLine();

    std::string aux_name_utf8 = proc_mon.get_aux_metrics().name.empty()
        ? "None"
        : wide_to_utf8(proc_mon.get_aux_metrics().name) + " (PID " + std::to_string(proc_mon.get_aux_pid()) + ")";

    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("##AuxSelect", aux_name_utf8.c_str())) {
        if (ImGui::Selectable("None (Disable Aux Tracking)", proc_mon.get_aux_pid() == 0)) {
            proc_mon.set_aux_pid(0);
        }
        for (int i = 0; i < (int)cached_proc_list_.size(); ++i) {
            const auto& p = cached_proc_list_[i];
            std::string label = wide_to_utf8(p.name) + " [" + std::to_string(p.pid) + "]";
            bool is_selected = (proc_mon.get_aux_pid() == p.pid);
            if (ImGui::Selectable(label.c_str(), is_selected)) {
                proc_mon.set_aux_pid(p.pid);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh Process List")) {
        cached_proc_list_ = ProcessMonitor::enumerate_processes();
    }
}

void DashboardView::render_summary_cards(
    const ProcessMetrics& game,
    const ProcessMetrics& aux,
    const FrameStats& frame_stats
) {
    float avail_width = ImGui::GetContentRegionAvail().x;
    float card_width = (avail_width - 30.0f) / 4.0f;
    float card_height = 82.0f;

    // Card 1: Frame Rates
    ImGui::BeginChild("CardFPS", ImVec2(card_width, card_height), true);
    ImGui::TextDisabled("FRAME RATE & 1%% LOW");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.26f, 0.90f, 0.50f, 1.0f));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("%.1f FPS", frame_stats.current_fps);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Text("1%% Low: %.1f | FT: %.2f ms", frame_stats.one_percent_low, frame_stats.frametime_ms);
    ImGui::EndChild();

    ImGui::SameLine();

    // Card 2: CPU Attribution
    ImGui::BeginChild("CardCPU", ImVec2(card_width, card_height), true);
    ImGui::TextDisabled("CPU ATTRIBUTION");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.75f, 1.0f, 1.0f));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("Game: %.1f%%", game.cpu_percent);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Text("Aux Tool: %.2f%% | Overhead: +%.1f%%",
        aux.cpu_percent,
        (game.cpu_percent > 0.0) ? (aux.cpu_percent / game.cpu_percent * 100.0) : 0.0
    );
    ImGui::EndChild();

    ImGui::SameLine();

    // Card 3: RAM Footprint
    ImGui::BeginChild("CardRAM", ImVec2(card_width, card_height), true);
    ImGui::TextDisabled("PRIVATE RAM (COMMIT)");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.70f, 0.20f, 1.0f));
    ImGui::SetWindowFontScale(1.3f);
    float game_ram_mb = static_cast<float>(game.private_working_set) / (1024.0f * 1024.0f);
    float aux_ram_mb  = static_cast<float>(aux.private_working_set) / (1024.0f * 1024.0f);
    ImGui::Text("%.0f MB", game_ram_mb);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Text("Aux Private: %.1f MB", aux_ram_mb);
    ImGui::EndChild();

    ImGui::SameLine();

    // Card 4: Threads & Handles
    ImGui::BeginChild("CardThreads", ImVec2(card_width, card_height), true);
    ImGui::TextDisabled("THREADS & HANDLES");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.45f, 0.95f, 1.0f));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("%u Threads", game.thread_count);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Text("Game Hnd: %u | Aux Thrd: %u", game.handle_count, aux.thread_count);
    ImGui::EndChild();
}

void DashboardView::render_telemetry_plots() {
    float avail_width = ImGui::GetContentRegionAvail().x;
    float plot_height = 240.0f;

    // 1. Frametime Plot
    if (ImPlot::BeginPlot("Frametime History (ms)", ImVec2(avail_width, plot_height))) {
        ImPlot::SetupAxes("Frames", "Time (ms)", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 33.3, ImPlotCond_Once);

        std::vector<float> ft_data;
        frametime_history_.get_linear(ft_data);

        if (!ft_data.empty()) {
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.26f, 0.90f, 0.50f, 1.0f));
            ImPlot::PlotLine("Frametime", ft_data.data(), (int)ft_data.size());
            ImPlot::PopStyleColor();
        }

        // Target baseline lines: 16.6ms (60fps) and 6.94ms (144fps)
        double baseline_60 = 16.666;
        double baseline_144 = 6.944;
        ImPlot::TagY(baseline_60, ImVec4(0.9f, 0.3f, 0.3f, 0.8f), "60 FPS");
        ImPlot::TagY(baseline_144, ImVec4(0.3f, 0.8f, 0.9f, 0.8f), "144 FPS");

        ImPlot::EndPlot();
    }

    ImGui::Spacing();

    // 2. CPU Comparison Plot (Game vs Aux)
    float half_width = (avail_width - 15.0f) / 2.0f;

    if (ImPlot::BeginPlot("CPU Usage Comparison (%)", ImVec2(half_width, plot_height))) {
        ImPlot::SetupAxes("Samples", "CPU %", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 100.0, ImPlotCond_Always);

        std::vector<float> game_cpu, aux_cpu;
        game_cpu_history_.get_linear(game_cpu);
        aux_cpu_history_.get_linear(aux_cpu);

        if (!game_cpu.empty()) {
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.35f, 0.75f, 1.0f, 1.0f));
            ImPlot::PlotLine("Game Process", game_cpu.data(), (int)game_cpu.size());
            ImPlot::PopStyleColor();
        }
        if (!aux_cpu.empty()) {
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.95f, 0.35f, 0.45f, 1.0f));
            ImPlot::PlotLine("Aux Tool / Mod", aux_cpu.data(), (int)aux_cpu.size());
            ImPlot::PopStyleColor();
        }

        ImPlot::EndPlot();
    }

    ImGui::SameLine();

    // 3. RAM Footprint Comparison Plot (MB)
    if (ImPlot::BeginPlot("Memory Footprint (MB)", ImVec2(half_width, plot_height))) {
        ImPlot::SetupAxes("Samples", "MB", ImPlotAxisFlags_None, ImPlotAxisFlags_None);

        std::vector<float> game_ram, aux_ram;
        game_ram_history_mb_.get_linear(game_ram);
        aux_ram_history_mb_.get_linear(aux_ram);

        if (!game_ram.empty()) {
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.95f, 0.70f, 0.20f, 1.0f));
            ImPlot::PlotLine("Game Private RAM", game_ram.data(), (int)game_ram.size());
            ImPlot::PopStyleColor();
        }
        if (!aux_ram.empty()) {
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.85f, 0.45f, 0.95f, 1.0f));
            ImPlot::PlotLine("Aux Tool RAM", aux_ram.data(), (int)aux_ram.size());
            ImPlot::PopStyleColor();
        }

        ImPlot::EndPlot();
    }
}

void DashboardView::render_thread_inspector(ThreadProfiler& thread_prof, DWORD target_pid) {
    if (target_pid == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Please select a target process first to profile threads.");
        return;
    }

    ImGui::Text("Active Threads: %zu | Sorted by CPU Cycle Delta (Real-time Execution)", thread_prof.get_thread_count());
    ImGui::Separator();

    static ImGuiTableFlags flags =
        ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("ThreadTable", 5, flags, ImVec2(0, 400))) {
        ImGui::TableSetupColumn("Thread ID (TID)", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Relative CPU %", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Cycle Delta / Frame", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Start Address", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Module Origin", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        const auto& threads = thread_prof.get_sorted_threads();
        for (const auto& t : threads) {
            ImGui::TableNextRow();

            // TID
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%u", t.tid);

            // CPU %
            ImGui::TableSetColumnIndex(1);
            if (t.cpu_percent > 15.0) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%.1f%%", t.cpu_percent);
            } else {
                ImGui::Text("%.1f%%", t.cpu_percent);
            }

            // Cycle Delta
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%llu", t.cycle_delta);

            // Start Address
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("0x%llX", static_cast<unsigned long long>(t.start_address));

            // Module Name
            ImGui::TableSetColumnIndex(4);
            std::string mod_utf8 = wide_to_utf8(t.module_name);
            if (mod_utf8 == "[Unknown/JIT]") {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s (Possible injected code)", mod_utf8.c_str());
            } else {
                ImGui::Text("%s", mod_utf8.c_str());
            }
        }

        ImGui::EndTable();
    }
}

void DashboardView::render_benchmark_tab(
    const ProcessMetrics& game,
    const ProcessMetrics& aux,
    const FrameStats& frames
) {
    ImGui::Text("A/B Impact Benchmark Module");
    ImGui::TextDisabled("Run controlled 30s-60s benchmarks with and without your tool to calculate the exact performance tax.");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(150);
    ImGui::SliderInt("Benchmark Duration (sec)", &benchmark_duration_sec_, 10, 120);

    if (!is_benchmarking_) {
        if (ImGui::Button("Start Benchmark Run", ImVec2(200, 32))) {
            is_benchmarking_ = true;
            benchmark_start_time_ = ImGui::GetTime();
        }
    } else {
        double elapsed = ImGui::GetTime() - benchmark_start_time_;
        float progress = static_cast<float>(elapsed / benchmark_duration_sec_);
        if (progress > 1.0f) progress = 1.0f;

        ImGui::ProgressBar(progress, ImVec2(300, 28));
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            is_benchmarking_ = false;
        }

        if (elapsed >= benchmark_duration_sec_) {
            is_benchmarking_ = false;
            BenchmarkRecord rec;
            rec.session_name = (aux.pid != 0) ? L"With Aux Tool" : L"Baseline (Game Only)";
            rec.avg_fps = frames.avg_fps > 0 ? frames.avg_fps : frames.current_fps;
            rec.one_percent_low = frames.one_percent_low;
            rec.avg_frametime_ms = frames.frametime_ms;
            rec.avg_game_cpu_percent = game.cpu_percent;
            rec.avg_aux_cpu_percent = aux.cpu_percent;
            rec.avg_aux_ram_mb = aux.private_working_set / (1024 * 1024);
            rec.duration_seconds = benchmark_duration_sec_;
            benchmark_history_.push_back(rec);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Completed Benchmark Sessions:");

    if (benchmark_history_.empty()) {
        ImGui::TextDisabled("No benchmark sessions recorded yet.");
    } else {
        for (size_t i = 0; i < benchmark_history_.size(); ++i) {
            const auto& b = benchmark_history_[i];
            ImGui::BulletText("Session #%zu [%s]: Avg FPS: %.1f | 1%% Low: %.1f | Aux CPU: %.2f%% | Aux RAM: %zu MB",
                i + 1,
                wide_to_utf8(b.session_name).c_str(),
                b.avg_fps,
                b.one_percent_low,
                b.avg_aux_cpu_percent,
                b.avg_aux_ram_mb
            );
        }
    }
}

void DashboardView::render_mini_hud(
    const ProcessMetrics& game,
    const ProcessMetrics& aux,
    const FrameStats& frames,
    OverlayMode& current_mode
) {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(340, 200), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.05f, 0.07f, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.26f, 0.78f, 0.86f, 0.6f));

    ImGui::Begin("AnyPerfMiniHud", nullptr, flags);

    ImGui::TextColored(ImVec4(0.26f, 0.78f, 0.86f, 1.0f), "ANYPERFOMANS HUD");
    ImGui::SameLine(ImGui::GetWindowWidth() - 70.0f);
    if (ImGui::SmallButton("Full [F11]")) {
        current_mode = OverlayMode::Dashboard;
    }

    ImGui::Separator();

    // FPS
    ImGui::Text("FPS: %.1f  (1%% Low: %.1f)", frames.current_fps, frames.one_percent_low);
    ImGui::Text("Frametime: %.2f ms", frames.frametime_ms);

    // Game stats
    float game_mb = static_cast<float>(game.private_working_set) / (1024.0f * 1024.0f);
    ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "Game: %.1f%% CPU | %.0f MB RAM", game.cpu_percent, game_mb);

    // Aux tool stats
    if (aux.pid != 0) {
        float aux_mb = static_cast<float>(aux.private_working_set) / (1024.0f * 1024.0f);
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.45f, 1.0f), "Aux : %.2f%% CPU | %.1f MB RAM", aux.cpu_percent, aux_mb);

        double overhead = (game.cpu_percent > 0.0) ? (aux.cpu_percent / game.cpu_percent * 100.0) : 0.0;
        ImGui::TextDisabled("Tool Impact: +%.1f%% CPU overhead", overhead);
    } else {
        ImGui::TextDisabled("Aux: not configured");
    }

    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

} // namespace anyperf
