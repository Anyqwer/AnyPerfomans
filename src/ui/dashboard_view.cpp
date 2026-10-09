#include "anyperf/dashboard_view.hpp"
#include "anyperf/etw_tracker.hpp"
#include "anyperf/dx11_backend.hpp"
#include "anyperf/report_generator.hpp"
#include <imgui.h>
#include <implot.h>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace anyperf {

static std::string wide_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &result[0], size, nullptr, nullptr);
    return result;
}

static bool stristr_simple(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); }
    );
    return it != haystack.end();
}

DashboardView::DashboardView() {
    cached_proc_list_ = ProcessMonitor::enumerate_processes();
}

void DashboardView::push_metrics_sample(
    const std::vector<ProcessMetrics>& targets,
    const FrameStats& frame_stats,
    const GpuMetrics& gpu_metrics
) {
    frametime_history_.push(static_cast<float>(frame_stats.frametime_ms));
    gpu_usage_history_.push(static_cast<float>(gpu_metrics.core_usage_percent));

    if (is_benchmarking_) {
        current_benchmark_frametimes_.push_back(static_cast<float>(frame_stats.frametime_ms));
    }

    for (const auto& target : targets) {
        if (cpu_histories_.find(target.pid) == cpu_histories_.end()) {
            cpu_histories_[target.pid] = std::make_unique<RingBuffer<float, 300>>();
            ram_histories_mb_[target.pid] = std::make_unique<RingBuffer<float, 300>>();
        }

        cpu_histories_[target.pid]->push(static_cast<float>(target.cpu_percent));

        float ram_mb = static_cast<float>(target.private_working_set) / (1024.0f * 1024.0f);
        ram_histories_mb_[target.pid]->push(ram_mb);
    }
}

void DashboardView::render(
    ProcessMonitor& proc_mon,
    ThreadProfiler& thread_prof,
    EtwTracker& etw_tracker,
    GpuMonitor& gpu_mon,
    ConfigManager& config_mgr,
    Dx11Backend& backend,
    const FrameStats& frame_stats,
    OverlayMode& current_mode
) {
    if (current_mode == OverlayMode::MiniHud) {
        render_rtss_mini_hud(proc_mon, etw_tracker, gpu_mon.get_metrics(), config_mgr, backend, frame_stats, current_mode);
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

    render_top_bar(proc_mon, etw_tracker, current_mode);
    render_process_tags_bar(proc_mon);

    ImGui::Separator();
    ImGui::Spacing();

    render_summary_cards(proc_mon, etw_tracker, gpu_mon.get_metrics(), frame_stats);

    ImGui::Spacing();

    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Telemetry & Multi-Process Charts")) {
            render_telemetry_plots(proc_mon, gpu_mon.get_metrics());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Thread Inspector")) {
            render_thread_inspector(thread_prof, proc_mon);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Comparative Impact Benchmark")) {
            render_benchmark_tab(proc_mon, gpu_mon.get_metrics(), frame_stats);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("HUD & Hotkey Settings")) {
            render_settings_tab(config_mgr);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void DashboardView::render_top_bar(ProcessMonitor& proc_mon, EtwTracker& etw_tracker, OverlayMode& current_mode) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.22f, 0.32f, 1.0f));
    ImGui::Text("⚡ ANYPERFOMANS");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("v0.2.0 | ROG Liquid Glass Edition");

    // ETW Status Indicator
    ImGui::SameLine();
    if (etw_tracker.is_active()) {
        ImGui::TextColored(ImVec4(0.00f, 0.90f, 0.45f, 1.0f), "[Kernel ETW: Active]");
    } else if (etw_tracker.get_state() == EtwState::AccessDenied) {
        ImGui::TextColored(ImVec4(1.00f, 0.55f, 0.20f, 1.0f), "[ETW: Non-Elevated]");
        ImGui::SameLine();
        if (ImGui::SmallButton("Restart as Admin")) {
            EtwTracker::relaunch_as_admin();
        }
    } else {
        ImGui::TextDisabled("[ETW: Standby]");
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 280.0f);
    if (ImGui::Button("Switch to Mini HUD [F11]", ImVec2(260, 26))) {
        current_mode = OverlayMode::MiniHud;
    }

    ImGui::Spacing();

    // Process Search & Add Control
    ImGui::Text("Add Process to Monitor:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##FilterQuery", "Filter (e.g. discord, chrome)", process_search_query_, sizeof(process_search_query_));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("##ProcessSelectCombo", "Select from active processes...")) {
        std::string query = process_search_query_;
        for (const auto& p : cached_proc_list_) {
            std::string name_utf8 = wide_to_utf8(p.name);
            if (!query.empty() && !stristr_simple(name_utf8, query)) {
                continue;
            }

            std::string label = name_utf8 + " [" + std::to_string(p.pid) + "]";
            bool already_monitored = proc_mon.is_monitored(p.pid);

            if (already_monitored) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            }

            if (ImGui::Selectable(label.c_str(), false, already_monitored ? ImGuiSelectableFlags_Disabled : 0)) {
                proc_mon.add_target(p.pid, p.name);
            }

            if (already_monitored) {
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh List")) {
        cached_proc_list_ = ProcessMonitor::enumerate_processes();
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear All")) {
        proc_mon.clear_targets();
        cpu_histories_.clear();
        ram_histories_mb_.clear();
    }
}

