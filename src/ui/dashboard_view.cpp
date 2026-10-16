#include "anyperf/dashboard_view.hpp"
#include "anyperf/etw_tracker.hpp"
#include "anyperf/dx11_backend.hpp"
#include "anyperf/report_generator.hpp"
#include "anyperf/rtss_bridge.hpp"
#include "anyperf/bottleneck_analyzer.hpp"
#include "anyperf/tray_manager.hpp"
#include "anyperf/hardware_controller.hpp"
#include <imgui.h>
#include <implot.h>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

#include "anyperf/utils.hpp"

namespace anyperf {

static bool stristr_simple(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); }
    );
    return it != haystack.end();
}

static ImVec4 hex_to_imvec4(const std::string& hex, float alpha = 1.0f) {
    if (hex.length() < 6) return ImVec4(1.0f, 1.0f, 1.0f, alpha);
    unsigned int r = 255, g = 255, b = 255;
    sscanf_s(hex.c_str(), "%02x%02x%02x", &r, &g, &b);
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, alpha);
}

static bool ColorPickerHex(const char* label, std::string& hex_color) {
    float col[3] = { 1.0f, 1.0f, 1.0f };
    if (hex_color.length() >= 6) {
        unsigned int r = 255, g = 255, b = 255;
        sscanf_s(hex_color.c_str(), "%02x%02x%02x", &r, &g, &b);
        col[0] = r / 255.0f;
        col[1] = g / 255.0f;
        col[2] = b / 255.0f;
    }

    bool changed = false;
    if (ImGui::ColorEdit3(label, col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
        int r = std::clamp(static_cast<int>(col[0] * 255.0f + 0.5f), 0, 255);
        int g = std::clamp(static_cast<int>(col[1] * 255.0f + 0.5f), 0, 255);
        int b = std::clamp(static_cast<int>(col[2] * 255.0f + 0.5f), 0, 255);
        char hex_buf[8];
        sprintf_s(hex_buf, "%02X%02X%02X", r, g, b);
        hex_color = hex_buf;
        changed = true;
    }
    return changed;
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
    CpuMonitor& cpu_mon,
    HardwareController& hw_ctrl,
    ConfigManager& config_mgr,
    RTSSBridge& rtss_bridge,
    Dx11Backend& backend,
    const FrameStats& frame_stats,
    OverlayMode& current_mode
) {
    const auto& fonts = backend.get_fonts();

    if (current_mode == OverlayMode::MiniHud) {
        render_rtss_mini_hud(proc_mon, etw_tracker, gpu_mon.get_metrics(), cpu_mon.get_metrics(), config_mgr, backend, frame_stats, current_mode);
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

    // 1. Header with branding & status badges (Liquid Glass Style)
    render_header_bar(proc_mon, etw_tracker, gpu_mon, fonts, current_mode);

    // 2. High-Tech Segmented Navigation Bar with 5 Profiles
    render_navigation_bar(config_mgr, fonts);

    // 3. Process Chip Management Bar
    render_process_tags_bar(proc_mon, fonts);

    ImGui::Spacing();

    // 4. Liquid Glass Telemetry Cards
    render_summary_cards(proc_mon, etw_tracker, gpu_mon.get_metrics(), cpu_mon.get_metrics(), frame_stats, fonts);

    ImGui::Spacing();

    // 5. Active Tab View (6 Complete Modules)
    switch (active_tab_index_) {
        case 0:
            render_telemetry_plots(proc_mon, gpu_mon.get_metrics(), fonts);
            break;
        case 1:
            render_thread_inspector(thread_prof, proc_mon, fonts);
            break;
        case 2:
            render_fan_control_tab(hw_ctrl, config_mgr, gpu_mon.get_metrics(), fonts);
            break;
        case 3:
            render_overclock_tab(hw_ctrl, config_mgr, gpu_mon.get_metrics(), fonts);
            break;
        case 4:
            render_benchmark_tab(proc_mon, gpu_mon.get_metrics(), frame_stats, fonts);
            break;
        case 5:
            render_settings_tab(config_mgr, rtss_bridge, fonts);
            break;
    }

    ImGui::End();
}

void DashboardView::render_header_bar(
    ProcessMonitor& /*proc_mon*/,
    EtwTracker& etw_tracker,
    GpuMonitor& gpu_mon,
    const AppFonts& fonts,
    OverlayMode& current_mode
) {
    float s = fonts.dpi_scale;

    // Liquid Glass Edition Branding
    ImGui::PushFont(fonts.title);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 1.00f, 1.00f, 1.0f));
    ImGui::Text("ANYPERFOMANS");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::SameLine();
    ImGui::PushFont(fonts.bold);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.90f, 1.00f, 1.0f));
    ImGui::Text("LIQUID GLASS");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.65f, 0.9f), "| Hardware & Telemetry Suite");

    // Right-aligned status badges
    float badges_width = 460.0f * s;
    float start_x = ImGui::GetWindowWidth() - badges_width;
    if (start_x < 420.0f * s) start_x = 420.0f * s;
    ImGui::SameLine(start_x);

    // ETW Status Badge
    if (etw_tracker.is_active()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.35f, 0.18f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 1.00f, 0.50f, 1.0f));
        ImGui::Button("[ETW KERNEL: ACTIVE]");
        ImGui::PopStyleColor(2);
    } else if (etw_tracker.get_state() == EtwState::AccessDenied) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.40f, 0.20f, 0.05f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.60f, 0.20f, 1.0f));
        if (ImGui::Button("[! RUN AS ADMIN]")) {
            EtwTracker::relaunch_as_admin();
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.18f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.58f, 0.65f, 1.0f));
        ImGui::Button("[ETW: STANDBY]");
        ImGui::PopStyleColor(2);
    }

    ImGui::SameLine();

    // GPU Status Badge
    const auto& gpu_metrics = gpu_mon.get_metrics();
    std::string gpu_short_name = wide_to_utf8(gpu_metrics.adapter_name);
    if (gpu_short_name.length() > 20) {
        gpu_short_name = gpu_short_name.substr(0, 18) + "..";
    }
    std::string gpu_badge = "[" + gpu_short_name + "]";
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.18f, 0.22f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.88f, 0.95f, 1.0f));
    ImGui::Button(gpu_badge.c_str());
    ImGui::PopStyleColor(2);

    ImGui::SameLine();

    // Mini HUD Button (GPU-Z Close/Lookup Red Button Style)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.12f, 0.18f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.18f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.08f, 0.12f, 1.0f));
    if (ImGui::Button("[MINI HUD F11]", ImVec2(120.0f * s, 26.0f * s))) {
        current_mode = OverlayMode::MiniHud;
    }
    ImGui::PopStyleColor(3);

    ImGui::Spacing();
}

