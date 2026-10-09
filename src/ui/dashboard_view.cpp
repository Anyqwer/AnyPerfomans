#include "anyperf/dashboard_view.hpp"
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
    const FrameStats& frame_stats
) {
    frametime_history_.push(static_cast<float>(frame_stats.frametime_ms));

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
    const FrameStats& frame_stats,
    OverlayMode& current_mode
) {
    if (current_mode == OverlayMode::MiniHud) {
        render_mini_hud(proc_mon, frame_stats, current_mode);
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
    render_process_tags_bar(proc_mon);

    ImGui::Separator();
    ImGui::Spacing();

    render_summary_cards(proc_mon, frame_stats);

    ImGui::Spacing();

    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Telemetry & Multi-Process Charts")) {
            render_telemetry_plots(proc_mon);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Thread Inspector")) {
            render_thread_inspector(thread_prof, proc_mon);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Comparative Impact Benchmark")) {
            render_benchmark_tab(proc_mon, frame_stats);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void DashboardView::render_top_bar(ProcessMonitor& proc_mon, OverlayMode& current_mode) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.26f, 0.78f, 0.95f, 1.0f));
    ImGui::Text("ANYPERFOMANS");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("v0.1.0 | High-Precision Multi-Process Telemetry & Overhead Profiler");

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
    const FrameStats& frame_stats
) {
    float avail_width = ImGui::GetContentRegionAvail().x;
    float card_width = (avail_width - 30.0f) / 4.0f;
    float card_height = 84.0f;

    const auto* primary = proc_mon.get_primary_target();
    double total_sec_cpu = proc_mon.get_total_secondary_cpu();
    size_t total_sec_ram = proc_mon.get_total_secondary_ram();

    // Card 1: Frame Rates & Latency
    ImGui::BeginChild("CardFPS", ImVec2(card_width, card_height), true);
    ImGui::TextDisabled("FRAME TIMING");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.26f, 0.90f, 0.50f, 1.0f));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("%.1f FPS", frame_stats.current_fps);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Text("1%% Low: %.1f | FT: %.2f ms", frame_stats.one_percent_low, frame_stats.frametime_ms);
    ImGui::EndChild();

    ImGui::SameLine();

    // Card 2: Primary Process
    ImGui::BeginChild("CardPrimary", ImVec2(card_width, card_height), true);
    ImGui::TextDisabled("PRIMARY TARGET");
    if (primary && primary->is_alive) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(primary->color.r, primary->color.g, primary->color.b, 1.0f));
        ImGui::SetWindowFontScale(1.3f);
        ImGui::Text("%.1f%% CPU", primary->cpu_percent);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        float ram_mb = static_cast<float>(primary->private_working_set) / (1024.0f * 1024.0f);
        ImGui::Text("%s | %.0f MB RAM", wide_to_utf8(primary->name).c_str(), ram_mb);
    } else {
        ImGui::TextDisabled("No primary target");
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Card 3: Secondary Background Total
    ImGui::BeginChild("CardSecondary", ImVec2(card_width, card_height), true);
    ImGui::TextDisabled("SECONDARY APPS OVERHEAD");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.45f, 1.0f));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("+%.2f%% CPU", total_sec_cpu);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    float sec_ram_mb = static_cast<float>(total_sec_ram) / (1024.0f * 1024.0f);
    double overhead_ratio = (primary && primary->cpu_percent > 0.0) ? (total_sec_cpu / primary->cpu_percent * 100.0) : 0.0;
    ImGui::Text("%.0f MB Total RAM | +%.1f%% Tax", sec_ram_mb, overhead_ratio);
    ImGui::EndChild();

    ImGui::SameLine();

    // Card 4: Active Targets Count
    ImGui::BeginChild("CardTargets", ImVec2(card_width, card_height), true);
    ImGui::TextDisabled("MONITORED TARGETS");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.45f, 0.95f, 1.0f));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("%zu Targets", proc_mon.get_monitored_targets().size());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    size_t total_threads = 0;
    for (const auto& t : proc_mon.get_monitored_targets()) total_threads += t.thread_count;
    ImGui::Text("Total Threads: %zu", total_threads);
    ImGui::EndChild();
}

void DashboardView::render_telemetry_plots(const ProcessMonitor& proc_mon) {
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
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.26f, 0.90f, 0.50f, 1.0f));
            ImPlot::PlotLine("Frametime", ft_data.data(), (int)ft_data.size());
            ImPlot::PopStyleColor();
        }

        ImPlot::TagY(16.666, ImVec4(0.9f, 0.3f, 0.3f, 0.8f), "60 FPS (16.6ms)");
        ImPlot::TagY(6.944, ImVec4(0.3f, 0.8f, 0.9f, 0.8f), "144 FPS (6.9ms)");

        ImPlot::EndPlot();
    }

    ImGui::Spacing();

    // 2. Multi-Series CPU Comparison Plot
    float half_width = (avail_width - 15.0f) / 2.0f;

    if (ImPlot::BeginPlot("Multi-Process CPU Utilization (%)", ImVec2(half_width, plot_height))) {
        ImPlot::SetupAxes("Timeline", "CPU %", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 100.0, ImPlotCond_Always);

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
    if (ImPlot::BeginPlot("Multi-Process Private RAM Footprint (MB)", ImVec2(half_width, plot_height))) {
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

    // Default inspector PID to primary target
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
            rec.session_name = primary ? (L"Run with " + primary->name) : L"Multi-Target Run";
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
        }
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

void DashboardView::render_mini_hud(
    const ProcessMonitor& proc_mon,
    const FrameStats& frames,
    OverlayMode& current_mode
) {
    const auto& targets = proc_mon.get_monitored_targets();
    float hud_height = 140.0f + static_cast<float>(targets.size()) * 26.0f;
    if (hud_height < 180.0f) hud_height = 180.0f;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(360, hud_height), ImGuiCond_Always);

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
    ImGui::SameLine(ImGui::GetWindowWidth() - 75.0f);
    if (ImGui::SmallButton("Full [F11]")) {
        current_mode = OverlayMode::Dashboard;
    }

    ImGui::Separator();

    // FPS
    ImGui::Text("FPS: %.1f  (1%% Low: %.1f)", frames.current_fps, frames.one_percent_low);
    ImGui::Text("Frametime: %.2f ms", frames.frametime_ms);

    ImGui::Separator();

    if (targets.empty()) {
        ImGui::TextDisabled("No targets configured");
    } else {
        for (const auto& t : targets) {
            float ram_mb = static_cast<float>(t.private_working_set) / (1024.0f * 1024.0f);
            ImVec4 col(t.color.r, t.color.g, t.color.b, 1.0f);

            std::string prefix = t.is_primary ? "[P] " : "    ";
            ImGui::TextColored(col, "%s%-14s: %5.1f%% | %4.0f MB",
                prefix.c_str(),
                wide_to_utf8(t.name).substr(0, 14).c_str(),
                t.cpu_percent,
                ram_mb
            );
        }

        double sec_cpu = proc_mon.get_total_secondary_cpu();
        if (sec_cpu > 0.0) {
            ImGui::TextDisabled("Secondary Total: +%.2f%% CPU overhead", sec_cpu);
        }
    }

    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

} // namespace anyperf