void DashboardView::render_process_tags_bar(ProcessMonitor& proc_mon) {
    const auto& targets = proc_mon.get_monitored_targets();
    if (targets.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "No processes selected. Select 2 or more processes above to start profiling.");
        return;
    }

    ImGui::TextDisabled("Active Targets:");
    ImGui::SameLine();

    DWORD to_remove_pid = 0;
    DWORD to_primary_pid = 0;

    for (const auto& t : targets) {
        ImGui::PushID(static_cast<int>(t.pid));

        ImVec4 col(t.color.r, t.color.g, t.color.b, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x * 0.25f, col.y * 0.25f, col.z * 0.25f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x * 0.40f, col.y * 0.40f, col.z * 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, col);

        std::string badge = (t.is_primary ? "[PRIMARY] " : "") + wide_to_utf8(t.name) + " (" + std::to_string(t.pid) + ")";
        if (ImGui::Button(badge.c_str())) {
            to_primary_pid = t.pid;
        }

        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 2);
        if (ImGui::SmallButton("X")) {
            to_remove_pid = t.pid;
        }
        ImGui::SameLine(0, 8);

        ImGui::PopID();
    }

    ImGui::NewLine();

    if (to_remove_pid != 0) {
        proc_mon.remove_target(to_remove_pid);
        cpu_histories_.erase(to_remove_pid);
        ram_histories_mb_.erase(to_remove_pid);
    }
    if (to_primary_pid != 0) {
        proc_mon.set_primary_target(to_primary_pid);
    }
}

void DashboardView::render_summary_cards(
    const ProcessMonitor& proc_mon,
    const EtwTracker& etw_tracker,
    const GpuMetrics& gpu,
    const FrameStats& frame_stats
) {
    float avail_width = ImGui::GetContentRegionAvail().x;
    float card_width = (avail_width - 30.0f) / 4.0f;
    float card_height = 86.0f;

    const auto* primary = proc_mon.get_primary_target();
    double total_sec_cpu = proc_mon.get_total_secondary_cpu();
    size_t total_sec_ram = proc_mon.get_total_secondary_ram();

    // Card 1: Frame Rates & Latency
    ImGui::BeginChild("CardFPS", ImVec2(card_width, card_height), true);
    if (etw_tracker.is_active()) {
        ImGui::TextColored(ImVec4(0.00f, 0.90f, 0.45f, 1.0f), "KERNEL PRESENTMON (ETW)");
    } else {
        ImGui::TextDisabled("FRAME TIMING (LOCAL)");
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.90f, 0.45f, 1.0f));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("%.1f FPS", frame_stats.current_fps);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Text("1%% Low: %.1f | FT: %.2f ms", frame_stats.one_percent_low, frame_stats.frametime_ms);
    ImGui::EndChild();

    ImGui::SameLine();

    // Card 2: GPU Telemetry Card
    ImGui::BeginChild("CardGPU", ImVec2(card_width, card_height), true);
    ImGui::TextDisabled("GPU TELEMETRY");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.30f, 0.40f, 1.0f));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("%d%% GPU", gpu.core_usage_percent);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    float vram_mb = static_cast<float>(gpu.vram_used_bytes) / (1024.0f * 1024.0f);
    if (gpu.temperature_c > 0) {
        ImGui::Text("%d°C | VRAM: %.0f MB | %d MHz", gpu.temperature_c, vram_mb, gpu.core_clock_mhz);
    } else {
        ImGui::Text("VRAM: %.0f MB (%s)", vram_mb, wide_to_utf8(gpu.adapter_name).substr(0, 15).c_str());
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Card 3: Primary Process
    ImGui::BeginChild("CardPrimary", ImVec2(card_width, card_height), true);
    ImGui::TextDisabled("PRIMARY TARGET");
    if (primary && primary->is_alive) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(primary->color.r, primary->color.g, primary->color.b, 1.0f));
        ImGui::SetWindowFontScale(1.3f);
        ImGui::Text("%.1f%% CPU", primary->cpu_percent);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        float ram_mb = static_cast<float>(primary->private_working_set) / (1024.0f * 1024.0f);
        ImGui::Text("%s | %.0f MB Private", wide_to_utf8(primary->name).c_str(), ram_mb);
    } else {
        ImGui::TextDisabled("No primary target selected");
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Card 4: Secondary Background Overhead Total
    ImGui::BeginChild("CardSecondary", ImVec2(card_width, card_height), true);
    ImGui::TextDisabled("SECONDARY APPS OVERHEAD");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.22f, 0.32f, 1.0f));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("+%.2f%% CPU", total_sec_cpu);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    float sec_ram_mb = static_cast<float>(total_sec_ram) / (1024.0f * 1024.0f);
    double overhead_ratio = (primary && primary->cpu_percent > 0.0) ? (total_sec_cpu / primary->cpu_percent * 100.0) : 0.0;
    ImGui::Text("%.0f MB Total RAM | +%.1f%% Tax", sec_ram_mb, overhead_ratio);
    ImGui::EndChild();
}