void DashboardView::render_navigation_bar(ConfigManager& config_mgr, const AppFonts& fonts) {
    const char* tabs[] = {
        "TELEMETRY",
        "THREADS",
        "FAN CONTROL",
        "TUNING & OC",
        "BENCHMARK",
        "MONITORING & OSD"
    };

    float s = fonts.dpi_scale;
    float tab_width = 138.0f * s;
    float tab_height = 30.0f * s;

    ImGui::PushFont(fonts.bold);

    for (int i = 0; i < 6; ++i) {
        bool is_active = (active_tab_index_ == i);

        if (is_active) {
            // Liquid Glass Active Tab: Crisp Cyan / Teal Glow with Crisp White Text
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.55f, 0.70f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.70f, 0.85f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            // Inactive Tab: Recessed Carbon Charcoal
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.09f, 0.12f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.16f, 0.22f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.85f, 0.95f, 0.85f));
        }

        if (ImGui::Button(tabs[i], ImVec2(tab_width, tab_height))) {
            active_tab_index_ = i;
        }

        ImGui::PopStyleColor(3);
        ImGui::SameLine(0, 5.0f * s);
    }

    // 5 Profile Slots Quick Selector on the Right Side
    float avail_w = ImGui::GetContentRegionAvail().x;
    float profile_btn_w = 34.0f * s;
    float total_profile_w = (profile_btn_w + 4.0f * s) * 5 + 75.0f * s;
    if (avail_w > total_profile_w) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - total_profile_w));
    }

    ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "PROFILE:");
    ImGui::SameLine(0, 6.0f * s);

    int cur_profile = config_mgr.get_active_profile_index();
    for (int p = 0; p < ConfigManager::NUM_PROFILES; ++p) {
        bool is_p_active = (cur_profile == p);
        if (is_p_active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.80f, 0.50f, 0.95f)); // Neon Emerald
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.10f, 0.08f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.18f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.75f, 0.85f, 1.0f));
        }
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.90f, 0.60f, 1.0f));

        std::string p_label = std::to_string(p + 1);
        if (ImGui::Button(p_label.c_str(), ImVec2(profile_btn_w, tab_height))) {
            config_mgr.switch_profile(p);
            config_mgr.save_to_file();
        }

        ImGui::PopStyleColor(3);
        if (p < ConfigManager::NUM_PROFILES - 1) {
            ImGui::SameLine(0, 4.0f * s);
        }
    }

    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void DashboardView::render_process_tags_bar(ProcessMonitor& proc_mon, const AppFonts& fonts) {
    float s = fonts.dpi_scale;

    // Filter & Add Search Bar (GPU-Z Aqua Labels)
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "PROCESS FILTER:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f * s);
    ImGui::InputTextWithHint("##FilterQuery", "Search process...", process_search_query_, sizeof(process_search_query_));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(260.0f * s);
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
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
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
    if (ImGui::Button("[Refresh]")) {
        cached_proc_list_ = ProcessMonitor::enumerate_processes();
    }

    ImGui::SameLine();
    if (ImGui::Button("[Clear All]")) {
        proc_mon.clear_targets();
        cpu_histories_.clear();
        ram_histories_mb_.clear();
    }

    // Active Process Chips
    const auto& targets = proc_mon.get_monitored_targets();
    if (targets.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "No processes selected. Select processes above to start profiling.");
        return;
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "TARGETS:");
    ImGui::SameLine();

    DWORD to_remove_pid = 0;
    DWORD to_primary_pid = 0;

    for (const auto& t : targets) {
        ImGui::PushID(static_cast<int>(t.pid));

        ImVec4 col(t.color.r, t.color.g, t.color.b, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.09f, 0.11f, 0.16f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.14f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, col);

        std::string badge = (t.is_primary ? "[PRIMARY] " : "[#] ") + wide_to_utf8(t.name) + " (" + std::to_string(t.pid) + ")";
        ImGui::PushFont(fonts.bold);
        if (ImGui::Button(badge.c_str())) {
            to_primary_pid = t.pid;
        }
        ImGui::PopFont();

        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 2);
        if (ImGui::SmallButton("X")) {
            to_remove_pid = t.pid;
        }
        ImGui::SameLine(0, 8.0f * s);

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
    const CpuMetrics& cpu,
    const FrameStats& frame_stats,
    const AppFonts& fonts
) {
    float s = fonts.dpi_scale;
    float avail_width = ImGui::GetContentRegionAvail().x;
    float card_width = (avail_width - 24.0f * s) / 4.0f;
    float card_height = 114.0f * s; // Guaranteed height to prevent any scrollbars

    const auto* primary = proc_mon.get_primary_target();
    double total_sec_cpu = proc_mon.get_total_secondary_cpu();
    size_t total_sec_ram = proc_mon.get_total_secondary_ram();

    // Style adjustments for compact, scrollbar-free GPU-Z ROG cards
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * s, 8.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * s, 3.0f * s));
    ImGuiWindowFlags card_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    // Card 1: Frame Rates & Latency
    ImGui::BeginChild("CardFPS", ImVec2(card_width, card_height), true, card_flags);
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "FRAME DELIVERY & LATENCY");
    
    ImGui::PushFont(fonts.large_stat);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.90f, 0.45f, 1.0f));
    ImGui::Text("%.1f", frame_stats.current_fps);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "FPS");

    // Live bottleneck indicator badge
    BottleneckInfo bInfo = detect_bottleneck(gpu, cpu, frame_stats.current_fps, proc_mon.get_system_ram_used(), proc_mon.get_system_ram_total());
    if (bInfo.type != BottleneckType::None) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.23f, 0.19f, 1.0f), "[%s]", bInfo.short_badge.c_str());
    }

    ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.95f, 1.0f), "1%% Low: %.1f FPS | FT: %.2f ms", frame_stats.one_percent_low, frame_stats.frametime_ms);
    if (etw_tracker.is_active()) {
        ImGui::TextColored(ImVec4(0.00f, 0.85f, 0.45f, 0.9f), "Engine: Kernel PresentMon (ETW)");
    } else {
        ImGui::TextDisabled("Engine: D3D11 SwapChain Present");
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Card 2: GPU Telemetry Card (Universal AMD / NVIDIA / Intel)
    ImGui::BeginChild("CardGPU", ImVec2(card_width, card_height), true, card_flags);
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "GPU ENGINE TELEMETRY");

    ImGui::PushFont(fonts.large_stat);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.88f, 0.95f, 1.0f));
    ImGui::Text("%d%%", gpu.core_usage_percent);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "LOAD");

    // VRAM calculation
    float vram_used_mb = static_cast<float>(gpu.vram_used_bytes) / (1024.0f * 1024.0f);
    float vram_total_mb = static_cast<float>(gpu.vram_total_bytes) / (1024.0f * 1024.0f);
    if (vram_total_mb > 0.0f) {
        if (vram_used_mb > 0.0f) {
            float vram_pct = (vram_used_mb / vram_total_mb) * 100.0f;
            ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.95f, 1.0f), "VRAM: %.0f / %.0f MB (%.0f%%)", vram_used_mb, vram_total_mb, vram_pct);
        } else {
            ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.95f, 1.0f), "VRAM: %.0f MB Dedicated", vram_total_mb);
        }
    } else {
        ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.95f, 1.0f), "VRAM: %.0f MB Used", vram_used_mb);
    }

    if (gpu.temperature_c > 0) {
        if (gpu.hotspot_temp_c > 0 && gpu.vram_temp_c > 0) {
            ImGui::TextColored(ImVec4(1.00f, 0.50f, 0.30f, 1.0f), "T: %dC | Hot: %dC | Mem: %dC", gpu.temperature_c, gpu.hotspot_temp_c, gpu.vram_temp_c);
        } else if (gpu.hotspot_temp_c > 0) {
            ImGui::TextColored(ImVec4(1.00f, 0.50f, 0.30f, 1.0f), "T: %dC | Hotspot: %dC", gpu.temperature_c, gpu.hotspot_temp_c);
        } else {
            ImGui::TextColored(ImVec4(1.00f, 0.50f, 0.30f, 1.0f), "Temp: %d C | Clocks: %d MHz", gpu.temperature_c, gpu.core_clock_mhz);
        }
    } else {
        std::string name_short = wide_to_utf8(gpu.adapter_name);
        if (name_short.length() > 22) name_short = name_short.substr(0, 20) + "..";
        ImGui::TextDisabled("%s", name_short.c_str());
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Card 3: CPU Hardware & Application
    ImGui::BeginChild("CardPrimary", ImVec2(card_width, card_height), true, card_flags);
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "CPU HARDWARE & PROCESS");
    ImGui::PushFont(fonts.large_stat);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.75f, 1.0f, 1.0f));
    ImGui::Text("%.0f%%", cpu.total_usage_percent);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "LOAD (C%d: %.0f%%)", cpu.max_core_index, cpu.max_core_usage);

    if (cpu.package_temp_c > 0) {
        if (cpu.package_power_w > 0) {
            ImGui::TextColored(ImVec4(1.00f, 0.65f, 0.20f, 1.0f), "Package: %d C | Power: %d W", cpu.package_temp_c, cpu.package_power_w);
        } else {
            ImGui::TextColored(ImVec4(1.00f, 0.65f, 0.20f, 1.0f), "Package: %d C | Cores: %u", cpu.package_temp_c, cpu.logical_core_count);
        }
    } else {
        ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.85f, 1.0f), "Package Temp: N/A | Cores: %u", cpu.logical_core_count);
    }

    if (primary && primary->is_alive) {
        float ram_mb = static_cast<float>(primary->private_working_set) / (1024.0f * 1024.0f);
        std::string proc_name = wide_to_utf8(primary->name);
        if (proc_name.length() > 14) proc_name = proc_name.substr(0, 12) + "..";
        ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s: %.1f%% (%d MB)", proc_name.c_str(), primary->cpu_percent, static_cast<int>(ram_mb));
    } else {
        std::string cpu_name = wide_to_utf8(cpu.cpu_model_name);
        if (cpu_name.length() > 22) cpu_name = cpu_name.substr(0, 20) + "..";
        ImGui::TextDisabled("%s", cpu_name.c_str());
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Card 4: Secondary Background Overhead Total
    ImGui::BeginChild("CardSecondary", ImVec2(card_width, card_height), true, card_flags);
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "BACKGROUND OVERHEAD TAX");
    
    ImGui::PushFont(fonts.large_stat);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.22f, 0.32f, 1.0f));
    ImGui::Text("+%.2f%%", total_sec_cpu);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "TOTAL TAX");

    float sec_ram_mb = static_cast<float>(total_sec_ram) / (1024.0f * 1024.0f);
    size_t sec_count = 0;
    for (const auto& t : proc_mon.get_monitored_targets()) {
        if (!t.is_primary) sec_count++;
    }
    ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.95f, 1.0f), "Background RAM: %.1f MB", sec_ram_mb);
    ImGui::TextColored(ImVec4(0.75f, 0.80f, 0.88f, 1.0f), "Monitored Secondary: %zu Apps", sec_count);
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
}

void DashboardView::render_telemetry_plots(const ProcessMonitor& proc_mon, const GpuMetrics& /*gpu*/, const AppFonts& /*fonts*/) {
    float avail_width = ImGui::GetContentRegionAvail().x;
    float plot_height = 240.0f;
    const auto& targets = proc_mon.get_monitored_targets();

    // 1. Frametime Plot
    if (ImPlot::BeginPlot("Frametime Latency Timeline (ms)", ImVec2(avail_width, plot_height))) {
        ImPlot::SetupAxes("Frames", "Time (ms)", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 33.3, ImPlotCond_Once);

        std::vector<float> ft_data;
        frametime_history_.get_linear(ft_data);

        if (!ft_data.empty()) {
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.00f, 0.88f, 0.95f, 1.0f));
            ImPlot::PlotLine("Frametime", ft_data.data(), static_cast<int>(ft_data.size()));
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
            ImPlot::PlotLine("GPU Core Load %", gpu_data.data(), static_cast<int>(gpu_data.size()));
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
                    ImPlot::PlotLine(label.c_str(), cpu_data.data(), static_cast<int>(cpu_data.size()));
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
                    ImPlot::PlotLine(label.c_str(), ram_data.data(), static_cast<int>(ram_data.size()));
                    ImPlot::PopStyleColor();
                }
            }
        }

        ImPlot::EndPlot();
    }
}