void DashboardView::render_telemetry_plots(const ProcessMonitor& proc_mon, const GpuMetrics& gpu) {
    float avail_width = ImGui::GetContentRegionAvail().x;
    float plot_height = 230.0f;
    const auto& targets = proc_mon.get_monitored_targets();

    // 1. Frametime Plot
    if (ImPlot::BeginPlot("Frametime Latency Timeline (ms)", ImVec2(avail_width, plot_height))) {
        ImPlot::SetupAxes("Frames", "Time (ms)", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 33.3, ImPlotCond_Once);

        std::vector<float> ft_data;
        frametime_history_.get_linear(ft_data);

        if (!ft_data.empty()) {
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1.00f, 0.24f, 0.32f, 1.0f));
            ImPlot::PlotLine("Frametime", ft_data.data(), (int)ft_data.size());
            ImPlot::PopStyleColor();
        }

        ImPlot::TagY(16.666, ImVec4(0.9f, 0.3f, 0.3f, 0.8f), "60 FPS (16.6ms)");
        ImPlot::TagY(6.944, ImVec4(0.0f, 0.85f, 0.5f, 0.8f), "144 FPS (6.9ms)");

        ImPlot::EndPlot();
    }

    ImGui::Spacing();

    // 2. Multi-Series CPU & GPU Utilization
    float half_width = (avail_width - 15.0f) / 2.0f;

    if (ImPlot::BeginPlot("Hardware & Process Load (%)", ImVec2(half_width, plot_height))) {
        ImPlot::SetupAxes("Timeline", "Load %", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 100.0, ImPlotCond_Always);

        // GPU Line
        std::vector<float> gpu_data;
        gpu_usage_history_.get_linear(gpu_data);
        if (!gpu_data.empty()) {
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1.00f, 0.40f, 0.20f, 1.0f));
            ImPlot::PlotLine("GPU Core Load %", gpu_data.data(), (int)gpu_data.size());
            ImPlot::PopStyleColor();
        }

        for (const auto& t : targets) {
            auto it = cpu_histories_.find(t.pid);
            if (it != cpu_histories_.end()) {
                std::vector<float> cpu_data;
                it->second->get_linear(cpu_data);
                if (!cpu_data.empty()) {
                    std::string label = wide_to_utf8(t.name) + " [" + std::to_string(t.pid) + "]";
                    ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(t.color.r, t.color.g, t.color.b, 1.0f));
                    ImPlot::PlotLine(label.c_str(), cpu_data.data(), (int)cpu_data.size());
                    ImPlot::PopStyleColor();
                }
            }
        }

        ImPlot::EndPlot();
    }

    ImGui::SameLine();

    // 3. Multi-Series RAM Comparison Plot
    if (ImPlot::BeginPlot("Process Memory Footprint (MB)", ImVec2(half_width, plot_height))) {
        ImPlot::SetupAxes("Timeline", "MB", ImPlotAxisFlags_None, ImPlotAxisFlags_None);

        for (const auto& t : targets) {
            auto it = ram_histories_mb_.find(t.pid);
            if (it != ram_histories_mb_.end()) {
                std::vector<float> ram_data;
                it->second->get_linear(ram_data);
                if (!ram_data.empty()) {
                    std::string label = wide_to_utf8(t.name) + " [" + std::to_string(t.pid) + "]";
                    ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(t.color.r, t.color.g, t.color.b, 1.0f));
                    ImPlot::PlotLine(label.c_str(), ram_data.data(), (int)ram_data.size());
                    ImPlot::PopStyleColor();
                }
            }
        }

        ImPlot::EndPlot();
    }
}