void DashboardView::render_thread_inspector(
    ThreadProfiler& thread_prof,
    const ProcessMonitor& proc_mon,
    const AppFonts& fonts
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

    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "SELECT TARGET PROCESS:");
    ImGui::SameLine();

    const auto* cur_target = proc_mon.get_target(selected_inspector_pid_);
    std::string cur_name = cur_target ? (wide_to_utf8(cur_target->name) + " (" + std::to_string(cur_target->pid) + ")") : "Select...";

    ImGui::SetNextItemWidth(300.0f * fonts.dpi_scale);
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

    if (ImGui::BeginTable("ThreadTable", 5, flags, ImVec2(0, 420.0f * fonts.dpi_scale))) {
        ImGui::TableSetupColumn("Thread ID (TID)", ImGuiTableColumnFlags_WidthFixed, 130.0f * fonts.dpi_scale);
        ImGui::TableSetupColumn("Relative Load %", ImGuiTableColumnFlags_WidthFixed, 140.0f * fonts.dpi_scale);
        ImGui::TableSetupColumn("Cycle Delta / Sample", ImGuiTableColumnFlags_WidthFixed, 180.0f * fonts.dpi_scale);
        ImGui::TableSetupColumn("Start Address", ImGuiTableColumnFlags_WidthFixed, 180.0f * fonts.dpi_scale);
        ImGui::TableSetupColumn("Module Origin", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        const auto& threads = thread_prof.get_sorted_threads();
        for (const auto& t : threads) {
            ImGui::TableNextRow();

            // TID
            ImGui::TableSetColumnIndex(0);
            ImGui::PushFont(fonts.mono);
            ImGui::Text("%u", t.tid);
            ImGui::PopFont();

            // Load %
            ImGui::TableSetColumnIndex(1);
            if (t.cpu_percent > 20.0) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%.1f%%", t.cpu_percent);
            } else {
                ImGui::Text("%.1f%%", t.cpu_percent);
            }

            // Cycle Delta
            ImGui::TableSetColumnIndex(2);
            ImGui::PushFont(fonts.mono);
            ImGui::Text("%llu", t.cycle_delta);
            ImGui::PopFont();

            // Start Address
            ImGui::TableSetColumnIndex(3);
            ImGui::PushFont(fonts.mono);
            ImGui::Text("0x%llX", static_cast<unsigned long long>(t.start_address));
            ImGui::PopFont();

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

double DashboardView::get_benchmark_elapsed_sec() const {
    if (!is_benchmarking_) return 0.0;
    return ImGui::GetTime() - benchmark_start_time_;
}

void DashboardView::start_benchmark() {
    is_benchmarking_ = true;
    benchmark_start_time_ = ImGui::GetTime();
    current_benchmark_frametimes_.clear();
    MessageBeep(MB_OK);
}

void DashboardView::stop_benchmark(const ProcessMonitor& proc_mon, const GpuMetrics& gpu, const FrameStats& frames) {
    if (!is_benchmarking_) return;
    is_benchmarking_ = false;

    BenchmarkRecord rec;
    const auto* primary = proc_mon.get_primary_target();
    rec.session_name = primary ? (L"Benchmark of " + primary->name) : L"Multi-Target Benchmark";
    rec.duration_seconds = ImGui::GetTime() - benchmark_start_time_;
    if (rec.duration_seconds < 0.1) rec.duration_seconds = 0.1;

    // Analyze captured benchmark frametimes
    if (!current_benchmark_frametimes_.empty()) {
        double sum = 0.0;
        for (float ft : current_benchmark_frametimes_) sum += ft;
        double avg_ft = sum / current_benchmark_frametimes_.size();
        rec.avg_frametime_ms = avg_ft;
        rec.avg_fps = (avg_ft > 0.0) ? (1000.0 / avg_ft) : 0.0;

        // Pacing Standard Deviation (jitter)
        double var_sum = 0.0;
        for (float ft : current_benchmark_frametimes_) {
            double diff = ft - avg_ft;
            var_sum += diff * diff;
        }
        rec.pacing_std_dev_ms = std::sqrt(var_sum / current_benchmark_frametimes_.size());

        // Sort for percentiles and min/max
        std::vector<float> sorted_ft = current_benchmark_frametimes_;
        std::sort(sorted_ft.begin(), sorted_ft.end());

        rec.max_fps = (sorted_ft.front() > 0.0f) ? (1000.0 / sorted_ft.front()) : 0.0;
        rec.min_fps = (sorted_ft.back() > 0.0f) ? (1000.0 / sorted_ft.back()) : 0.0;

        size_t idx_1pct = static_cast<size_t>(sorted_ft.size() * 0.99);
        if (idx_1pct < sorted_ft.size()) {
            rec.one_percent_low = 1000.0 / sorted_ft[idx_1pct];
        }
        size_t idx_01pct = static_cast<size_t>(sorted_ft.size() * 0.999);
        if (idx_01pct < sorted_ft.size()) {
            rec.zero_one_percent_low = 1000.0 / sorted_ft[idx_01pct];
        }
        size_t idx_001pct = static_cast<size_t>(sorted_ft.size() * 0.9999);
        if (idx_001pct < sorted_ft.size()) {
            rec.zero_zero_one_percent_low = 1000.0 / sorted_ft[idx_001pct];
        }

        // Stutters (frametime > 2.5 * avg or avg + 10ms)
        uint32_t stutters = 0;
        double stutter_thresh = (avg_ft * 2.5 > avg_ft + 10.0) ? (avg_ft * 2.5) : (avg_ft + 10.0);
        for (float ft : current_benchmark_frametimes_) {
            if (ft > stutter_thresh) stutters++;
        }
        rec.stutter_count = stutters;
        rec.stutter_percentage = (static_cast<double>(stutters) / current_benchmark_frametimes_.size()) * 100.0;
    } else {
        rec.avg_fps = frames.avg_fps > 0 ? frames.avg_fps : frames.current_fps;
        rec.min_fps = frames.min_fps;
        rec.max_fps = frames.max_fps;
        rec.one_percent_low = frames.one_percent_low;
        rec.zero_one_percent_low = frames.zero_one_percent_low;
        rec.zero_zero_one_percent_low = frames.zero_zero_one_percent_low;
        rec.avg_frametime_ms = frames.frametime_ms;
        rec.stutter_count = frames.stutter_count;
        rec.stutter_percentage = frames.stutter_percentage;
        rec.pacing_std_dev_ms = frames.pacing_std_dev_ms;
    }

    for (const auto& t : proc_mon.get_monitored_targets()) {
        BenchmarkProcessSummary sum;
        sum.pid = t.pid;
        sum.name = t.name;
        sum.avg_cpu_percent = t.cpu_percent;
        sum.avg_ram_mb = t.private_working_set / (1024 * 1024);
        rec.process_summaries.push_back(sum);
    }

    benchmark_history_.push_back(rec);

    // Auto-generate HTML report and CSV data log
    last_generated_report_path_ = ReportGenerator::generate_html_report(
        rec,
        current_benchmark_frametimes_,
        gpu
    );
    ReportGenerator::generate_csv_report(
        rec,
        current_benchmark_frametimes_,
        gpu
    );

    MessageBeep(MB_ICONASTERISK);
}

void DashboardView::toggle_benchmark(const ProcessMonitor& proc_mon, const GpuMetrics& gpu, const FrameStats& frames) {
    if (is_benchmarking_) {
        stop_benchmark(proc_mon, gpu, frames);
    } else {
        start_benchmark();
    }
}

void DashboardView::render_fan_control_tab(
    HardwareController& hw_ctrl,
    ConfigManager& config_mgr,
    const GpuMetrics& gpu,
    const AppFonts& fonts
) {
    float s = fonts.dpi_scale;
    auto& fan_cfg = config_mgr.get_fan();

    ImGui::PushFont(fonts.title);
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "GPU FAN CURVE & COOLER CONTROLLER");
    ImGui::PopFont();
    ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Intelligent low-noise acoustic curve with hardware temperature hysteresis and zero-RPM mode");
    ImGui::Spacing();

    // Hardware status bar
    ImGui::BeginChild("FanStatusCard", ImVec2(0, 75.0f * s), true);
    ImGui::Columns(4, "FanStatusCols", false);
    
    ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "GPU TEMP");
    ImGui::PushFont(fonts.bold);
    ImGui::TextColored(ImVec4(1.0f, 0.60f, 0.00f, 1.0f), "%d C", gpu.temperature_c);
    ImGui::PopFont();

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "CURRENT FAN SPEED");
    ImGui::PushFont(fonts.bold);
    int current_fan = hw_ctrl.get_current_fan_speed_pct();
    if (current_fan >= 0) {
        ImGui::TextColored(ImVec4(0.00f, 0.90f, 0.50f, 1.0f), "%d %%", current_fan);
    } else {
        ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.85f, 1.0f), "Auto (VBIOS)");
    }
    ImGui::PopFont();

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "DRIVER BACKEND");
    ImGui::PushFont(fonts.bold);
    if (hw_ctrl.is_nvidia()) {
        ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.20f, 1.0f), "NVIDIA NVAPI (Active)");
    } else if (hw_ctrl.is_amd()) {
        ImGui::TextColored(ImVec4(0.95f, 0.20f, 0.20f, 1.0f), "AMD ADL (Active)");
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "Generic / Standard");
    }
    ImGui::PopFont();

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "CONTROL MODE");
    ImGui::PushFont(fonts.bold);
    if (!fan_cfg.enabled || fan_cfg.mode == 0) {
        ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.85f, 1.0f), "Automatic (VBIOS)");
    } else if (fan_cfg.mode == 1) {
        ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.00f, 1.0f), "Fixed (%d%%)", fan_cfg.fixed_speed_pct);
    } else {
        ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "Custom Curve");
    }
    ImGui::PopFont();

    ImGui::Columns(1);
    ImGui::EndChild();

    ImGui::Spacing();

    // Mode Selection Radio Buttons
    ImGui::Checkbox("Enable Custom Fan Control", &fan_cfg.enabled);
    ImGui::SameLine(0, 20.0f * s);
    if (fan_cfg.enabled) {
        ImGui::RadioButton("Auto (VBIOS)", &fan_cfg.mode, 0);
        ImGui::SameLine(0, 15.0f * s);
        ImGui::RadioButton("Manual Fixed %", &fan_cfg.mode, 1);
        ImGui::SameLine(0, 15.0f * s);
        ImGui::RadioButton("Custom Fan Curve", &fan_cfg.mode, 2);
    }

    ImGui::Spacing();

    if (fan_cfg.enabled && fan_cfg.mode == 1) {
        ImGui::Text("Target Fan Speed: %d%%", fan_cfg.fixed_speed_pct);
        ImGui::SliderInt("##FixedFanSpeed", &fan_cfg.fixed_speed_pct, 0, 100, "%d %%");
        if (ImGui::Button("Apply Fixed Speed", ImVec2(180.0f * s, 32.0f * s))) {
            hw_ctrl.set_fan_speed(fan_cfg.fixed_speed_pct);
            config_mgr.save_to_file();
        }
    } else if (fan_cfg.enabled && fan_cfg.mode == 2) {
        ImGui::Checkbox("Zero RPM Mode (Stop fans below 45 C)", &fan_cfg.zero_rpm_enabled);
        ImGui::SameLine(0, 25.0f * s);
        ImGui::SetNextItemWidth(150.0f * s);
        ImGui::SliderInt("Hysteresis", &fan_cfg.hysteresis_c, 1, 8, "%d C");

        ImGui::Spacing();

        if (ImPlot::BeginPlot("##FanCurvePlot", ImVec2(-1, 260.0f * s), ImPlotFlags_NoTitle | ImPlotFlags_NoBoxSelect)) {
            ImPlot::SetupAxes("GPU Temperature (C)", "Fan Speed (%)", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
            ImPlot::SetupAxesLimits(20.0, 100.0, 0.0, 100.0, ImPlotCond_Always);

            std::vector<double> xs, ys;
            for (const auto& p : fan_cfg.points) {
                xs.push_back(static_cast<double>(p.temp_c));
                ys.push_back(static_cast<double>(p.fan_pct));
            }

            ImPlot::SetNextLineStyle(ImVec4(0.00f, 0.85f, 1.00f, 1.0f), 2.5f);
            ImPlot::PlotLine("Curve", xs.data(), ys.data(), static_cast<int>(xs.size()));

            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 6.0f, ImVec4(0.00f, 1.00f, 0.60f, 1.0f), 2.0f, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            ImPlot::PlotScatter("Points", xs.data(), ys.data(), static_cast<int>(xs.size()));

            if (gpu.temperature_c >= 20 && gpu.temperature_c <= 100) {
                double cur_t = static_cast<double>(gpu.temperature_c);
                double t_xs[2] = { cur_t, cur_t };
                double t_ys[2] = { 0.0, 100.0 };
                ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.40f, 0.10f, 0.85f), 1.5f);
                ImPlot::PlotLine("Live Temp", t_xs, t_ys, 2);
            }

            ImPlot::EndPlot();
        }

        ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "CURVE CONTROL NODES:");
        for (size_t i = 0; i < fan_cfg.points.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("Node %d:", static_cast<int>(i + 1));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f * s);
            ImGui::SliderInt("Temp C", &fan_cfg.points[i].temp_c, 20, 100, "%d C");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f * s);
            ImGui::SliderInt("Fan %", &fan_cfg.points[i].fan_pct, 0, 100, "%d %%");
            ImGui::PopID();
            if (i < fan_cfg.points.size() - 1) ImGui::SameLine(0, 15.0f * s);
        }

        ImGui::Spacing();
        if (ImGui::Button("Save & Apply Curve", ImVec2(160.0f * s, 32.0f * s))) {
            std::sort(fan_cfg.points.begin(), fan_cfg.points.end(), [](const FanPoint& a, const FanPoint& b) {
                return a.temp_c < b.temp_c;
            });
            hw_ctrl.update_fan_curve(fan_cfg, gpu.temperature_c);
            config_mgr.save_to_file();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset to Defaults", ImVec2(140.0f * s, 32.0f * s))) {
            fan_cfg.points = { { 30, 0 }, { 50, 40 }, { 65, 60 }, { 80, 80 }, { 90, 100 } };
            config_mgr.save_to_file();
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "Custom fan control is disabled. Video card BIOS / driver manages fan speeds automatically.");
        if (ImGui::Button("Enable Custom Fan Curve", ImVec2(200.0f * s, 32.0f * s))) {
            fan_cfg.enabled = true;
            fan_cfg.mode = 2;
            config_mgr.save_to_file();
        }
    }
}

void DashboardView::render_overclock_tab(
    HardwareController& hw_ctrl,
    ConfigManager& config_mgr,
    const GpuMetrics& gpu,
    const AppFonts& fonts
) {
    float s = fonts.dpi_scale;
    auto& oc_cfg = config_mgr.get_oc();

    ImGui::PushFont(fonts.title);
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "GPU TUNING & OVERCLOCK / UNDERVOLT");
    ImGui::PopFont();
    ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Hardware frequency offsets, safe vendor limits, and interactive Voltage-Frequency undervolting");
    ImGui::Spacing();

    // Vendor & Architecture Detection
    bool is_amd = hw_ctrl.is_amd() || gpu.is_amd || 
                  (gpu.adapter_name.find(L"Radeon") != std::wstring::npos) || 
                  (gpu.adapter_name.find(L"AMD") != std::wstring::npos);
    bool is_nvidia = hw_ctrl.is_nvidia() || gpu.is_nvidia || 
                     (gpu.adapter_name.find(L"NVIDIA") != std::wstring::npos) || 
                     (gpu.adapter_name.find(L"GeForce") != std::wstring::npos);

    // Live Hardware Telemetry Status Card
    ImGui::BeginChild("OcStatusCard", ImVec2(0, 75.0f * s), true);
    ImGui::Columns(4, "OcStatusCols", false);
    
    ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "GPU CORE CLOCK");
    ImGui::PushFont(fonts.bold);
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "%d MHz", gpu.core_clock_mhz);
    ImGui::PopFont();

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "MEMORY CLOCK");
    ImGui::PushFont(fonts.bold);
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.00f, 1.0f), "%d MHz", gpu.memory_clock_mhz);
    ImGui::PopFont();

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "POWER DRAW");
    ImGui::PushFont(fonts.bold);
    ImGui::TextColored(ImVec4(0.00f, 1.00f, 0.60f, 1.0f), "%d W", gpu.power_watts);
    ImGui::PopFont();

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "HOTSPOT TEMP");
    ImGui::PushFont(fonts.bold);
    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "%d C", gpu.hotspot_temp_c);
    ImGui::PopFont();

    ImGui::Columns(1);
    ImGui::EndChild();

    // Architecture badge and safety guard
    if (is_amd) {
        ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "[AMD RADEON RDNA ARCHITECTURE DETECTED]");
        ImGui::SameLine();
        ImGui::TextDisabled("- Safe Overdrive limits: VRAM [-500..+250 MHz], Undervolt [-150..+50 mV], Power [50..120%%]");
    } else if (is_nvidia) {
        ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.20f, 1.0f), "[NVIDIA GEFORCE ARCHITECTURE DETECTED]");
        ImGui::SameLine();
        ImGui::TextDisabled("- NVIDIA NVAPI limits: VRAM [-1000..+2000 MHz], Power [50..130%%]");
    } else {
        ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.85f, 1.0f), "[GENERIC GPU DETECTED]");
    }

    if (hw_ctrl.get_last_oc_status() != HardwareController::HardwareOcStatus::NotApplied) {
        ImVec4 col = (hw_ctrl.get_last_oc_status() == HardwareController::HardwareOcStatus::FullyApplied)
            ? ImVec4(0.0f, 0.9f, 0.5f, 1.0f)
            : ImVec4(1.0f, 0.75f, 0.2f, 1.0f);
        ImGui::TextColored(col, "Hardware Driver Status: %s", hw_ctrl.get_last_oc_message().c_str());
    }

    ImGui::Spacing();

    ImGui::Checkbox("Enable GPU Overclocking / Undervolting", &oc_cfg.enabled);
    ImGui::SameLine(0, 25.0f * s);
    ImGui::Checkbox("Apply Overclock at Startup", &oc_cfg.apply_at_startup);

    ImGui::Spacing();

    // Vendor-specific Safe Slider Limits (matching MSI Afterburner & hardware VBIOS)
    int min_core = is_amd ? (hw_ctrl.has_amd_od8() ? (hw_ctrl.get_amd_min_core_mhz() - hw_ctrl.get_amd_def_core_mhz()) : -300) : -500;
    int max_core = is_amd ? (hw_ctrl.has_amd_od8() ? (hw_ctrl.get_amd_max_core_mhz() - hw_ctrl.get_amd_def_core_mhz()) : 300) : 500;
    int min_mem = is_amd ? (hw_ctrl.has_amd_od8() ? (hw_ctrl.get_amd_min_mem_mhz() - hw_ctrl.get_amd_def_mem_mhz()) : -500) : -1000;
    int max_mem = is_amd ? (hw_ctrl.has_amd_od8() ? (hw_ctrl.get_amd_max_mem_mhz() - hw_ctrl.get_amd_def_mem_mhz()) : 250) : 2000;
    int min_power = 50;
    int max_power = is_amd ? (hw_ctrl.has_amd_od8() ? (100 + hw_ctrl.get_amd_max_power_pct()) : 120) : 130;
    int min_volt = is_amd ? (hw_ctrl.has_amd_od8() ? (hw_ctrl.get_amd_min_volt_mv() - hw_ctrl.get_amd_def_volt_mv()) : -150) : -100;
    int max_volt = is_amd ? (hw_ctrl.has_amd_od8() ? (hw_ctrl.get_amd_max_volt_mv() - hw_ctrl.get_amd_def_volt_mv()) : 50) : 100;

    // Sanitize any existing config values if they exceed safe vendor limits
    if (oc_cfg.memory_clock_offset_mhz > max_mem) oc_cfg.memory_clock_offset_mhz = max_mem;
    if (oc_cfg.memory_clock_offset_mhz < min_mem) oc_cfg.memory_clock_offset_mhz = min_mem;
    if (oc_cfg.core_clock_offset_mhz > max_core) oc_cfg.core_clock_offset_mhz = max_core;
    if (oc_cfg.core_clock_offset_mhz < min_core) oc_cfg.core_clock_offset_mhz = min_core;
    if (oc_cfg.power_limit_pct > max_power) oc_cfg.power_limit_pct = max_power;
    if (oc_cfg.voltage_offset_mv > max_volt) oc_cfg.voltage_offset_mv = max_volt;
    if (oc_cfg.voltage_offset_mv < min_volt) oc_cfg.voltage_offset_mv = min_volt;

    if (is_amd && hw_ctrl.has_amd_od8()) {
        ImGui::Text("Core Clock Offset (MHz): %+d MHz (Stock: %d MHz -> Target: %d MHz, Max VBIOS: %d MHz)",
                    oc_cfg.core_clock_offset_mhz, hw_ctrl.get_amd_def_core_mhz(),
                    std::clamp(hw_ctrl.get_amd_def_core_mhz() + oc_cfg.core_clock_offset_mhz, hw_ctrl.get_amd_min_core_mhz(), hw_ctrl.get_amd_max_core_mhz()),
                    hw_ctrl.get_amd_max_core_mhz());
    } else {
        ImGui::Text("Core Clock Offset (MHz): %+d MHz", oc_cfg.core_clock_offset_mhz);
    }
    ImGui::SliderInt("##CoreOffset", &oc_cfg.core_clock_offset_mhz, min_core, max_core, "%+d MHz");

    if (is_amd && hw_ctrl.has_amd_od8()) {
        ImGui::Text("Memory Clock Offset (MHz): %+d MHz (Stock: %d MHz -> Target: %d MHz, Max VBIOS: %d MHz)",
                    oc_cfg.memory_clock_offset_mhz, hw_ctrl.get_amd_def_mem_mhz(),
                    std::clamp(hw_ctrl.get_amd_def_mem_mhz() + oc_cfg.memory_clock_offset_mhz, hw_ctrl.get_amd_min_mem_mhz(), hw_ctrl.get_amd_max_mem_mhz()),
                    hw_ctrl.get_amd_max_mem_mhz());
    } else {
        ImGui::Text("Memory Clock Offset (MHz): %+d MHz %s", oc_cfg.memory_clock_offset_mhz, is_amd ? "(Safe GDDR6 Limit: +250 MHz)" : "");
    }
    ImGui::SliderInt("##MemOffset", &oc_cfg.memory_clock_offset_mhz, min_mem, max_mem, "%+d MHz");

    ImGui::Text("Power Limit (%%): %d%% (Range: %d%% - %d%%)", oc_cfg.power_limit_pct, min_power, max_power);
    ImGui::SliderInt("##PowerLimit", &oc_cfg.power_limit_pct, min_power, max_power, "%d %%");

    ImGui::Text("Temperature Limit (C): %d C", oc_cfg.temp_limit_c);
    ImGui::SliderInt("##TempLimit", &oc_cfg.temp_limit_c, 65, 90, "%d C");

    const char* volt_tag = (oc_cfg.voltage_offset_mv < 0) ? "[UNDERVOLT - Cooler & Quiet]" : (oc_cfg.voltage_offset_mv > 0 ? "[OVERVOLT]" : "[STOCK VBIOS]");
    if (is_amd && hw_ctrl.has_amd_od8()) {
        ImGui::Text("Core Voltage Offset / Undervolt (mV): %+d mV (Stock: %d mV -> Target: %d mV) %s",
                    oc_cfg.voltage_offset_mv, hw_ctrl.get_amd_def_volt_mv(),
                    std::clamp(hw_ctrl.get_amd_def_volt_mv() + oc_cfg.voltage_offset_mv, hw_ctrl.get_amd_min_volt_mv(), hw_ctrl.get_amd_max_volt_mv()),
                    volt_tag);
    } else {
        ImGui::Text("Core Voltage Offset / Undervolt (mV): %+d mV %s", oc_cfg.voltage_offset_mv, volt_tag);
    }
    ImGui::SliderInt("##VoltOffset", &oc_cfg.voltage_offset_mv, min_volt, max_volt, "%+d mV");

    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "VOLTAGE-FREQUENCY (V/F) CURVE EDITOR (Interactive Undervolt):");
    if (ImPlot::BeginPlot("##VfCurvePlot", ImVec2(-1, 200.0f * s), ImPlotFlags_NoTitle | ImPlotFlags_NoBoxSelect)) {
        if (is_amd && hw_ctrl.has_amd_od8()) {
            ImPlot::SetupAxes("Core Voltage (mV)", "Core Clock (MHz)", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
            ImPlot::SetupAxesLimits(650.0, 1100.0, 700.0, 1900.0, ImPlotCond_Always);

            double target_core = std::clamp(hw_ctrl.get_amd_def_core_mhz() + oc_cfg.core_clock_offset_mhz, hw_ctrl.get_amd_min_core_mhz(), hw_ctrl.get_amd_max_core_mhz());
            double target_volt = std::clamp(hw_ctrl.get_amd_def_volt_mv() + oc_cfg.voltage_offset_mv, hw_ctrl.get_amd_min_volt_mv(), hw_ctrl.get_amd_max_volt_mv());

            std::vector<double> v_xs = { 695.0, 776.0, target_volt };
            std::vector<double> v_ys = { 800.0, 1290.0, target_core };

            ImPlot::SetNextLineStyle(ImVec4(0.00f, 0.90f, 0.60f, 1.0f), 2.5f);
            ImPlot::PlotLine("RDNA V/F Curve", v_xs.data(), v_ys.data(), static_cast<int>(v_xs.size()));

            ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 6.0f, ImVec4(0.00f, 0.90f, 1.00f, 1.0f), 1.5f, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            ImPlot::PlotScatter("P-States (P1, P2, P3)", v_xs.data(), v_ys.data(), static_cast<int>(v_xs.size()));
        } else {
            ImPlot::SetupAxes("Core Voltage (mV)", "Frequency Offset (MHz)", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
            ImPlot::SetupAxesLimits(650.0, 1150.0, -150.0, 300.0, ImPlotCond_Always);

            std::vector<double> v_xs, v_ys;
            for (const auto& vf : oc_cfg.vf_curve_points) {
                v_xs.push_back(static_cast<double>(vf.voltage_mv + oc_cfg.voltage_offset_mv));
                v_ys.push_back(static_cast<double>(vf.freq_offset_mhz + oc_cfg.core_clock_offset_mhz));
            }

            ImPlot::SetNextLineStyle(ImVec4(0.00f, 0.90f, 0.60f, 1.0f), 2.5f);
            ImPlot::PlotLine("V/F Curve", v_xs.data(), v_ys.data(), static_cast<int>(v_xs.size()));

            ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 5.0f, ImVec4(0.00f, 0.90f, 1.00f, 1.0f), 1.5f, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            ImPlot::PlotScatter("Nodes", v_xs.data(), v_ys.data(), static_cast<int>(v_xs.size()));
        }

        ImPlot::EndPlot();
    }

    ImGui::Spacing();
    if (ImGui::Button("Apply Profile Settings", ImVec2(180.0f * s, 34.0f * s))) {
        hw_ctrl.apply_overclock(oc_cfg);
        config_mgr.save_to_file();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to Defaults", ImVec2(140.0f * s, 34.0f * s))) {
        oc_cfg.enabled = false;
        oc_cfg.core_clock_offset_mhz = 0;
        oc_cfg.memory_clock_offset_mhz = 0;
        oc_cfg.power_limit_pct = 100;
        oc_cfg.temp_limit_c = 83;
        oc_cfg.voltage_offset_mv = 0;
        hw_ctrl.reset_overclock();
        config_mgr.save_to_file();
    }
}

void DashboardView::render_benchmark_tab(
    const ProcessMonitor& proc_mon,
    const GpuMetrics& gpu,
    const FrameStats& frames,
    const AppFonts& fonts
) {
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "COMPARATIVE IMPACT BENCHMARK");
    ImGui::TextDisabled("Run controlled sampling sessions to compute the exact resource footprint across all monitored applications.");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(160.0f * fonts.dpi_scale);
    ImGui::SliderInt("Session Duration (sec)", &benchmark_duration_sec_, 10, 120);

    if (!is_benchmarking_) {
        ImGui::PushFont(fonts.bold);
        if (ImGui::Button("[START BENCHMARK SESSION (F10)]", ImVec2(280.0f * fonts.dpi_scale, 36.0f * fonts.dpi_scale))) {
            start_benchmark();
        }
        ImGui::PopFont();
    } else {
        double elapsed = get_benchmark_elapsed_sec();
        float progress = static_cast<float>(elapsed / benchmark_duration_sec_);
        if (progress > 1.0f) progress = 1.0f;

        ImGui::ProgressBar(progress, ImVec2(340.0f * fonts.dpi_scale, 32.0f * fonts.dpi_scale));
        ImGui::SameLine();
        if (ImGui::Button("Stop & Save")) {
            stop_benchmark(proc_mon, gpu, frames);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            is_benchmarking_ = false;
        }

        if (elapsed >= benchmark_duration_sec_) {
            stop_benchmark(proc_mon, gpu, frames);
        }
    }

    if (!last_generated_report_path_.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.65f, 0.35f, 0.9f));
        ImGui::PushFont(fonts.bold);
        if (ImGui::Button("[OPEN INTERACTIVE BROWSER REPORT & SHARE CARD]", ImVec2(440.0f * fonts.dpi_scale, 36.0f * fonts.dpi_scale))) {
            ReportGenerator::open_in_browser(last_generated_report_path_);
        }
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "Recorded Benchmark Runs:");

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