void DashboardView::render_thread_inspector(
    ThreadProfiler& thread_prof,
    const ProcessMonitor& proc_mon
) {
    const auto& targets = proc_mon.get_monitored_targets();
    if (targets.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Add at least one process to inspect its active threads.");
        return;
    }

    if (selected_inspector_pid_ == 0 || !proc_mon.is_monitored(selected_inspector_pid_)) {
        const auto* primary = proc_mon.get_primary_target();
        if (primary) selected_inspector_pid_ = primary->pid;
    }

    ImGui::Text("Select Process to Profile Threads:");
    ImGui::SameLine();

    const auto* cur_target = proc_mon.get_target(selected_inspector_pid_);
    std::string cur_name = cur_target ? (wide_to_utf8(cur_target->name) + " (" + std::to_string(cur_target->pid) + ")") : "Select...";

    ImGui::SetNextItemWidth(300);
    if (ImGui::BeginCombo("##ThreadInspectorSelect", cur_name.c_str())) {
        for (const auto& t : targets) {
            std::string label = wide_to_utf8(t.name) + " [" + std::to_string(t.pid) + "]";
            if (ImGui::Selectable(label.c_str(), selected_inspector_pid_ == t.pid)) {
                selected_inspector_pid_ = t.pid;
                thread_prof.update(t.pid);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Active Threads: %zu | Microsecond CPU Cycle Attribution", thread_prof.get_thread_count());

    static ImGuiTableFlags flags =
        ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("ThreadTable", 5, flags, ImVec2(0, 380))) {
        ImGui::TableSetupColumn("Thread ID (TID)", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Relative Load %", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Cycle Delta / Sample", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Start Address", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Module Origin", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        const auto& threads = thread_prof.get_sorted_threads();
        for (const auto& t : threads) {
            ImGui::TableNextRow();

            // TID
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%u", t.tid);

            // Load %
            ImGui::TableSetColumnIndex(1);
            if (t.cpu_percent > 20.0) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%.1f%%", t.cpu_percent);
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
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s (Dynamic / JIT / Injected)", mod_utf8.c_str());
            } else {
                ImGui::Text("%s", mod_utf8.c_str());
            }
        }

        ImGui::EndTable();
    }
}

void DashboardView::render_benchmark_tab(
    const ProcessMonitor& proc_mon,
    const GpuMetrics& gpu,
    const FrameStats& frames
) {
    ImGui::Text("Comparative Overhead Benchmark");
    ImGui::TextDisabled("Run controlled sampling sessions to compute the exact resource footprint across all monitored applications.");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(160);
    ImGui::SliderInt("Session Duration (sec)", &benchmark_duration_sec_, 10, 120);

    if (!is_benchmarking_) {
        if (ImGui::Button("Start Benchmark Session", ImVec2(220, 32))) {
            is_benchmarking_ = true;
            benchmark_start_time_ = ImGui::GetTime();
            current_benchmark_frametimes_.clear();
        }
    } else {
        double elapsed = ImGui::GetTime() - benchmark_start_time_;
        float progress = static_cast<float>(elapsed / benchmark_duration_sec_);
        if (progress > 1.0f) progress = 1.0f;

        ImGui::ProgressBar(progress, ImVec2(320, 28));
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            is_benchmarking_ = false;
        }

        if (elapsed >= benchmark_duration_sec_) {
            is_benchmarking_ = false;
            BenchmarkRecord rec;
            const auto* primary = proc_mon.get_primary_target();
            rec.session_name = primary ? (L"Benchmark of " + primary->name) : L"Multi-Target Benchmark";
            rec.avg_fps = frames.avg_fps > 0 ? frames.avg_fps : frames.current_fps;
            rec.one_percent_low = frames.one_percent_low;
            rec.avg_frametime_ms = frames.frametime_ms;
            rec.duration_seconds = benchmark_duration_sec_;

            for (const auto& t : proc_mon.get_monitored_targets()) {
                BenchmarkProcessSummary sum;
                sum.pid = t.pid;
                sum.name = t.name;
                sum.avg_cpu_percent = t.cpu_percent;
                sum.avg_ram_mb = t.private_working_set / (1024 * 1024);
                rec.process_summaries.push_back(sum);
            }

            benchmark_history_.push_back(rec);

            // Auto-generate HTML report
            last_generated_report_path_ = ReportGenerator::generate_html_report(
                rec,
                current_benchmark_frametimes_,
                gpu
            );
        }
    }

    if (!last_generated_report_path_.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.65f, 0.35f, 0.9f));
        if (ImGui::Button("🌐 Open Interactive Browser Report & Share Card", ImVec2(380, 34))) {
            ReportGenerator::open_in_browser(last_generated_report_path_);
        }
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Recorded Benchmark Runs:");

    if (benchmark_history_.empty()) {
        ImGui::TextDisabled("No benchmark sessions recorded yet.");
    } else {
        for (size_t i = 0; i < benchmark_history_.size(); ++i) {
            const auto& b = benchmark_history_[i];
            ImGui::BulletText("Session #%zu [%s]: Avg FPS: %.1f | 1%% Low: %.1f | Targets: %zu",
                i + 1,
                wide_to_utf8(b.session_name).c_str(),
                b.avg_fps,
                b.one_percent_low,
                b.process_summaries.size()
            );
            for (const auto& p : b.process_summaries) {
                ImGui::Indent(20.0f);
                ImGui::TextDisabled("- %s: %.2f%% CPU | %zu MB Private RAM",
                    wide_to_utf8(p.name).c_str(),
                    p.avg_cpu_percent,
                    p.avg_ram_mb
                );
                ImGui::Unindent(20.0f);
            }
        }
    }
}