void DashboardView::render_settings_tab(ConfigManager& config_mgr, RTSSBridge& rtss_bridge, const AppFonts& fonts) {
    auto& hud = config_mgr.get_hud();
    auto& hotkeys = config_mgr.get_hotkeys();

    // 0. RTSS Standalone Service Control & Lifecycle
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "RIVATUNER STATISTICS SERVER (RTSS) ENGINE");
    if (rtss_bridge.is_connected()) {
        ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.45f, 1.0f), "[ONLINE] Connected to RivaTuner Statistics Server V2");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "[OFFLINE] RTSS is not running or memory mapping is inactive");
    }

    if (ImGui::Button("Start / Connect RTSS")) {
        RTSSBridge::launch_rtss();
        Sleep(600);
        rtss_bridge.connect();
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart RTSS Server")) {
        RTSSBridge::restart_rtss();
        Sleep(600);
        rtss_bridge.connect();
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.15f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.15f, 0.2f, 1.0f));
    if (ImGui::Button("Shutdown RTSS")) {
        rtss_bridge.clear_osd();
        rtss_bridge.disconnect();
        RTSSBridge::kill_rtss();
    }
    ImGui::PopStyleColor(2);

    ImGui::Checkbox("Close RTSS process automatically when AnyPerfomans exits", &hud.close_rtss_on_exit);
    ImGui::TextDisabled("Ensures no orphaned RTSS.exe background tasks remain when you close AnyPerfomans.");

    std::wstring rtss_path = RTSSBridge::get_rtss_executable_path();
    if (!rtss_path.empty()) {
        ImGui::TextDisabled("Detected RTSS Path: %s", wide_to_utf8(rtss_path).c_str());
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Notice: RTSS.exe was not detected in default directories.");
    }
    ImGui::TextDisabled("Note: MSI Afterburner is NOT required. AnyPerfomans feeds RTSS directly with zero overhead.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 1. Output Display Targets (Smart F11 Coordination)
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "TELEMETRY ENGINE OUTPUT TARGETS");
    ImGui::Checkbox("Output to RTSS On-Screen Display (RivaTuner Shared Memory V2)", &hud.output_to_rtss);
    ImGui::TextDisabled("Injects real-time hardware telemetry and hardware line graphs into RivaTuner OSD.");
    ImGui::Checkbox("DirectX 11 Standalone Transparent Overlay (Zero-Hook Desktop Fallback)", &hud.output_to_dx11_overlay);
    ImGui::TextDisabled("Hardware-accelerated layered window overlay running independently over games.");
    if (hud.output_to_rtss) {
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "* Smart F11: RTSS is active. Pressing F11 minimizes AnyPerfomans without popping up duplicate DX11 overlays.");
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 2. MSI Afterburner MAX: Line Graphs & Sensor Customization
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "RTSS HARDWARE LINE GRAPHS & LABELS (MSI AFTERBURNER MAX)");
    ImGui::TextDisabled("Configure line graph modes and custom sensor names for RTSS In-Game OSD.");
    ImGui::Spacing();

    // Graph style controls
    ImGui::Checkbox("Fill Graph Background (Unchecked = Clean Sharp Line Graph)", &hud.rtss_graph_filled);
    ImGui::SameLine(360.0f * fonts.dpi_scale);
    ImGui::SetNextItemWidth(180.0f * fonts.dpi_scale);
    ImGui::SliderFloat("Frametime Max (ms)", &hud.rtss_frametime_max_ms, 16.6f, 100.0f, "%.1f ms");

    int graph_h = hud.rtss_graph_height;
    const char* height_options[] = { "2 Lines (-2)", "3 Lines (-3)", "4 Lines (-4, Default)", "5 Lines (-5)" };
    int current_h_idx = (graph_h == -2) ? 0 : (graph_h == -3) ? 1 : (graph_h == -5) ? 3 : 2;
    ImGui::SetNextItemWidth(180.0f * fonts.dpi_scale);
    if (ImGui::Combo("Graph Height", &current_h_idx, height_options, IM_ARRAYSIZE(height_options))) {
        int vals[] = { -2, -3, -4, -5 };
        hud.rtss_graph_height = vals[current_h_idx];
    }
    ImGui::SameLine(360.0f * fonts.dpi_scale);
    const char* width_options[] = { "24 Chars (-24)", "32 Chars (-32, Default)", "40 Chars (-40)", "48 Chars (-48)" };
    int graph_w = hud.rtss_graph_width;
    int current_w_idx = (graph_w == -24) ? 0 : (graph_w == -40) ? 2 : (graph_w == -48) ? 3 : 1;
    ImGui::SetNextItemWidth(180.0f * fonts.dpi_scale);
    if (ImGui::Combo("Graph Width", &current_w_idx, width_options, IM_ARRAYSIZE(width_options))) {
        int vals[] = { -24, -32, -40, -48 };
        hud.rtss_graph_width = vals[current_w_idx];
    }

    ImGui::Spacing();

    // Global Color Palette
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "GLOBAL OSD PALETTE:");
    ColorPickerHex("##ValCol", hud.color_values);
    ImGui::SameLine();
    ImGui::Text("Telemetry Numbers / Values");

    ImGui::SameLine(320.0f * fonts.dpi_scale);
    ColorPickerHex("##StutCol", hud.color_stutter);
    ImGui::SameLine();
    ImGui::Text("Stutter Hitch Badge");

    ImGui::SameLine(600.0f * fonts.dpi_scale);
    ColorPickerHex("##BneckCol", hud.color_bottleneck);
    ImGui::SameLine();
    ImGui::Text("Bottleneck Limit Badge");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // RTSS Hardware Framerate Limiter
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "RTSS HARDWARE FRAMERATE LIMITER");
    ImGui::TextDisabled("Ultra-low latency hardware framerate limiter injected directly into the 3D rendering pipeline.");

    int current_fps_limit = config_mgr.get_framerate_limit();
    const struct { const char* label; int limit; } fps_presets[] = {
        { "UNLIMITED", 0 }, { "60 FPS", 60 }, { "120 FPS", 120 },
        { "144 FPS", 144 }, { "165 FPS", 165 }, { "240 FPS", 240 }
    };

    for (int p = 0; p < IM_ARRAYSIZE(fps_presets); ++p) {
        if (p > 0) ImGui::SameLine();
        bool is_active = (current_fps_limit == fps_presets[p].limit);
        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.7f, 0.8f, 0.9f));
        }
        if (ImGui::Button(fps_presets[p].label)) {
            current_fps_limit = fps_presets[p].limit;
            config_mgr.set_framerate_limit(current_fps_limit);
            RTSSBridge::set_framerate_limit(current_fps_limit);
        }
        if (is_active) {
            ImGui::PopStyleColor();
        }
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f * fonts.dpi_scale);
    if (ImGui::InputInt("Custom Cap##FpsCapInput", &current_fps_limit)) {
        if (current_fps_limit < 0) current_fps_limit = 0;
        config_mgr.set_framerate_limit(current_fps_limit);
        RTSSBridge::set_framerate_limit(current_fps_limit);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Reorderable Sensors Table
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "REORDERABLE SENSORS & OSD PIPELINE (MSI AFTERBURNER MAX)");
    ImGui::TextDisabled("Reorder telemetry rows with [^] [v], customize labels, pick colors and toggle hardware line graphs.");

    if (ImGui::Button("Reset Sensors to Default Layout")) {
        ConfigManager::init_default_sensors(hud.sensors);
    }

    ImGui::Spacing();

    if (hud.sensors.empty()) {
        ConfigManager::init_default_sensors(hud.sensors);
    }

    if (ImGui::BeginTable("SensorsReorderTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 65.0f * fonts.dpi_scale);
        ImGui::TableSetupColumn("OSD", ImGuiTableColumnFlags_WidthFixed, 50.0f * fonts.dpi_scale);
        ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthFixed, 55.0f * fonts.dpi_scale);
        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 60.0f * fonts.dpi_scale);
        ImGui::TableSetupColumn("Label / Custom Name", ImGuiTableColumnFlags_WidthFixed, 180.0f * fonts.dpi_scale);
        ImGui::TableSetupColumn("Hardware Sensor", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(hud.sensors.size()); ++i) {
            auto& s = hud.sensors[i];
            ImGui::TableNextRow();

            // Column 0: Move Up / Down
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i * 100 + 1);
            if (i > 0) {
                if (ImGui::SmallButton("^")) {
                    std::swap(hud.sensors[i], hud.sensors[i - 1]);
                }
            } else {
                ImGui::Dummy(ImVec2(16.0f * fonts.dpi_scale, 0));
            }
            ImGui::SameLine();
            if (i + 1 < static_cast<int>(hud.sensors.size())) {
                if (ImGui::SmallButton("v")) {
                    std::swap(hud.sensors[i], hud.sensors[i + 1]);
                }
            }
            ImGui::PopID();

            // Column 1: Enabled (OSD)
            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(i * 100 + 2);
            ImGui::Checkbox("##Show", &s.enabled);
            ImGui::PopID();

            // Column 2: Graph (Line graph)
            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(i * 100 + 3);
            ImGui::Checkbox("##Graph", &s.show_graph);
            ImGui::PopID();

            // Column 3: Color Picker
            ImGui::TableSetColumnIndex(3);
            ImGui::PushID(i * 100 + 4);
            ColorPickerHex("##Col", s.color);
            ImGui::PopID();

            // Column 4: Label InputText
            ImGui::TableSetColumnIndex(4);
            ImGui::PushID(i * 100 + 5);
            char lbl_buf[64];
            strcpy_s(lbl_buf, s.label.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##Lbl", lbl_buf, sizeof(lbl_buf))) {
                s.label = lbl_buf;
            }
            ImGui::PopID();

            // Column 5: Hardware Sensor identifier
            ImGui::TableSetColumnIndex(5);
            ImGui::TextDisabled("%s", s.id.c_str());
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Additional OSD Metric Options
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "ADDITIONAL OSD METRIC OPTIONS");
    ImGui::Columns(2, "SubMetricColumns", false);
    ImGui::Checkbox("Show 1% Low FPS", &hud.show_1pct_low);
    ImGui::Checkbox("Show 0.1% Low FPS", &hud.show_01pct_low);
    ImGui::Checkbox("Show 0.01% Low FPS", &hud.show_001pct_low);
    ImGui::Checkbox("Show Min / Max FPS", &hud.show_min_max);
    ImGui::Checkbox("Show Stutters / Jitter Hitches", &hud.show_stutters);
    ImGui::Checkbox("Show Frametime (ms) Value", &hud.show_frametime_text);
    ImGui::NextColumn();
    ImGui::Checkbox("Show Bottleneck / Saturation Limit Badge", &hud.show_bottleneck_indicator);
    ImGui::Checkbox("Show Monitored Secondary Apps Tax", &hud.show_secondary_apps);
    ImGui::Columns(1);

    ImGui::Separator();
    ImGui::Spacing();

    // 4. System Integration & Background System Tray
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "SYSTEM INTEGRATION & TRAY (MSI AFTERBURNER MAX)");
    ImGui::Checkbox("Minimize to System Tray on close [X button]", &hud.minimize_to_tray_on_close);
    ImGui::TextDisabled("When enabled, clicking the close button minimizes AnyPerfomans silently to the system tray.");

    ImGui::Checkbox("Display live hardware temperature directly on Tray Icon", &hud.show_tray_temperature);
    ImGui::TextDisabled("Renders sharp real-time temperature digits (e.g. 62) directly on the taskbar tray icon.");

    if (hud.show_tray_temperature) {
        ImGui::Indent(20.0f);
        if (ImGui::RadioButton("Display GPU Core Temperature", hud.tray_temp_is_gpu)) {
            hud.tray_temp_is_gpu = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Display CPU Package Temperature", !hud.tray_temp_is_gpu)) {
            hud.tray_temp_is_gpu = false;
        }
        ImGui::Unindent(20.0f);
    }

    if (ImGui::Checkbox("Start with Windows (Task Scheduler with Highest Admin Privileges, No UAC)", &hud.autostart_with_windows)) {
        TrayManager::set_autostart_enabled(hud.autostart_with_windows, hud.start_minimized_to_tray);
    }
    ImGui::TextDisabled("Uses Windows Task Scheduler to run with elevated privileges at logon without annoying UAC popups.");

    if (hud.autostart_with_windows) {
        ImGui::Indent(20.0f);
        if (ImGui::Checkbox("Start Minimized directly to System Tray", &hud.start_minimized_to_tray)) {
            TrayManager::set_autostart_enabled(hud.autostart_with_windows, hud.start_minimized_to_tray);
        }
        ImGui::Unindent(20.0f);
    }

    ImGui::Separator();
    ImGui::Spacing();

    // 4. Standalone DX11 Overlay Appearance
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "STANDALONE DX11 OVERLAY SETTINGS");
    ImGui::Text("Overlay Screen Position:");
    const char* pos_names[] = { "Top-Left", "Top-Right", "Bottom-Left", "Bottom-Right", "Custom (Free Coordinates)" };
    int current_pos = static_cast<int>(hud.position);
    ImGui::SetNextItemWidth(280.0f * fonts.dpi_scale);
    if (ImGui::Combo("##HudPosCombo", &current_pos, pos_names, IM_ARRAYSIZE(pos_names))) {
        hud.position = static_cast<HudPosition>(current_pos);
    }

    if (hud.position == HudPosition::Custom) {
        ImGui::Indent(20.0f);
        ImGui::SliderInt("Custom X Coordinate", &hud.custom_x, 0, 3840);
        ImGui::SliderInt("Custom Y Coordinate", &hud.custom_y, 0, 2160);
        ImGui::Unindent(20.0f);
    }

    ImGui::SliderFloat("HUD Glass Background Opacity", &hud.bg_opacity, 0.1f, 1.0f, "%.2f");
    ImGui::SliderFloat("HUD Font Scale", &hud.font_scale, 0.75f, 1.50f, "%.2fx");
    ImGui::Checkbox("Show Frametime Graph in DX11 HUD", &hud.show_frametime_graph);

    ImGui::Separator();
    ImGui::Spacing();

    // 5. Hotkeys Configuration
    ImGui::TextColored(ImVec4(0.00f, 0.88f, 0.95f, 1.0f), "HOTKEYS & SHORTCUTS");
    ImGui::Text("Toggle AnyPerfomans Dashboard: %s", ConfigManager::key_to_string(hotkeys.toggle_overlay_key).c_str());
    ImGui::Text("Toggle Benchmark Run: %s", ConfigManager::key_to_string(hotkeys.toggle_benchmark_key).c_str());

    ImGui::Spacing();
    ImGui::PushFont(fonts.bold);
    if (ImGui::Button("[SAVE CONFIGURATION]", ImVec2(320.0f * fonts.dpi_scale, 36.0f * fonts.dpi_scale))) {
        config_mgr.save_to_file();
    }
    ImGui::PopFont();
}