void DashboardView::render_settings_tab(ConfigManager& config_mgr) {
    auto& hud = config_mgr.get_hud();
    auto& hotkeys = config_mgr.get_hotkeys();

    ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.35f, 1.0f), "RTSS MINI-OVERLAY CUSTOMIZATION");
    ImGui::TextDisabled("Customize position, layout, visible hardware sensors, and the frametime graph.");
    ImGui::Spacing();

    // 1. Position & Layout
    ImGui::Text("Overlay Screen Position:");
    const char* pos_names[] = { "Top-Left", "Top-Right", "Bottom-Left", "Bottom-Right", "Custom (Free Coordinates)" };
    int current_pos = static_cast<int>(hud.position);
    ImGui::SetNextItemWidth(260);
    if (ImGui::Combo("##HudPosCombo", &current_pos, pos_names, IM_ARRAYSIZE(pos_names))) {
        hud.position = static_cast<HudPosition>(current_pos);
    }

    if (hud.position == HudPosition::Custom) {
        ImGui::Indent(20.0f);
        ImGui::SliderInt("Custom X Coordinate", &hud.custom_x, 0, 3840);
        ImGui::SliderInt("Custom Y Coordinate", &hud.custom_y, 0, 2160);
        ImGui::Unindent(20.0f);
    }

    ImGui::SliderFloat("HUD Background Glass Opacity", &hud.bg_opacity, 0.1f, 1.0f, "%.2f");
    ImGui::SliderFloat("HUD Font Scale", &hud.font_scale, 0.75f, 1.50f, "%.2fx");

    ImGui::Separator();
    ImGui::Spacing();

    // 2. Frametime Graph Customization
    ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.35f, 1.0f), "FRAMETIME GRAPH SETTINGS");
    ImGui::Checkbox("Show Frametime Graph in HUD", &hud.show_frametime_graph);
    if (hud.show_frametime_graph) {
        ImGui::Indent(20.0f);
        ImGui::SliderFloat("Graph Height (px)", &hud.frametime_graph_height, 20.0f, 90.0f, "%.0f px");
        ImGui::SliderFloat("Graph Scale Target (ms)", &hud.frametime_graph_max_ms, 16.6f, 60.0f, "%.1f ms");
        ImGui::TextDisabled("Default: 33.3 ms (30-60 FPS window). Line targets: 16.6ms (60 FPS), 6.9ms (144 FPS).");
        ImGui::Unindent(20.0f);
    }

    ImGui::Separator();
    ImGui::Spacing();

    // 3. Sensor Toggles
    ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.35f, 1.0f), "DISPLAYED TELEMETRY ROWS");
    ImGui::Columns(2, "SensorColumns", false);

    ImGui::Checkbox("Show FPS", &hud.show_fps);
    ImGui::Checkbox("Show 1% Low FPS", &hud.show_1pct_low);
    ImGui::Checkbox("Show 0.1% Low FPS", &hud.show_01pct_low);
    ImGui::Checkbox("Show Frametime (ms)", &hud.show_frametime_text);
    ImGui::Checkbox("Show GPU Core Load %", &hud.show_gpu_usage);

    ImGui::NextColumn();

    ImGui::Checkbox("Show GPU Temperature °C", &hud.show_gpu_temp);
    ImGui::Checkbox("Show GPU VRAM Usage", &hud.show_gpu_vram);
    ImGui::Checkbox("Show GPU Core Clock (MHz)", &hud.show_gpu_clock);
    ImGui::Checkbox("Show CPU Total Load %", &hud.show_cpu_usage);
    ImGui::Checkbox("Show Monitored Secondary Apps", &hud.show_secondary_apps);

    ImGui::Columns(1);

    ImGui::Separator();
    ImGui::Spacing();

    // 4. Hotkeys Configuration
    ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.35f, 1.0f), "HOTKEYS");
    ImGui::Text("Toggle HUD: %s (Press to toggle overlay)", ConfigManager::key_to_string(hotkeys.toggle_overlay_key).c_str());
    ImGui::Text("Toggle Benchmark: %s (Press to start/stop benchmark run)", ConfigManager::key_to_string(hotkeys.toggle_benchmark_key).c_str());

    ImGui::Spacing();
    if (ImGui::Button("💾 Save Configuration to Disk", ImVec2(240, 32))) {
        config_mgr.save_to_file();
    }
}

void DashboardView::render_rtss_mini_hud(
    const ProcessMonitor& proc_mon,
    const EtwTracker& etw_tracker,
    const GpuMetrics& gpu,
    const ConfigManager& config_mgr,
    Dx11Backend& backend,
    const FrameStats& frames,
    OverlayMode& current_mode
) {
    const auto& hud = config_mgr.get_hud();
    const auto& targets = proc_mon.get_monitored_targets();

    // Compute dynamic window dimensions
    float hud_width = 320.0f * hud.font_scale;
    float hud_height = 80.0f * hud.font_scale;

    if (hud.show_gpu_usage || hud.show_gpu_temp) hud_height += 24.0f * hud.font_scale;
    if (hud.show_cpu_usage) hud_height += 24.0f * hud.font_scale;
    if (hud.show_fps) hud_height += 24.0f * hud.font_scale;
    if (hud.show_frametime_graph) hud_height += (hud.frametime_graph_height + 14.0f) * hud.font_scale;
    if (hud.show_secondary_apps && !targets.empty()) {
        hud_height += (static_cast<float>(targets.size()) * 22.0f + 16.0f) * hud.font_scale;
    }

    // Screen positioning
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int hud_x = 20;
    int hud_y = 20;

    switch (hud.position) {
        case HudPosition::TopLeft:
            hud_x = 20; hud_y = 20;
            break;
        case HudPosition::TopRight:
            hud_x = screen_w - static_cast<int>(hud_width) - 20; hud_y = 20;
            break;
        case HudPosition::BottomLeft:
            hud_x = 20; hud_y = screen_h - static_cast<int>(hud_height) - 40;
            break;
        case HudPosition::BottomRight:
            hud_x = screen_w - static_cast<int>(hud_width) - 20;
            hud_y = screen_h - static_cast<int>(hud_height) - 40;
            break;
        case HudPosition::Custom:
            hud_x = hud.custom_x; hud_y = hud.custom_y;
            break;
    }

    backend.set_overlay_position(hud_x, hud_y, static_cast<int>(hud_width), static_cast<int>(hud_height));

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(hud_width, hud_height), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.06f, 0.08f, hud.bg_opacity));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.18f, 0.25f, 0.6f));

    ImGui::Begin("AnyPerfRTSSHud", nullptr, flags);
    ImGui::SetWindowFontScale(hud.font_scale);

    // Title / Switch button
    ImGui::TextColored(ImVec4(1.00f, 0.25f, 0.35f, 1.0f), "ANYPERFOMANS");
    ImGui::SameLine(ImGui::GetWindowWidth() - 75.0f * hud.font_scale);
    if (ImGui::SmallButton("Full [F11]")) {
        current_mode = OverlayMode::Dashboard;
    }
    ImGui::Separator();

    // 1. GPU Row
    if (hud.show_gpu_usage || hud.show_gpu_temp || hud.show_gpu_vram) {
        ImGui::TextColored(ImVec4(1.00f, 0.35f, 0.45f, 1.0f), "GPU : ");
        ImGui::SameLine();
        std::stringstream gpu_ss;
        if (hud.show_gpu_usage) gpu_ss << gpu.core_usage_percent << "% ";
        if (hud.show_gpu_temp && gpu.temperature_c > 0) gpu_ss << "| " << gpu.temperature_c << "°C ";
        if (hud.show_gpu_clock && gpu.core_clock_mhz > 0) gpu_ss << "| " << gpu.core_clock_mhz << " MHz ";
        if (hud.show_gpu_vram) {
            float vram_mb = static_cast<float>(gpu.vram_used_bytes) / (1024.0f * 1024.0f);
            gpu_ss << "| " << static_cast<int>(vram_mb) << " MB";
        }
        ImGui::Text("%s", gpu_ss.str().c_str());
    }

    // 2. CPU Row
    if (hud.show_cpu_usage) {
        const auto* primary = proc_mon.get_primary_target();
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "CPU : ");
        ImGui::SameLine();
        if (primary && primary->is_alive) {
            ImGui::Text("%.1f%%  (%s)", primary->cpu_percent, wide_to_utf8(primary->name).c_str());
        } else {
            ImGui::Text("Idle");
        }
    }

    // 3. FPS & 1% Low Row
    if (hud.show_fps) {
        ImGui::TextColored(ImVec4(0.00f, 0.90f, 0.45f, 1.0f), "FPS : ");
        ImGui::SameLine();
        std::stringstream fps_ss;
        fps_ss << std::fixed << std::setprecision(0) << frames.current_fps;
        if (hud.show_1pct_low) {
            fps_ss << "  (1%: " << std::fixed << std::setprecision(0) << frames.one_percent_low << ")";
        }
        if (hud.show_frametime_text) {
            fps_ss << "  FT: " << std::fixed << std::setprecision(1) << frames.frametime_ms << "ms";
        }
        if (etw_tracker.is_active()) {
            fps_ss << " [ETW]";
        }
        ImGui::Text("%s", fps_ss.str().c_str());
    }

    // 4. Configurable Frametime Graph
    if (hud.show_frametime_graph) {
        std::vector<float> ft_data;
        frametime_history_.get_linear(ft_data);
        if (!ft_data.empty()) {
            ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.00f, 0.25f, 0.35f, 1.0f));
            float graph_width = ImGui::GetContentRegionAvail().x;
            ImGui::PlotLines("##HudFrametimeGraph",
                ft_data.data(),
                static_cast<int>(ft_data.size()),
                0,
                nullptr,
                0.0f,
                hud.frametime_graph_max_ms,
                ImVec2(graph_width, hud.frametime_graph_height * hud.font_scale)
            );
            ImGui::PopStyleColor();
        }
    }

    // 5. Monitored Secondary Apps Breakdown
    if (hud.show_secondary_apps && !targets.empty()) {
        ImGui::Separator();
        for (const auto& t : targets) {
            if (t.is_primary) continue; // Skip primary, already displayed
            float ram_mb = static_cast<float>(t.private_working_set) / (1024.0f * 1024.0f);
            ImVec4 col(t.color.r, t.color.g, t.color.b, 1.0f);

            ImGui::TextColored(col, "%-12s: %4.1f%% | %3.0f MB",
                wide_to_utf8(t.name).substr(0, 12).c_str(),
                t.cpu_percent,
                ram_mb
            );
        }
    }

    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

} // namespace anyperf