void DashboardView::render_rtss_mini_hud(
    const ProcessMonitor& proc_mon,
    const EtwTracker& etw_tracker,
    const GpuMetrics& gpu,
    const CpuMetrics& cpu,
    const ConfigManager& config_mgr,
    Dx11Backend& backend,
    const FrameStats& frames,
    OverlayMode& current_mode
) {
    const auto& hud = config_mgr.get_hud();
    const auto& targets = proc_mon.get_monitored_targets();
    const auto& fonts = backend.get_fonts();

    // Compute dynamic window dimensions
    float hud_width = 360.0f * hud.font_scale * fonts.dpi_scale;
    float hud_height = 80.0f * hud.font_scale * fonts.dpi_scale;

    if (is_benchmarking_) hud_height += 24.0f * hud.font_scale * fonts.dpi_scale;
    if (hud.show_gpu_usage || hud.show_gpu_temp || hud.show_gpu_vram || hud.show_gpu_clock) hud_height += 24.0f * hud.font_scale * fonts.dpi_scale;
    if (hud.show_cpu_usage) hud_height += 24.0f * hud.font_scale * fonts.dpi_scale;
    if (hud.show_cpu_per_core) hud_height += 22.0f * hud.font_scale * fonts.dpi_scale;
    if (hud.show_system_ram) hud_height += 24.0f * hud.font_scale * fonts.dpi_scale;
    if (hud.show_fps) hud_height += 24.0f * hud.font_scale * fonts.dpi_scale;
    if (hud.show_stutters) hud_height += 22.0f * hud.font_scale * fonts.dpi_scale;
    if (hud.show_bottleneck_indicator) hud_height += 22.0f * hud.font_scale * fonts.dpi_scale;
    if (hud.show_frametime_graph) hud_height += (hud.frametime_graph_height + 14.0f) * hud.font_scale * fonts.dpi_scale;
    if (hud.show_secondary_apps && !targets.empty()) {
        hud_height += (static_cast<float>(targets.size()) * 22.0f + 16.0f) * hud.font_scale * fonts.dpi_scale;
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

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.07f, hud.bg_opacity));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.18f, 0.25f, 0.6f));

    ImGui::Begin("AnyPerfRTSSHud", nullptr, flags);

    // Title / Switch button
    ImGui::PushFont(fonts.bold);
    ImGui::TextColored(ImVec4(0.85f, 0.15f, 0.22f, 1.0f), "ANYPERFOMANS");
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetWindowWidth() - 75.0f * hud.font_scale * fonts.dpi_scale);
    if (ImGui::SmallButton("Full [F11]")) {
        current_mode = OverlayMode::Dashboard;
    }
    ImGui::Separator();

    // Switch to Monospace Font for Razor-Sharp RTSS HUD Numbers
    ImGui::PushFont(fonts.mono);

    // 0. Live Benchmark Recording Badge
    if (is_benchmarking_) {
        double elapsed = get_benchmark_elapsed_sec();
        int sec = static_cast<int>(elapsed);
        int mins = sec / 60;
        int rem_sec = sec % 60;
        char rec_str[64];
        sprintf_s(rec_str, "[REC %02d:%02d]", mins, rem_sec);
        float pulse = 0.6f + 0.4f * std::sin(static_cast<float>(elapsed * 5.0));
        ImGui::TextColored(ImVec4(1.0f, 0.1f * pulse, 0.15f * pulse, 1.0f), "%s", rec_str);
        ImGui::SameLine();
        ImGui::TextColored(hex_to_imvec4(hud.color_values), "Avg: %.0f | 1%%: %.0f", frames.avg_fps, frames.one_percent_low);
    }

    // 1. GPU Row
    if (hud.show_gpu_usage || hud.show_gpu_temp || hud.show_gpu_vram || hud.show_gpu_clock) {
        std::string label = hud.label_gpu.empty() ? "GPU" : hud.label_gpu;
        ImGui::TextColored(hex_to_imvec4(hud.color_gpu), "%s : ", label.c_str());
        ImGui::SameLine();
        std::stringstream gpu_ss;
        if (hud.show_gpu_usage) gpu_ss << gpu.core_usage_percent << "% ";
        if (hud.show_gpu_temp && gpu.temperature_c > 0) gpu_ss << "| " << gpu.temperature_c << "C ";
        if (hud.show_gpu_hotspot && gpu.hotspot_temp_c > 0) gpu_ss << "| Hot: " << gpu.hotspot_temp_c << "C ";
        if (hud.show_gpu_vram_temp && gpu.vram_temp_c > 0) gpu_ss << "| Mem: " << gpu.vram_temp_c << "C ";
        if (hud.show_gpu_clock && gpu.core_clock_mhz > 0) gpu_ss << "| " << gpu.core_clock_mhz << " MHz ";
        if (hud.show_gpu_power && gpu.power_watts > 0) gpu_ss << "| " << gpu.power_watts << "W ";
        if (hud.show_gpu_vram) {
            float vram_used_mb = static_cast<float>(gpu.vram_used_bytes) / (1024.0f * 1024.0f);
            float vram_tot_mb = static_cast<float>(gpu.vram_total_bytes) / (1024.0f * 1024.0f);
            if (vram_tot_mb > 0) {
                gpu_ss << "| " << static_cast<int>(vram_used_mb) << "/" << static_cast<int>(vram_tot_mb) << " MB";
            } else {
                gpu_ss << "| " << static_cast<int>(vram_used_mb) << " MB";
            }
        }
        ImGui::TextColored(hex_to_imvec4(hud.color_values), "%s", gpu_ss.str().c_str());
    }

    // 2. CPU Row
    if (hud.show_cpu_usage) {
        const auto* primary = proc_mon.get_primary_target();
        std::string label = hud.label_cpu.empty() ? "CPU" : hud.label_cpu;
        ImGui::TextColored(hex_to_imvec4(hud.color_cpu), "%s : ", label.c_str());
        ImGui::SameLine();
        std::stringstream cpu_ss;
        if (primary && primary->is_alive) {
            cpu_ss << std::fixed << std::setprecision(1) << primary->cpu_percent << "% ";
            if (hud.show_cpu_temp && cpu.package_temp_c > 0) cpu_ss << "| " << cpu.package_temp_c << "C ";
            if (hud.show_cpu_power && cpu.package_power_w > 0) cpu_ss << "| " << cpu.package_power_w << "W ";
            cpu_ss << "(" << wide_to_utf8(primary->name) << ")";
        } else {
            cpu_ss << std::fixed << std::setprecision(1) << cpu.total_usage_percent << "% ";
            if (hud.show_cpu_temp && cpu.package_temp_c > 0) cpu_ss << "| " << cpu.package_temp_c << "C ";
            if (hud.show_cpu_power && cpu.package_power_w > 0) cpu_ss << "| " << cpu.package_power_w << "W ";
            cpu_ss << "[Total]";
        }
        ImGui::TextColored(hex_to_imvec4(hud.color_values), "%s", cpu_ss.str().c_str());

        // Per-Core Load Breakdown
        if (hud.show_cpu_per_core && !cpu.per_core_usage.empty()) {
            ImGui::TextColored(hex_to_imvec4(hud.color_cpu), "CORES: ");
            ImGui::SameLine();
            std::stringstream core_ss;
            for (size_t c = 0; c < cpu.per_core_usage.size() && c < 6; ++c) {
                core_ss << "C" << c << ":" << static_cast<int>(cpu.per_core_usage[c]) << "% ";
            }
            if (cpu.per_core_usage.size() > 6) core_ss << "...";
            ImGui::TextColored(hex_to_imvec4(hud.color_values), "%s", core_ss.str().c_str());
        }
    }

    // 3. System RAM & Pagefile Row
    if (hud.show_system_ram) {
        float ram_used_gb = static_cast<float>(proc_mon.get_system_ram_used()) / (1024.0f * 1024.0f * 1024.0f);
        float ram_tot_gb = static_cast<float>(proc_mon.get_system_ram_total()) / (1024.0f * 1024.0f * 1024.0f);
        std::string label = hud.label_ram.empty() ? "RAM" : hud.label_ram;
        ImGui::TextColored(hex_to_imvec4(hud.color_ram), "%s : ", label.c_str());
        ImGui::SameLine();
        if (ram_tot_gb > 0.0f) {
            ImGui::TextColored(hex_to_imvec4(hud.color_values), "%.1f / %.1f GB", ram_used_gb, ram_tot_gb);
        } else {
            ImGui::TextColored(hex_to_imvec4(hud.color_values), "%.1f GB", ram_used_gb);
        }
        if (hud.show_pagefile) {
            float page_used_gb = static_cast<float>(proc_mon.get_system_commit_used()) / (1024.0f * 1024.0f * 1024.0f);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "| Page: %.1f GB", page_used_gb);
        }
    }

    // 4. FPS & Percentiles Row
    if (hud.show_fps) {
        std::string label = hud.label_fps.empty() ? "FPS" : hud.label_fps;
        ImGui::TextColored(hex_to_imvec4(hud.color_fps), "%s : ", label.c_str());
        ImGui::SameLine();
        std::stringstream fps_ss;
        fps_ss << std::fixed << std::setprecision(0) << frames.current_fps;
        if (hud.show_1pct_low && frames.one_percent_low > 0.0) {
            fps_ss << "  (1%: " << std::fixed << std::setprecision(0) << frames.one_percent_low;
            if (hud.show_01pct_low && frames.zero_one_percent_low > 0.0) {
                fps_ss << ", 0.1%: " << std::fixed << std::setprecision(0) << frames.zero_one_percent_low;
            }
            if (hud.show_001pct_low && frames.zero_zero_one_percent_low > 0.0) {
                fps_ss << ", 0.01%: " << std::fixed << std::setprecision(0) << frames.zero_zero_one_percent_low;
            }
            fps_ss << ")";
        }
        if (hud.show_frametime_text) {
            fps_ss << "  FT: " << std::fixed << std::setprecision(1) << frames.frametime_ms << "ms";
        }
        if (etw_tracker.is_active()) {
            fps_ss << " [ETW]";
        }
        ImGui::TextColored(hex_to_imvec4(hud.color_values), "%s", fps_ss.str().c_str());
    }

    // 5. Optional Stutters & Pacing Jitter Row
    if (hud.show_stutters) {
        ImGui::TextColored(hex_to_imvec4(hud.color_stutter), "PACE: ");
        ImGui::SameLine();
        ImGui::TextColored(hex_to_imvec4(hud.color_values), "Jitter: %.2fms | Hitches: %u (%.1f%%)",
            frames.pacing_std_dev_ms,
            frames.stutter_count,
            frames.stutter_percentage
        );
    }

    // 6. Live Bottleneck Saturation Indicator
    if (hud.show_bottleneck_indicator) {
        BottleneckInfo bInfo = detect_bottleneck(
            gpu,
            cpu,
            frames.current_fps,
            proc_mon.get_system_ram_used(),
            proc_mon.get_system_ram_total()
        );
        if (bInfo.type != BottleneckType::None) {
            std::string b_col = hud.color_bottleneck.empty() ? "FF3B30" : hud.color_bottleneck;
            ImGui::TextColored(hex_to_imvec4(b_col), "[LIMIT: %s]", bInfo.description.c_str());
        }
    }

    // 7. Configurable Frametime Graph Sparkline
    if (hud.show_frametime_graph) {
        std::vector<float> ft_data;
        frametime_history_.get_linear(ft_data);
        if (!ft_data.empty()) {
            ImGui::PushStyleColor(ImGuiCol_PlotLines, hex_to_imvec4(hud.color_frametime));
            float graph_width = ImGui::GetContentRegionAvail().x;
            ImGui::PlotLines("##HudFrametimeGraph",
                ft_data.data(),
                static_cast<int>(ft_data.size()),
                0,
                nullptr,
                0.0f,
                hud.frametime_graph_max_ms,
                ImVec2(graph_width, hud.frametime_graph_height * hud.font_scale * fonts.dpi_scale)
            );
            ImGui::PopStyleColor();
        }
    }

    // 8. Monitored Secondary Apps Breakdown
    if (hud.show_secondary_apps && !targets.empty()) {
        ImGui::Separator();
        for (const auto& t : targets) {
            if (t.is_primary) continue;
            float ram_mb = static_cast<float>(t.private_working_set) / (1024.0f * 1024.0f);
            ImVec4 col(t.color.r, t.color.g, t.color.b, 1.0f);

            ImGui::TextColored(col, "%-12s: %4.1f%% | %3.0f MB",
                wide_to_utf8(t.name).substr(0, 12).c_str(),
                t.cpu_percent,
                ram_mb
            );
        }
    }

    ImGui::PopFont(); // Pop fonts.mono

    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

} // namespace anyperf
