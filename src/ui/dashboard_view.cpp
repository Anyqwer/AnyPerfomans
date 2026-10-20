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

    // 5. Active Tab View (3 Minimalist Core Modules)
    switch (active_tab_index_) {
        case 0:
            render_tuning_tab(hw_ctrl, config_mgr, proc_mon, gpu_mon.get_metrics(), frame_stats, fonts);
            break;
        case 1:
            render_telemetry_tab(thread_prof, proc_mon, gpu_mon.get_metrics(), fonts);
            break;
        case 2:
            render_osd_sensors_tab(config_mgr, rtss_bridge, fonts);
            break;
        default:
            active_tab_index_ = 0;
            break;
    }

    // Ctrl + F Hotkey to toggle V/F Curve Editor (MSI Afterburner style)
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        show_curve_editor_modal_ = !show_curve_editor_modal_;
    }

    // Render Afterburner-style V/F Curve Editor Modal if active
    if (show_curve_editor_modal_) {
        render_vf_curve_editor_modal(hw_ctrl, config_mgr, gpu_mon.get_metrics(), fonts);
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

    // Clean Minimalist Title Branding
    ImGui::PushFont(fonts.title);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.98f, 1.00f, 1.0f));
    ImGui::Text("ANYPERFOMANS");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::SameLine(0, 10.0f * s);
    ImGui::TextColored(ImVec4(0.45f, 0.52f, 0.62f, 0.9f), "| Hardware & Telemetry Suite");

    // Right-aligned status badges
    float badges_width = 460.0f * s;
    float start_x = ImGui::GetWindowWidth() - badges_width;
    if (start_x < 380.0f * s) start_x = 380.0f * s;
    ImGui::SameLine(start_x);

    // ETW Engine Status Badge
    if (etw_tracker.is_active()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.04f, 0.18f, 0.12f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.06f, 0.24f, 0.16f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.92f, 0.48f, 1.0f));
        ImGui::Button("[ ETW KERNEL PRO ]");
        ImGui::PopStyleColor(3);
    } else if (etw_tracker.get_state() == EtwState::AccessDenied) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.16f, 0.04f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.25f, 0.06f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.70f, 0.15f, 1.0f));
        if (ImGui::Button("[ ! ELEVATE ADMIN ]")) {
            EtwTracker::relaunch_as_admin();
        }
        ImGui::PopStyleColor(3);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.09f, 0.12f, 0.17f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.16f, 0.22f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.56f, 0.65f, 1.0f));
        ImGui::Button("[ ETW STANDBY ]");
        ImGui::PopStyleColor(3);
    }

    ImGui::SameLine(0, 6.0f * s);

    // GPU Hardware Architecture Badge
    const auto& gpu_metrics = gpu_mon.get_metrics();
    std::string gpu_short_name = wide_to_utf8(gpu_metrics.adapter_name);
    if (gpu_short_name.length() > 18) {
        gpu_short_name = gpu_short_name.substr(0, 16) + "..";
    }
    std::string gpu_badge = "[ " + gpu_short_name + " ]";
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.12f, 0.18f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.08f, 0.18f, 0.26f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.88f, 1.00f, 1.0f));
    ImGui::Button(gpu_badge.c_str());
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0, 6.0f * s);

    // Mini HUD Button (HUD Glass Pill)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.42f, 0.58f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.65f, 0.85f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.00f, 0.85f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
    if (ImGui::Button("[ MINI HUD F11 ]", ImVec2(125.0f * s, 26.0f * s))) {
        current_mode = OverlayMode::MiniHud;
    }
    ImGui::PopStyleColor(4);

    ImGui::Spacing();
}

void DashboardView::render_navigation_bar(ConfigManager& config_mgr, const AppFonts& fonts) {
    const char* tabs[] = {
        "TUNING & OC",
        "TELEMETRY",
        "OSD & SENSORS"
    };

    float s = fonts.dpi_scale;
    float tab_width = 160.0f * s;
    float tab_height = 32.0f * s;

    ImGui::PushFont(fonts.bold);

    for (int i = 0; i < 3; ++i) {
        bool is_active = (active_tab_index_ == i);

        if (is_active) {
            // High-Tech Active Pill: Vibrant Cyber Cyan Glass with Bold White
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.48f, 0.65f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.62f, 0.82f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
        } else {
            // Inactive Pill: Translucent Dark Slate
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.11f, 0.16f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.18f, 0.26f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.68f, 0.80f, 0.90f));
        }

        if (ImGui::Button(tabs[i], ImVec2(tab_width, tab_height))) {
            active_tab_index_ = i;
        }

        ImGui::PopStyleColor(3);
        ImGui::SameLine(0, 6.0f * s);
    }

    // 5 Profile Slots Quick Selector on the Right Side
    float avail_w = ImGui::GetContentRegionAvail().x;
    float profile_btn_w = 34.0f * s;
    float total_profile_w = (profile_btn_w + 4.0f * s) * 5 + 85.0f * s;
    if (avail_w > total_profile_w) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - total_profile_w));
    }

    ImGui::TextColored(ImVec4(0.50f, 0.60f, 0.72f, 1.0f), "PROFILES:");
    ImGui::SameLine(0, 6.0f * s);

    int cur_profile = config_mgr.get_active_profile_index();
    for (int p = 0; p < ConfigManager::NUM_PROFILES; ++p) {
        bool is_p_active = (cur_profile == p);
        if (is_p_active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.85f, 0.45f, 0.95f)); // Neon Emerald
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.04f, 0.10f, 0.06f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.14f, 0.20f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.75f, 0.85f, 1.0f));
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

    // Filter & Add Search Bar (Cyber Cyan Label)
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "PROCESS FILTER:");
    ImGui::SameLine(0, 8.0f * s);
    ImGui::SetNextItemWidth(180.0f * s);
    ImGui::InputTextWithHint("##FilterQuery", "Filter processes...", process_search_query_, sizeof(process_search_query_));

    ImGui::SameLine(0, 8.0f * s);
    ImGui::SetNextItemWidth(260.0f * s);
    if (ImGui::BeginCombo("##ProcessSelectCombo", "Select running target...")) {
        std::string query = process_search_query_;
        for (const auto& p : cached_proc_list_) {
            std::string name_utf8 = wide_to_utf8(p.name);
            if (!query.empty() && !stristr_simple(name_utf8, query)) {
                continue;
            }

            std::string label = name_utf8 + " [" + std::to_string(p.pid) + "]";
            bool already_monitored = proc_mon.is_monitored(p.pid);

            if (already_monitored) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.40f, 0.48f, 1.0f));
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

    ImGui::SameLine(0, 8.0f * s);
    if (ImGui::Button("[ Refresh List ]")) {
        cached_proc_list_ = ProcessMonitor::enumerate_processes();
    }

    ImGui::SameLine(0, 6.0f * s);
    if (ImGui::Button("[ Clear Targets ]")) {
        proc_mon.clear_targets();
        cpu_histories_.clear();
        ram_histories_mb_.clear();
    }

    // Active Process Chips
    const auto& targets = proc_mon.get_monitored_targets();
    if (targets.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.00f, 0.70f, 0.15f, 1.0f), "No targets hooked. Select game or application above to enable hardware attribution profiling.");
        return;
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "TARGETS:");
    ImGui::SameLine(0, 8.0f * s);

    DWORD to_remove_pid = 0;
    DWORD to_primary_pid = 0;

    for (const auto& t : targets) {
        ImGui::PushID(static_cast<int>(t.pid));

        ImVec4 col(t.color.r, t.color.g, t.color.b, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.09f, 0.13f, 0.19f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.20f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, col);

        std::string badge = (t.is_primary ? "[PRIMARY] " : "[#] ") + wide_to_utf8(t.name) + " (" + std::to_string(t.pid) + ")";
        ImGui::PushFont(fonts.bold);
        if (ImGui::Button(badge.c_str())) {
            to_primary_pid = t.pid;
        }
        ImGui::PopFont();

        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 2);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.08f, 0.10f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.12f, 0.18f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.40f, 0.40f, 1.0f));
        if (ImGui::SmallButton("X")) {
            to_remove_pid = t.pid;
        }
        ImGui::PopStyleColor(3);
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
    float card_height = 120.0f * s; // Guaranteed height to prevent any scrollbars

    const auto* primary = proc_mon.get_primary_target();
    double total_sec_cpu = proc_mon.get_total_secondary_cpu();
    size_t total_sec_ram = proc_mon.get_total_secondary_ram();

    // Style adjustments for compact, scrollbar-free Bento cards (ui-ux-pro-max-skill)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * s, 10.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * s, 3.0f * s));
    ImGuiWindowFlags card_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    // Card 1: Frame Rates & Latency (Neon Emerald KPI)
    ImGui::BeginChild("CardFPS", ImVec2(card_width, card_height), true, card_flags);
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "FRAME DELIVERY & PACING");
    
    ImGui::PushFont(fonts.large_stat);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.90f, 0.46f, 1.0f)); // Neon Emerald
    ImGui::Text("%.1f", frame_stats.current_fps);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine(0, 6.0f * s);
    ImGui::PushFont(fonts.bold);
    ImGui::TextColored(ImVec4(0.85f, 0.90f, 0.95f, 1.0f), "FPS");
    ImGui::PopFont();

    // Live bottleneck indicator badge
    BottleneckInfo bInfo = detect_bottleneck(gpu, cpu, frame_stats.current_fps, proc_mon.get_system_ram_used(), proc_mon.get_system_ram_total());
    if (bInfo.type != BottleneckType::None) {
        ImGui::SameLine(0, 8.0f * s);
        ImGui::TextColored(ImVec4(1.00f, 0.35f, 0.25f, 1.0f), "[%s]", bInfo.short_badge.c_str());
    }

    ImGui::TextColored(ImVec4(0.88f, 0.92f, 0.96f, 1.0f), "1%% Low: %.1f FPS  |  FT: %.2f ms", frame_stats.one_percent_low, frame_stats.frametime_ms);
    if (etw_tracker.is_active()) {
        ImGui::TextColored(ImVec4(0.00f, 0.90f, 0.46f, 0.95f), "Engine: Kernel PresentMon (ETW)");
    } else {
        ImGui::TextDisabled("Engine: D3D11 SwapChain Present");
    }
    ImGui::EndChild();

    ImGui::SameLine(0, 8.0f * s);

    // Card 2: GPU Telemetry Card (Cyber Cyan KPI)
    ImGui::BeginChild("CardGPU", ImVec2(card_width, card_height), true, card_flags);
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "GPU ACCELERATOR & VRAM");

    ImGui::PushFont(fonts.large_stat);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.90f, 1.00f, 1.0f)); // Cyber Cyan
    ImGui::Text("%d%%", gpu.core_usage_percent);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine(0, 6.0f * s);
    ImGui::PushFont(fonts.bold);
    ImGui::TextColored(ImVec4(0.85f, 0.90f, 0.95f, 1.0f), "LOAD");
    ImGui::PopFont();

    // VRAM calculation
    float vram_used_mb = static_cast<float>(gpu.vram_used_bytes) / (1024.0f * 1024.0f);
    float vram_total_mb = static_cast<float>(gpu.vram_total_bytes) / (1024.0f * 1024.0f);
    if (vram_total_mb > 0.0f) {
        if (vram_used_mb > 0.0f) {
            float vram_pct = (vram_used_mb / vram_total_mb) * 100.0f;
            ImGui::TextColored(ImVec4(0.88f, 0.92f, 0.96f, 1.0f), "VRAM: %.0f / %.0f MB (%.0f%%)", vram_used_mb, vram_total_mb, vram_pct);
        } else {
            ImGui::TextColored(ImVec4(0.88f, 0.92f, 0.96f, 1.0f), "VRAM: %.0f MB Dedicated", vram_total_mb);
        }
    } else {
        ImGui::TextColored(ImVec4(0.88f, 0.92f, 0.96f, 1.0f), "VRAM: %.0f MB Used", vram_used_mb);
    }

    if (gpu.temperature_c > 0) {
        if (gpu.hotspot_temp_c > 0 && gpu.vram_temp_c > 0) {
            ImGui::TextColored(ImVec4(1.00f, 0.65f, 0.25f, 1.0f), "T: %d C | Hot: %d C | Mem: %d C", gpu.temperature_c, gpu.hotspot_temp_c, gpu.vram_temp_c);
        } else if (gpu.hotspot_temp_c > 0) {
            ImGui::TextColored(ImVec4(1.00f, 0.65f, 0.25f, 1.0f), "T: %d C | Hotspot: %d C", gpu.temperature_c, gpu.hotspot_temp_c);
        } else {
            ImGui::TextColored(ImVec4(1.00f, 0.65f, 0.25f, 1.0f), "Temp: %d C | Clocks: %d MHz", gpu.temperature_c, gpu.core_clock_mhz);
        }
    } else {
        std::string name_short = wide_to_utf8(gpu.adapter_name);
        if (name_short.length() > 22) name_short = name_short.substr(0, 20) + "..";
        ImGui::TextDisabled("%s", name_short.c_str());
    }
    ImGui::EndChild();

    ImGui::SameLine(0, 8.0f * s);

    // Card 3: CPU Hardware & Application (Electric Sky Blue KPI)
    ImGui::BeginChild("CardPrimary", ImVec2(card_width, card_height), true, card_flags);
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "CPU PROCESSOR & APP");
    
    ImGui::PushFont(fonts.large_stat);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.22f, 0.74f, 0.97f, 1.0f)); // Electric Sky Blue
    ImGui::Text("%.0f%%", cpu.total_usage_percent);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine(0, 6.0f * s);
    ImGui::PushFont(fonts.bold);
    ImGui::TextColored(ImVec4(0.85f, 0.90f, 0.95f, 1.0f), "LOAD (C%d: %.0f%%)", cpu.max_core_index, cpu.max_core_usage);
    ImGui::PopFont();

    if (cpu.package_temp_c > 0) {
        if (cpu.package_power_w > 0) {
            ImGui::TextColored(ImVec4(1.00f, 0.70f, 0.20f, 1.0f), "Package: %d C | Power: %d W", cpu.package_temp_c, cpu.package_power_w);
        } else {
            ImGui::TextColored(ImVec4(1.00f, 0.70f, 0.20f, 1.0f), "Package: %d C | Cores: %u", cpu.package_temp_c, cpu.logical_core_count);
        }
    } else {
        ImGui::TextColored(ImVec4(0.60f, 0.68f, 0.78f, 1.0f), "Package Temp: N/A | Cores: %u", cpu.logical_core_count);
    }

    if (primary && primary->is_alive) {
        float ram_mb = static_cast<float>(primary->private_working_set) / (1024.0f * 1024.0f);
        std::string proc_name = wide_to_utf8(primary->name);
        if (proc_name.length() > 14) proc_name = proc_name.substr(0, 12) + "..";
        ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.00f, 1.0f), "%s: %.1f%% (%d MB)", proc_name.c_str(), primary->cpu_percent, static_cast<int>(ram_mb));
    } else {
        std::string cpu_name = wide_to_utf8(cpu.cpu_model_name);
        if (cpu_name.length() > 22) cpu_name = cpu_name.substr(0, 20) + "..";
        ImGui::TextDisabled("%s", cpu_name.c_str());
    }
    ImGui::EndChild();

    ImGui::SameLine(0, 8.0f * s);

    // Card 4: Secondary Background Overhead Total (Crimson Alert KPI)
    ImGui::BeginChild("CardSecondary", ImVec2(card_width, card_height), true, card_flags);
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "BACKGROUND OVERHEAD TAX");
    
    ImGui::PushFont(fonts.large_stat);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.32f, 0.45f, 1.0f)); // Crimson Alert
    ImGui::Text("+%.2f%%", total_sec_cpu);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine(0, 6.0f * s);
    ImGui::PushFont(fonts.bold);
    ImGui::TextColored(ImVec4(0.85f, 0.90f, 0.95f, 1.0f), "TOTAL TAX");
    ImGui::PopFont();

    float sec_ram_mb = static_cast<float>(total_sec_ram) / (1024.0f * 1024.0f);
    size_t sec_count = 0;
    for (const auto& t : proc_mon.get_monitored_targets()) {
        if (!t.is_primary) sec_count++;
    }
    ImGui::TextColored(ImVec4(0.88f, 0.92f, 0.96f, 1.0f), "Background RAM: %.1f MB", sec_ram_mb);
    ImGui::TextColored(ImVec4(0.65f, 0.72f, 0.82f, 1.0f), "Monitored Background: %zu Apps", sec_count);
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
}

void DashboardView::render_telemetry_tab(
    ThreadProfiler& thread_prof,
    const ProcessMonitor& proc_mon,
    const GpuMetrics& /*gpu*/,
    const AppFonts& fonts
) {
    float s = fonts.dpi_scale;
    float avail_width = ImGui::GetContentRegionAvail().x;
    float plot_height = 220.0f * s;
    const auto& targets = proc_mon.get_monitored_targets();

    // 1. Frametime Plot
    if (ImPlot::BeginPlot("Frametime Latency Timeline (ms)", ImVec2(avail_width, plot_height))) {
        ImPlot::SetupAxes("Frames", "Time (ms)", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 33.3, ImPlotCond_Once);

        std::vector<float> ft_data;
        frametime_history_.get_linear(ft_data);

        if (!ft_data.empty()) {
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.00f, 0.90f, 1.00f, 1.0f));
            ImPlot::PlotLine("Frametime", ft_data.data(), static_cast<int>(ft_data.size()));
            ImPlot::PopStyleColor();
        }

        ImPlot::TagY(16.666, ImVec4(0.95f, 0.35f, 0.35f, 0.85f), "60 FPS (16.6ms)");
        ImPlot::TagY(6.944, ImVec4(0.00f, 0.90f, 0.46f, 0.85f), "144 FPS (6.9ms)");

        ImPlot::EndPlot();
    }

    ImGui::Spacing();

    // 2. Multi-Series CPU & GPU Utilization & RAM
    float half_width = (avail_width - 12.0f * s) / 2.0f;

    if (ImPlot::BeginPlot("Hardware & Process Load (%)", ImVec2(half_width, plot_height))) {
        ImPlot::SetupAxes("Timeline", "Load %", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 100.0, ImPlotCond_Always);

        // GPU Line
        std::vector<float> gpu_data;
        gpu_usage_history_.get_linear(gpu_data);
        if (!gpu_data.empty()) {
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1.00f, 0.45f, 0.15f, 1.0f));
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

    ImGui::SameLine(0, 12.0f * s);

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

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 4. Thread & Core Attribution Inspector (embedded in Telemetry as requested)
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "THREAD & CORE CPU CYCLE ATTRIBUTION:");
    ImGui::SameLine(0, 10.0f * s);

    if (targets.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Hook at least one target process above to inspect threads.");
        return;
    }

    if (selected_inspector_pid_ == 0 || !proc_mon.is_monitored(selected_inspector_pid_)) {
        const auto* primary = proc_mon.get_primary_target();
        if (primary) selected_inspector_pid_ = primary->pid;
    }

    const auto* cur_target = proc_mon.get_target(selected_inspector_pid_);
    std::string cur_name = cur_target ? (wide_to_utf8(cur_target->name) + " [" + std::to_string(cur_target->pid) + "]") : "Select target...";

    ImGui::SetNextItemWidth(260.0f * s);
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

    ImGui::SameLine(0, 15.0f * s);
    ImGui::TextDisabled("Active Threads: %zu | Microsecond CPU Cycle Attribution", thread_prof.get_thread_count());

    static ImGuiTableFlags flags =
        ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("ThreadTable", 5, flags, ImVec2(0, 320.0f * s))) {
        ImGui::TableSetupColumn("Thread ID (TID)", ImGuiTableColumnFlags_WidthFixed, 130.0f * s);
        ImGui::TableSetupColumn("Relative Load %", ImGuiTableColumnFlags_WidthFixed, 140.0f * s);
        ImGui::TableSetupColumn("Cycle Delta / Sample", ImGuiTableColumnFlags_WidthFixed, 180.0f * s);
        ImGui::TableSetupColumn("Start Address", ImGuiTableColumnFlags_WidthFixed, 180.0f * s);
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

void DashboardView::render_tuning_tab(
    HardwareController& hw_ctrl,
    ConfigManager& config_mgr,
    const ProcessMonitor& proc_mon,
    const GpuMetrics& gpu,
    const FrameStats& frame_stats,
    const AppFonts& fonts
) {
    float s = fonts.dpi_scale;
    auto& oc_cfg = config_mgr.get_oc();
    auto& fan_cfg = config_mgr.get_fan();

    bool is_amd = hw_ctrl.is_amd() || gpu.is_amd || 
                  (gpu.adapter_name.find(L"Radeon") != std::wstring::npos) || 
                  (gpu.adapter_name.find(L"AMD") != std::wstring::npos);
    bool is_nvidia = hw_ctrl.is_nvidia() || gpu.is_nvidia || 
                     (gpu.adapter_name.find(L"NVIDIA") != std::wstring::npos) || 
                     (gpu.adapter_name.find(L"GeForce") != std::wstring::npos);
    (void)is_nvidia;

    // Vendor safe slider limits (matching MSI Afterburner & hardware VBIOS)
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

    // 2-Column Responsive Layout (MSI Afterburner Center Stack + Side Cards)
    float avail_w = ImGui::GetContentRegionAvail().x;
    float col_left_w = (avail_w > 900.0f * s) ? ((avail_w - 16.0f * s) * 0.52f) : avail_w;
    float col_right_w = (avail_w > 900.0f * s) ? (avail_w - col_left_w - 16.0f * s) : avail_w;

    // ==========================================
    // LEFT COLUMN: HARDWARE CONTROLS (Afterburner Stack)
    // ==========================================
    ImGui::BeginChild("LeftTuningStack", ImVec2(col_left_w, 0), false);

    // Live Hardware Telemetry Banner (Like Afterburner's dials)
    ImGui::BeginChild("LiveHwCard", ImVec2(0, 68.0f * s), true);
    ImGui::Columns(5, "HwBannerCols", false);

    ImGui::TextColored(ImVec4(0.50f, 0.58f, 0.68f, 1.0f), "GPU CORE");
    ImGui::PushFont(fonts.bold);
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "%d MHz", gpu.core_clock_mhz);
    ImGui::PopFont();

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.50f, 0.58f, 0.68f, 1.0f), "VOLTAGE");
    ImGui::PushFont(fonts.bold);
    if (gpu.voltage_mv > 0) {
        ImGui::TextColored(ImVec4(0.00f, 0.90f, 0.46f, 1.0f), "%d mV", gpu.voltage_mv);
    } else {
        ImGui::TextColored(ImVec4(0.50f, 0.60f, 0.72f, 1.0f), "-- mV");
    }
    ImGui::PopFont();

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.50f, 0.58f, 0.68f, 1.0f), "VRAM CLOCK");
    ImGui::PushFont(fonts.bold);
    ImGui::TextColored(ImVec4(1.00f, 0.75f, 0.15f, 1.0f), "%d MHz", gpu.memory_clock_mhz);
    ImGui::PopFont();

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.50f, 0.58f, 0.68f, 1.0f), "POWER DRAW");
    ImGui::PushFont(fonts.bold);
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 0.46f, 1.0f), "%d W", gpu.power_watts);
    ImGui::PopFont();

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.50f, 0.58f, 0.68f, 1.0f), "HOTSPOT / TEMP");
    ImGui::PushFont(fonts.bold);
    if (gpu.hotspot_temp_c > 0) {
        ImGui::TextColored(ImVec4(1.00f, 0.35f, 0.25f, 1.0f), "%dC / %dC", gpu.temperature_c, gpu.hotspot_temp_c);
    } else {
        ImGui::TextColored(ImVec4(1.00f, 0.55f, 0.25f, 1.0f), "%d C", gpu.temperature_c);
    }
    ImGui::PopFont();

    ImGui::Columns(1);
    ImGui::EndChild();

    if (hw_ctrl.is_safe_mode_engaged()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.35f, 0.08f, 0.08f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.00f, 0.25f, 0.25f, 1.0f));
        ImGui::BeginChild("SafeModeBanner", ImVec2(0, 48.0f * s), true);
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "[!] SAFE MODE ACTIVE: Overclocking was reset to prevent OS crash.");
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
    }

    if (hw_ctrl.get_last_oc_status() != HardwareController::HardwareOcStatus::NotApplied) {
        ImVec4 col = (hw_ctrl.get_last_oc_status() == HardwareController::HardwareOcStatus::FullyApplied)
            ? ImVec4(0.0f, 0.9f, 0.5f, 1.0f)
            : ImVec4(1.0f, 0.75f, 0.2f, 1.0f);
        ImGui::TextColored(col, "Driver Status: %s", hw_ctrl.get_last_oc_message().c_str());
    }

    ImGui::Spacing();

    // 1. CORE VOLTAGE (mV)
    const char* volt_tag = (oc_cfg.voltage_offset_mv < 0) ? "[UNDERVOLT]" : (oc_cfg.voltage_offset_mv > 0 ? "[OVERVOLT]" : "[STOCK]");
    if (is_amd && hw_ctrl.has_amd_od8()) {
        int target_volt = std::clamp(hw_ctrl.get_amd_def_volt_mv() + oc_cfg.voltage_offset_mv, hw_ctrl.get_amd_min_volt_mv(), hw_ctrl.get_amd_max_volt_mv());
        if (gpu.voltage_mv > 0) {
            ImGui::Text("CORE VOLTAGE: %+d mV (Live: %d mV | Stock: %d mV -> Target: %d mV) %s", oc_cfg.voltage_offset_mv, gpu.voltage_mv, hw_ctrl.get_amd_def_volt_mv(), target_volt, volt_tag);
        } else {
            ImGui::Text("CORE VOLTAGE: %+d mV (Stock: %d mV -> Target: %d mV) %s", oc_cfg.voltage_offset_mv, hw_ctrl.get_amd_def_volt_mv(), target_volt, volt_tag);
        }
    } else {
        if (gpu.voltage_mv > 0) {
            ImGui::Text("CORE VOLTAGE: %+d mV (Live: %d mV) %s", oc_cfg.voltage_offset_mv, gpu.voltage_mv, volt_tag);
        } else {
            ImGui::Text("CORE VOLTAGE: %+d mV %s", oc_cfg.voltage_offset_mv, volt_tag);
        }
    }
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 175.0f * s);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.45f, 0.65f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.65f, 0.85f, 1.0f));
    if (ImGui::SmallButton("[ CURVE EDITOR (Ctrl+F) ]")) {
        show_curve_editor_modal_ = true;
    }
    ImGui::PopStyleColor(2);
    ImGui::SliderInt("##CoreVoltage", &oc_cfg.voltage_offset_mv, min_volt, max_volt, "%+d mV");

    ImGui::Spacing();

    // 2. GPU CORE CLOCK (MHz) with Curve toggle button
    if (is_amd && hw_ctrl.has_amd_od8()) {
        int target_core = std::clamp(hw_ctrl.get_amd_def_core_mhz() + oc_cfg.core_clock_offset_mhz, hw_ctrl.get_amd_min_core_mhz(), hw_ctrl.get_amd_max_core_mhz());
        ImGui::Text("GPU CORE CLOCK: %+d MHz (Stock: %d -> %d MHz)", oc_cfg.core_clock_offset_mhz, hw_ctrl.get_amd_def_core_mhz(), target_core);
    } else {
        ImGui::Text("GPU CORE CLOCK: %+d MHz", oc_cfg.core_clock_offset_mhz);
    }
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 110.0f * s);
    if (show_vf_curve_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.50f, 0.68f, 0.95f));
    }
    if (ImGui::SmallButton("[ INLINE CURVE ]")) {
        show_vf_curve_ = !show_vf_curve_;
    }
    if (show_vf_curve_) {
        ImGui::PopStyleColor();
    }
    ImGui::SliderInt("##CoreClock", &oc_cfg.core_clock_offset_mhz, min_core, max_core, "%+d MHz");

    ImGui::Spacing();

    // 3. MEMORY CLOCK (MHz)
    if (is_amd && hw_ctrl.has_amd_od8()) {
        int target_mem = std::clamp(hw_ctrl.get_amd_def_mem_mhz() + oc_cfg.memory_clock_offset_mhz, hw_ctrl.get_amd_min_mem_mhz(), hw_ctrl.get_amd_max_mem_mhz());
        ImGui::Text("MEMORY CLOCK: %+d MHz (Stock: %d -> %d MHz)", oc_cfg.memory_clock_offset_mhz, hw_ctrl.get_amd_def_mem_mhz(), target_mem);
    } else {
        ImGui::Text("MEMORY CLOCK: %+d MHz", oc_cfg.memory_clock_offset_mhz);
    }
    ImGui::SliderInt("##MemClock", &oc_cfg.memory_clock_offset_mhz, min_mem, max_mem, "%+d MHz");

    ImGui::Spacing();

    // 4. POWER LIMIT (%)
    ImGui::Text("POWER LIMIT: %d%% (Range: %d%% - %d%%)", oc_cfg.power_limit_pct, min_power, max_power);
    ImGui::SliderInt("##PowerLimit", &oc_cfg.power_limit_pct, min_power, max_power, "%d %%");

    ImGui::Spacing();

    // 5. TEMPERATURE LIMIT (C)
    ImGui::Text("TEMPERATURE LIMIT: %d C", oc_cfg.temp_limit_c);
    ImGui::SliderInt("##TempLimit", &oc_cfg.temp_limit_c, 65, 90, "%d C");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 6. FAN SPEED (%) & COOLER CONTROLLER (MSI Afterburner Style)
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "FAN SPEED CONTROL (AFTERBURNER COMPACT):");
    ImGui::SameLine(0, 10.0f * s);

    // Auto / Manual / User Curve Buttons
    bool is_auto = (!fan_cfg.enabled || fan_cfg.mode == 0);
    bool is_manual = (fan_cfg.enabled && fan_cfg.mode == 1);
    bool is_curve = (fan_cfg.enabled && fan_cfg.mode == 2);

    if (is_auto) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.85f, 0.45f, 0.95f));
    if (ImGui::Button("[ AUTO ]")) {
        fan_cfg.enabled = true;
        fan_cfg.mode = 0;
        hw_ctrl.restore_auto_fans();
        config_mgr.save_to_file();
    }
    if (is_auto) ImGui::PopStyleColor();

    ImGui::SameLine(0, 5.0f * s);
    if (is_manual) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.00f, 0.65f, 0.15f, 0.95f));
    if (ImGui::Button("[ MANUAL ]")) {
        fan_cfg.enabled = true;
        fan_cfg.mode = 1;
        hw_ctrl.set_fan_speed(fan_cfg.fixed_speed_pct);
        config_mgr.save_to_file();
    }
    if (is_manual) ImGui::PopStyleColor();

    ImGui::SameLine(0, 5.0f * s);
    if (is_curve) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.50f, 0.68f, 0.95f));
    if (ImGui::Button("[ USER CURVE ]")) {
        fan_cfg.enabled = true;
        fan_cfg.mode = 2;
        hw_ctrl.apply_hardware_fan_curve(fan_cfg);
        hw_ctrl.update_fan_curve(fan_cfg, gpu.temperature_c, gpu.hotspot_temp_c, gpu.vram_temp_c);
        config_mgr.save_to_file();
        show_fan_curve_ = true;
    }
    if (is_curve) ImGui::PopStyleColor();

    ImGui::SameLine(0, 8.0f * s);
    if (show_fan_curve_) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.50f, 0.68f, 0.95f));
    if (ImGui::SmallButton(show_fan_curve_ ? "Hide Curve" : "Edit Curve...")) {
        show_fan_curve_ = !show_fan_curve_;
    }
    if (show_fan_curve_) ImGui::PopStyleColor();

    // Fan Speed Slider
    int display_fan = is_auto ? (hw_ctrl.get_current_fan_speed_pct() >= 0 ? hw_ctrl.get_current_fan_speed_pct() : 0) : fan_cfg.fixed_speed_pct;
    if (is_auto) {
        char auto_str[64];
        snprintf(auto_str, sizeof(auto_str), "Auto (%d %%)", display_fan);
        int temp_fan = display_fan;
        if (ImGui::SliderInt("##FanSlider", &temp_fan, 0, 100, auto_str)) {
            fan_cfg.enabled = true;
            fan_cfg.mode = 1;
            fan_cfg.fixed_speed_pct = temp_fan;
            hw_ctrl.set_fan_speed(fan_cfg.fixed_speed_pct);
            config_mgr.save_to_file();
        }
    } else {
        if (ImGui::SliderInt("##FanSlider", &fan_cfg.fixed_speed_pct, 0, 100, "%d %%")) {
            fan_cfg.enabled = true;
            fan_cfg.mode = 1;
            hw_ctrl.set_fan_speed(fan_cfg.fixed_speed_pct);
            config_mgr.save_to_file();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 7. BOTTOM ACTION BUTTONS (MSI Afterburner Checkmark & Reset)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.48f, 0.65f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.65f, 0.85f, 1.0f));
    ImGui::PushFont(fonts.bold);
    if (ImGui::Button("[ APPLY PROFILE ]", ImVec2(160.0f * s, 34.0f * s))) {
        oc_cfg.enabled = true;
        hw_ctrl.apply_overclock(oc_cfg);
        if (fan_cfg.enabled) {
            if (fan_cfg.mode == 0) hw_ctrl.restore_auto_fans();
            else if (fan_cfg.mode == 1) hw_ctrl.set_fan_speed(fan_cfg.fixed_speed_pct);
            else if (fan_cfg.mode == 2) hw_ctrl.apply_hardware_fan_curve(fan_cfg);
        }
        config_mgr.save_to_file();
    }
    ImGui::PopFont();
    ImGui::PopStyleColor(2);

    ImGui::SameLine(0, 10.0f * s);
    if (ImGui::Button("Reset Defaults", ImVec2(120.0f * s, 34.0f * s))) {
        oc_cfg.enabled = false;
        oc_cfg.core_clock_offset_mhz = 0;
        oc_cfg.memory_clock_offset_mhz = 0;
        oc_cfg.power_limit_pct = 100;
        oc_cfg.temp_limit_c = 83;
        oc_cfg.voltage_offset_mv = 0;
        hw_ctrl.reset_overclock();
        fan_cfg.mode = 0;
        hw_ctrl.restore_auto_fans();
        config_mgr.save_to_file();
    }

    ImGui::SameLine(0, 15.0f * s);
    ImGui::Checkbox("Apply at Startup", &oc_cfg.apply_at_startup);

    ImGui::EndChild();

    // ==========================================
    // RIGHT COLUMN: BENCHMARK & INTERACTIVE CURVES
    // ==========================================
    if (avail_w > 900.0f * s) {
        ImGui::SameLine(0, 16.0f * s);
    }

    ImGui::BeginChild("RightTuningCurves", ImVec2(col_right_w, 0), false);

    // Quick Benchmark Card (Integrated into Tuning & OC as requested)
    ImGui::BeginChild("QuickBenchCard", ImVec2(0, is_benchmarking_ ? 130.0f * s : 105.0f * s), true);
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "QUICK IMPACT BENCHMARK (F10)");

    if (!is_benchmarking_) {
        ImGui::SetNextItemWidth(140.0f * s);
        ImGui::SliderInt("Sec", &benchmark_duration_sec_, 10, 120, "%d s");
        ImGui::SameLine(0, 12.0f * s);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.48f, 0.65f, 0.90f));
        ImGui::PushFont(fonts.bold);
        if (ImGui::Button("[ START BENCHMARK ]", ImVec2(170.0f * s, 28.0f * s))) {
            start_benchmark();
        }
        ImGui::PopFont();
        ImGui::PopStyleColor();

        if (!last_generated_report_path_.empty()) {
            ImGui::SameLine(0, 10.0f * s);
            if (ImGui::SmallButton("Open Report")) {
                ReportGenerator::open_in_browser(last_generated_report_path_);
            }
        }
    } else {
        double elapsed = get_benchmark_elapsed_sec();
        float progress = static_cast<float>(elapsed / benchmark_duration_sec_);
        if (progress > 1.0f) progress = 1.0f;
        ImGui::ProgressBar(progress, ImVec2(240.0f * s, 26.0f * s));
        ImGui::SameLine(0, 8.0f * s);
        if (ImGui::Button("Stop & Save")) {
            stop_benchmark(proc_mon, gpu, frame_stats);
        }
        ImGui::SameLine(0, 5.0f * s);
        if (ImGui::Button("Cancel")) {
            is_benchmarking_ = false;
        }
        if (elapsed >= benchmark_duration_sec_) {
            stop_benchmark(proc_mon, gpu, frame_stats);
        }
    }

    if (!benchmark_history_.empty()) {
        const auto& last_b = benchmark_history_.back();
        ImGui::TextDisabled("Last Session: Avg %.1f FPS | 1%% Low: %.1f FPS | FT: %.2f ms (Jitter: %.2f ms)",
                            last_b.avg_fps, last_b.one_percent_low, last_b.avg_frametime_ms, last_b.pacing_std_dev_ms);
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // V/F Curve Interactive Editor
    if (show_vf_curve_) {
        ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "VOLTAGE-FREQUENCY (V/F) CURVE:");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 140.0f * s);
        if (ImGui::SmallButton("[ EXPAND (Ctrl+F) ]")) {
            show_curve_editor_modal_ = true;
        }

        if (ImPlot::BeginPlot("##VfCurvePlot", ImVec2(-1, 180.0f * s), ImPlotFlags_NoTitle | ImPlotFlags_NoBoxSelect)) {
            if (is_amd && hw_ctrl.has_amd_od8()) {
                ImPlot::SetupAxes("Voltage (mV)", "Core Clock (MHz)", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
                ImPlot::SetupAxesLimits(650.0, 1100.0, 700.0, 1900.0, ImPlotCond_Always);

                double p1_f = oc_cfg.custom_vf_enabled ? oc_cfg.p1_freq_mhz : hw_ctrl.get_amd_p1_freq_mhz();
                double p1_v = oc_cfg.custom_vf_enabled ? oc_cfg.p1_volt_mv : hw_ctrl.get_amd_p1_volt_mv();
                double p2_f = oc_cfg.custom_vf_enabled ? oc_cfg.p2_freq_mhz : hw_ctrl.get_amd_p2_freq_mhz();
                double p2_v = oc_cfg.custom_vf_enabled ? oc_cfg.p2_volt_mv : hw_ctrl.get_amd_p2_volt_mv();
                double target_core = oc_cfg.custom_vf_enabled ? oc_cfg.p3_freq_mhz : std::clamp(hw_ctrl.get_amd_def_core_mhz() + oc_cfg.core_clock_offset_mhz, hw_ctrl.get_amd_min_core_mhz(), hw_ctrl.get_amd_max_core_mhz());
                double target_volt = oc_cfg.custom_vf_enabled ? oc_cfg.p3_volt_mv : std::clamp(hw_ctrl.get_amd_def_volt_mv() + oc_cfg.voltage_offset_mv, hw_ctrl.get_amd_min_volt_mv(), hw_ctrl.get_amd_max_volt_mv());

                std::vector<double> v_xs = { p1_v, p2_v, target_volt, 1100.0 };
                std::vector<double> v_ys = { p1_f, p2_f, target_core, target_core };

                ImPlot::SetNextLineStyle(ImVec4(0.00f, 0.90f, 0.46f, 1.0f), 2.5f);
                ImPlot::PlotLine("V/F Curve", v_xs.data(), v_ys.data(), static_cast<int>(v_xs.size()));
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 6.0f, ImVec4(0.00f, 0.90f, 1.00f, 1.0f), 1.5f, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                ImPlot::PlotScatter("P-States (P1, P2, P3)", v_xs.data(), v_ys.data(), 3);

                // Real-time live operating point
                if (gpu.voltage_mv >= 650 && gpu.voltage_mv <= 1150 && gpu.core_clock_mhz > 0) {
                    double cur_v = static_cast<double>(gpu.voltage_mv);
                    double cur_f = static_cast<double>(gpu.core_clock_mhz);
                    double pt_x[1] = { cur_v };
                    double pt_y[1] = { cur_f };
                    ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 7.0f, ImVec4(1.00f, 0.25f, 0.25f, 1.0f), 2.0f, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                    ImPlot::PlotScatter("Live GPU State", pt_x, pt_y, 1);
                }
            } else {
                ImPlot::SetupAxes("Voltage (mV)", "Offset (MHz)", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
                ImPlot::SetupAxesLimits(650.0, 1150.0, -150.0, 300.0, ImPlotCond_Always);

                std::vector<double> v_xs, v_ys;
                for (const auto& vf : oc_cfg.vf_curve_points) {
                    v_xs.push_back(static_cast<double>(vf.voltage_mv + oc_cfg.voltage_offset_mv));
                    v_ys.push_back(static_cast<double>(vf.freq_offset_mhz + oc_cfg.core_clock_offset_mhz));
                }
                ImPlot::SetNextLineStyle(ImVec4(0.00f, 0.90f, 0.46f, 1.0f), 2.5f);
                ImPlot::PlotLine("V/F Curve", v_xs.data(), v_ys.data(), static_cast<int>(v_xs.size()));
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 5.0f, ImVec4(0.00f, 0.90f, 1.00f, 1.0f), 1.5f, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                ImPlot::PlotScatter("Nodes", v_xs.data(), v_ys.data(), static_cast<int>(v_xs.size()));
            }
            ImPlot::EndPlot();
        }
    }

    // Fan Curve Editor
    if (show_fan_curve_) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "CUSTOM FAN CURVE EDITOR:");
        if (ImPlot::BeginPlot("##FanCurvePlot", ImVec2(-1, 180.0f * s), ImPlotFlags_NoTitle | ImPlotFlags_NoBoxSelect)) {
            ImPlot::SetupAxes("Temp (C)", "Fan Speed (%)", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
            ImPlot::SetupAxesLimits(20.0, 100.0, 0.0, 100.0, ImPlotCond_Always);

            std::vector<double> xs, ys;
            for (const auto& p : fan_cfg.points) {
                xs.push_back(static_cast<double>(p.temp_c));
                ys.push_back(static_cast<double>(p.fan_pct));
            }

            ImPlot::SetNextLineStyle(ImVec4(0.00f, 0.85f, 1.00f, 1.0f), 2.5f);
            ImPlot::PlotLine("Fan Curve", xs.data(), ys.data(), static_cast<int>(xs.size()));
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 5.0f, ImVec4(0.00f, 0.90f, 0.46f, 1.0f), 1.5f, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            ImPlot::PlotScatter("Nodes", xs.data(), ys.data(), static_cast<int>(xs.size()));

            if (gpu.temperature_c >= 20 && gpu.temperature_c <= 100) {
                double cur_t = static_cast<double>(gpu.temperature_c);
                double t_xs[2] = { cur_t, cur_t };
                double t_ys[2] = { 0.0, 100.0 };
                ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.40f, 0.10f, 0.85f), 1.5f);
                ImPlot::PlotLine("Live Temp", t_xs, t_ys, 2);
            }
            ImPlot::EndPlot();
        }

        // Curve Node Sliders
        for (size_t i = 0; i < fan_cfg.points.size(); ++i) {
            ImGui::PushID(static_cast<int>(i + 50));
            ImGui::Text("N%d:", static_cast<int>(i + 1));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(75.0f * s);
            ImGui::SliderInt("C##Temp", &fan_cfg.points[i].temp_c, 20, 100, "%d C");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(75.0f * s);
            ImGui::SliderInt("%##Pct", &fan_cfg.points[i].fan_pct, 0, 100, "%d %%");
            ImGui::PopID();
            if (i < fan_cfg.points.size() - 1) ImGui::SameLine(0, 10.0f * s);
        }
    }

    ImGui::EndChild();
}

void DashboardView::render_osd_sensors_tab(ConfigManager& config_mgr, RTSSBridge& rtss_bridge, const AppFonts& fonts) {
    auto& hud = config_mgr.get_hud();
    auto& hotkeys = config_mgr.get_hotkeys();
    float s = fonts.dpi_scale;

    float avail_w = ImGui::GetContentRegionAvail().x;
    bool is_wide = (avail_w > 900.0f * s);
    float col_left_w = is_wide ? ((avail_w - 16.0f * s) * 0.48f) : avail_w;
    float col_right_w = is_wide ? (avail_w - col_left_w - 16.0f * s) : avail_w;

    // ==========================================
    // LEFT COLUMN: RTSS ENGINE, GROUPINGS, SYSTEM & HOTKEYS
    // ==========================================
    ImGui::BeginChild("LeftOsdStack", ImVec2(col_left_w, 0), false);

    // Card 1: RTSS ENGINE & OUTPUT TARGETS
    ImGui::BeginChild("RtssCard", ImVec2(0, 275.0f * s), true);
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "RIVATUNER (RTSS) ENGINE & OUTPUT");
    ImGui::SameLine();
    if (rtss_bridge.is_connected()) {
        ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.45f, 1.0f), "  [ONLINE]");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "  [OFFLINE]");
    }

    if (ImGui::Button("Start / Connect RTSS")) {
        RTSSBridge::launch_rtss();
        Sleep(600);
        rtss_bridge.connect();
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart RTSS")) {
        RTSSBridge::restart_rtss();
        Sleep(600);
        rtss_bridge.connect();
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.32f, 0.10f, 0.14f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.14f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.50f, 0.50f, 1.0f));
    if (ImGui::Button("Shutdown RTSS")) {
        rtss_bridge.clear_osd();
        rtss_bridge.disconnect();
        RTSSBridge::kill_rtss();
    }
    ImGui::PopStyleColor(3);

    ImGui::Checkbox("Output to RTSS OSD (RivaTuner Shared Memory V2)", &hud.output_to_rtss);
    ImGui::Checkbox("DirectX 11 Standalone Transparent Overlay", &hud.output_to_dx11_overlay);
    ImGui::Checkbox("Close RTSS automatically on AnyPerfomans exit", &hud.close_rtss_on_exit);

    // RTSS Framerate Limiter
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.50f, 0.60f, 0.72f, 1.0f), "HARDWARE FRAMERATE LIMITER:");
    int current_fps_limit = config_mgr.get_framerate_limit();
    const struct { const char* label; int limit; } fps_presets[] = {
        { "OFF", 0 }, { "60", 60 }, { "120", 120 }, { "144", 144 }, { "165", 165 }, { "240", 240 }
    };
    for (int p = 0; p < IM_ARRAYSIZE(fps_presets); ++p) {
        if (p > 0) ImGui::SameLine(0, 4.0f * s);
        bool is_active = (current_fps_limit == fps_presets[p].limit);
        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.50f, 0.68f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        }
        if (ImGui::Button(fps_presets[p].label, ImVec2(40.0f * s, 0))) {
            current_fps_limit = fps_presets[p].limit;
            config_mgr.set_framerate_limit(current_fps_limit);
            RTSSBridge::set_framerate_limit(current_fps_limit);
        }
        if (is_active) ImGui::PopStyleColor(2);
    }
    ImGui::SameLine(0, 8.0f * s);
    ImGui::SetNextItemWidth(110.0f * s);
    if (ImGui::InputInt("##FpsCapInput", &current_fps_limit)) {
        if (current_fps_limit < 0) current_fps_limit = 0;
        config_mgr.set_framerate_limit(current_fps_limit);
        RTSSBridge::set_framerate_limit(current_fps_limit);
    }
    ImGui::SameLine(0, 4.0f * s);
    ImGui::TextDisabled("Cap FPS");

    ImGui::EndChild();

    ImGui::Spacing();

    // Card 2: COMPACT 1-LINE GROUPINGS & CPU CORES
    ImGui::BeginChild("GroupingCard", ImVec2(0, 390.0f * s), true);
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "COMPACT SINGLE-LINE GROUPING & CORES");

    ImGui::Columns(2, "CompactCols", false);
    ImGui::Checkbox("Compact GPU Line", &hud.group_gpu_on_one_line);
    ImGui::TextDisabled("%s", "GPU: 65C, 1750MHz, 99%");
    ImGui::Checkbox("Compact VRAM Line", &hud.group_vram_on_one_line);
    ImGui::TextDisabled("%s", "VRAM: 4850MB, 72C");
    ImGui::Checkbox("Compact CPU Line", &hud.group_cpu_on_one_line);
    ImGui::TextDisabled("%s", "CPU: 62C, 48.5%, 65W");
    ImGui::NextColumn();
    ImGui::Checkbox("Compact RAM Line", &hud.group_ram_on_one_line);
    ImGui::TextDisabled("%s", "RAM: 14.2/32GB");
    ImGui::Checkbox("Compact FPS Line", &hud.group_fps_on_one_line);
    ImGui::TextDisabled("%s", "FPS: 144, 6.9ms, 1% 115");
    ImGui::Columns(1);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.50f, 0.60f, 0.72f, 1.0f), "CPU CORES DISPLAY (MSI STYLE):");
    ImGui::Checkbox("Display each CPU Core in Column (в столбик)", &hud.cpu_cores_in_column);
    if (!hud.cpu_cores_in_column) {
        ImGui::Indent(15.0f * s);
        ImGui::Checkbox("Group CPU Cores into 1 Line (в одну строку)", &hud.group_cpu_cores_on_one_line);
        if (!hud.group_cpu_cores_on_one_line) {
            ImGui::SetNextItemWidth(150.0f * s);
            const char* wrap_opts[] = { "4 Cores/Row", "6 Cores/Row", "8 Cores/Row", "12 Cores/Row", "16 Cores/Row" };
            int wrap_vals[] = { 4, 6, 8, 12, 16 };
            int cur_w_idx = 2;
            for (int w = 0; w < 5; ++w) {
                if (hud.cpu_cores_per_row == wrap_vals[w]) { cur_w_idx = w; break; }
            }
            if (ImGui::Combo("Wrap Cores", &cur_w_idx, wrap_opts, IM_ARRAYSIZE(wrap_opts))) {
                hud.cpu_cores_per_row = wrap_vals[cur_w_idx];
            }
        }
        ImGui::Unindent(15.0f * s);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.50f, 0.60f, 0.72f, 1.0f), "ADDITIONAL OSD METRICS:");
    ImGui::Columns(2, "SubMetricsCols", false);
    ImGui::Checkbox("Show 1% Low FPS", &hud.show_1pct_low);
    ImGui::Checkbox("Show 0.1% Low FPS", &hud.show_01pct_low);
    ImGui::Checkbox("Show Min / Max FPS", &hud.show_min_max);
    ImGui::Checkbox("Show Frametime (ms)", &hud.show_frametime_text);
    ImGui::NextColumn();
    ImGui::Checkbox("Show Stutters / Jitter", &hud.show_stutters);
    ImGui::Checkbox("Show Bottleneck Badge", &hud.show_bottleneck_indicator);
    ImGui::Checkbox("Show Secondary Apps Tax", &hud.show_secondary_apps);
    ImGui::Columns(1);

    ImGui::EndChild();

    ImGui::Spacing();

    // Card 3: SYSTEM INTEGRATION, OVERLAY & SHORTCUTS
    ImGui::BeginChild("SystemCard", ImVec2(0, 480.0f * s), true);
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "SYSTEM INTEGRATION & SHORTCUTS");

    ImGui::Checkbox("Minimize to System Tray on close [X button]", &hud.minimize_to_tray_on_close);
    ImGui::Checkbox("Display live hardware temperature directly on Tray Icon", &hud.show_tray_temperature);
    if (hud.show_tray_temperature) {
        ImGui::Indent(15.0f * s);
        if (ImGui::RadioButton("GPU Temp", hud.tray_temp_is_gpu)) { hud.tray_temp_is_gpu = true; }
        ImGui::SameLine();
        if (ImGui::RadioButton("CPU Package Temp", !hud.tray_temp_is_gpu)) { hud.tray_temp_is_gpu = false; }
        ImGui::Unindent(15.0f * s);
    }

    if (ImGui::Checkbox("Start with Windows (Highest Privileges, No UAC)", &hud.autostart_with_windows)) {
        TrayManager::set_autostart_enabled(hud.autostart_with_windows, hud.start_minimized_to_tray);
    }
    if (hud.autostart_with_windows) {
        ImGui::Indent(15.0f * s);
        if (ImGui::Checkbox("Start Minimized directly to System Tray", &hud.start_minimized_to_tray)) {
            TrayManager::set_autostart_enabled(hud.autostart_with_windows, hud.start_minimized_to_tray);
        }
        ImGui::Unindent(15.0f * s);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.50f, 0.60f, 0.72f, 1.0f), "DX11 OVERLAY & POSITION:");
    const char* pos_names[] = { "Top-Left", "Top-Right", "Bottom-Left", "Bottom-Right", "Custom Coordinates" };
    int current_pos = static_cast<int>(hud.position);
    ImGui::SetNextItemWidth(220.0f * s);
    if (ImGui::Combo("Position", &current_pos, pos_names, IM_ARRAYSIZE(pos_names))) {
        hud.position = static_cast<HudPosition>(current_pos);
    }
    if (hud.position == HudPosition::Custom) {
        ImGui::Indent(15.0f * s);
        ImGui::SliderInt("X", &hud.custom_x, 0, 3840);
        ImGui::SliderInt("Y", &hud.custom_y, 0, 2160);
        ImGui::Unindent(15.0f * s);
    }
    ImGui::SetNextItemWidth(180.0f * s);
    ImGui::SliderFloat("HUD Opacity", &hud.bg_opacity, 0.1f, 1.0f, "%.2f");
    ImGui::SameLine(0, 10.0f * s);
    ImGui::SetNextItemWidth(180.0f * s);
    ImGui::SliderFloat("HUD Scale", &hud.font_scale, 0.75f, 1.50f, "%.2fx");
    ImGui::Checkbox("Show Frametime Graph in DX11 HUD", &hud.show_frametime_graph);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.50f, 0.60f, 0.72f, 1.0f), "HOTKEYS & SHORTCUTS:");
    const struct { const char* name; UINT vk; } common_keys[] = {
        { "F1", VK_F1 }, { "F2", VK_F2 }, { "F3", VK_F3 }, { "F4", VK_F4 },
        { "F5", VK_F5 }, { "F6", VK_F6 }, { "F7", VK_F7 }, { "F8", VK_F8 },
        { "F9", VK_F9 }, { "F10", VK_F10 }, { "F11", VK_F11 }, { "F12", VK_F12 },
        { "Insert", VK_INSERT }, { "Delete", VK_DELETE }, { "Home", VK_HOME },
        { "End", VK_END }, { "Page Up", VK_PRIOR }, { "Page Down", VK_NEXT },
        { "Scroll Lock", VK_SCROLL }, { "Pause", VK_PAUSE }, { "` (Tilde)", VK_OEM_3 }
    };

    auto render_key_combo = [&](const char* label, UINT& current_key) {
        ImGui::Text("%s:", label);
        ImGui::SameLine(240.0f * s);
        std::string cur_str = ConfigManager::key_to_string(current_key);
        std::string combo_id = std::string("##KeyCombo_") + label;
        ImGui::SetNextItemWidth(130.0f * s);
        if (ImGui::BeginCombo(combo_id.c_str(), cur_str.c_str())) {
            for (const auto& k : common_keys) {
                bool is_sel = (current_key == k.vk);
                if (ImGui::Selectable(k.name, is_sel)) { current_key = k.vk; }
                if (is_sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    };

    render_key_combo("Toggle Dashboard Window", hotkeys.toggle_overlay_key);
    render_key_combo("Toggle RTSS In-Game OSD", hotkeys.toggle_rtss_osd_key);
    render_key_combo("Toggle Benchmark Recording", hotkeys.toggle_benchmark_key);

    ImGui::Checkbox("Profile Quick-Switch (Ctrl+Shift+1..5)", &hotkeys.enable_profile_hotkeys);

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.48f, 0.65f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.65f, 0.85f, 1.0f));
    ImGui::PushFont(fonts.bold);
    if (ImGui::Button("[ SAVE ALL SETTINGS ]", ImVec2(240.0f * s, 34.0f * s))) {
        config_mgr.save_to_file();
    }
    ImGui::PopFont();
    ImGui::PopStyleColor(2);

    ImGui::EndChild(); // SystemCard

    ImGui::EndChild(); // LeftOsdStack

    // ==========================================
    // RIGHT COLUMN: REORDERABLE SENSORS & LINE GRAPHS
    // ==========================================
    if (is_wide) {
        ImGui::SameLine(0, 16.0f * s);
    } else {
        ImGui::Spacing();
    }

    ImGui::BeginChild("RightOsdStack", ImVec2(col_right_w, 0), false);
    ImGui::BeginChild("SensorsCard", ImVec2(0, 0), true);

    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "REORDERABLE SENSORS & LINE GRAPHS");
    ImGui::TextDisabled("Customize sensor labels, order, colors and toggle hardware line graphs for RTSS OSD.");

    ImGui::Spacing();

    // Global Palette
    ImGui::TextColored(ImVec4(0.50f, 0.60f, 0.72f, 1.0f), "GLOBAL OSD PALETTE:");
    ColorPickerHex("##ValCol", hud.color_values);
    ImGui::SameLine();
    ImGui::Text("Values");

    ImGui::SameLine(130.0f * s);
    ColorPickerHex("##StutCol", hud.color_stutter);
    ImGui::SameLine();
    ImGui::Text("Stutter");

    ImGui::SameLine(250.0f * s);
    ColorPickerHex("##BneckCol", hud.color_bottleneck);
    ImGui::SameLine();
    ImGui::Text("Bottleneck");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Hardware Line Graph Options
    ImGui::TextColored(ImVec4(0.50f, 0.60f, 0.72f, 1.0f), "HARDWARE LINE GRAPH OPTIONS:");
    ImGui::Checkbox("Fill Graph Background", &hud.rtss_graph_filled);
    ImGui::SameLine(220.0f * s);
    ImGui::SetNextItemWidth(140.0f * s);
    ImGui::SliderFloat("Frametime Max", &hud.rtss_frametime_max_ms, 16.6f, 100.0f, "%.1f ms");

    int graph_h = hud.rtss_graph_height;
    const char* height_options[] = { "2 Lines (-2)", "3 Lines (-3)", "4 Lines (-4, Def)", "5 Lines (-5)" };
    int current_h_idx = (graph_h == -2) ? 0 : (graph_h == -3) ? 1 : (graph_h == -5) ? 3 : 2;
    ImGui::SetNextItemWidth(150.0f * s);
    if (ImGui::Combo("Height", &current_h_idx, height_options, IM_ARRAYSIZE(height_options))) {
        int vals[] = { -2, -3, -4, -5 };
        hud.rtss_graph_height = vals[current_h_idx];
    }
    ImGui::SameLine(220.0f * s);
    const char* width_options[] = { "24 Chars (-24)", "32 Chars (-32, Def)", "40 Chars (-40)", "48 Chars (-48)" };
    int graph_w = hud.rtss_graph_width;
    int current_w_idx = (graph_w == -24) ? 0 : (graph_w == -40) ? 2 : (graph_w == -48) ? 3 : 1;
    ImGui::SetNextItemWidth(150.0f * s);
    if (ImGui::Combo("Width", &current_w_idx, width_options, IM_ARRAYSIZE(width_options))) {
        int vals[] = { -24, -32, -40, -48 };
        hud.rtss_graph_width = vals[current_w_idx];
    }

    ImGui::Spacing();
    if (ImGui::Button("[ Reset Sensors to Default Layout ]")) {
        ConfigManager::init_default_sensors(hud.sensors);
    }

    ImGui::Spacing();

    if (hud.sensors.empty()) {
        ConfigManager::init_default_sensors(hud.sensors);
    }

    if (ImGui::BeginTable("SensorsReorderTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 54.0f * s);
        ImGui::TableSetupColumn("OSD", ImGuiTableColumnFlags_WidthFixed, 40.0f * s);
        ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthFixed, 46.0f * s);
        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 50.0f * s);
        ImGui::TableSetupColumn("Label / Custom Name", ImGuiTableColumnFlags_WidthFixed, 150.0f * s);
        ImGui::TableSetupColumn("Hardware Sensor", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(hud.sensors.size()); ++i) {
            auto& sens = hud.sensors[i];
            ImGui::TableNextRow();

            // Column 0: Move Up / Down
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i * 100 + 1);
            if (i > 0) {
                if (ImGui::SmallButton("^")) {
                    std::swap(hud.sensors[i], hud.sensors[i - 1]);
                }
            } else {
                ImGui::Dummy(ImVec2(14.0f * s, 0));
            }
            ImGui::SameLine(0, 4.0f * s);
            if (i + 1 < static_cast<int>(hud.sensors.size())) {
                if (ImGui::SmallButton("v")) {
                    std::swap(hud.sensors[i], hud.sensors[i + 1]);
                }
            }
            ImGui::PopID();

            // Column 1: Enabled (OSD)
            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(i * 100 + 2);
            ImGui::Checkbox("##Show", &sens.enabled);
            ImGui::PopID();

            // Column 2: Graph (Line graph)
            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(i * 100 + 3);
            ImGui::Checkbox("##Graph", &sens.show_graph);
            ImGui::PopID();

            // Column 3: Color Picker
            ImGui::TableSetColumnIndex(3);
            ImGui::PushID(i * 100 + 4);
            ColorPickerHex("##Col", sens.color);
            ImGui::PopID();

            // Column 4: Label InputText
            ImGui::TableSetColumnIndex(4);
            ImGui::PushID(i * 100 + 5);
            char lbl_buf[64];
            strncpy_s(lbl_buf, sizeof(lbl_buf), sens.label.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##Lbl", lbl_buf, sizeof(lbl_buf))) {
                sens.label = lbl_buf;
            }
            ImGui::PopID();

            // Column 5: Hardware Sensor identifier
            ImGui::TableSetColumnIndex(5);
            ImGui::TextDisabled("%s", sens.id.c_str());
        }
        ImGui::EndTable();
    }

    ImGui::EndChild(); // SensorsCard
    ImGui::EndChild(); // RightOsdStack
}

void DashboardView::render_vf_curve_editor_modal(
    HardwareController& hw_ctrl,
    ConfigManager& config_mgr,
    const GpuMetrics& gpu,
    const AppFonts& fonts
) {
    if (!show_curve_editor_modal_) return;

    float s = fonts.dpi_scale;
    ImGui::SetNextWindowSize(ImVec2(880.0f * s, 620.0f * s), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(680.0f * s, 460.0f * s), ImVec2(1920.0f * s, 1080.0f * s));

    bool open = show_curve_editor_modal_;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Voltage / Frequency Curve Editor (MSI Afterburner Style) [Ctrl+F]##VfModal", &open, flags)) {
        auto& oc = config_mgr.get_oc();
        bool is_amd = hw_ctrl.is_amd() || gpu.is_amd || 
                      (gpu.adapter_name.find(L"Radeon") != std::wstring::npos) || 
                      (gpu.adapter_name.find(L"AMD") != std::wstring::npos);

        // Header Banner with GPU and Live Telemetry
        ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "GPU HARDWARE V/F CURVE CALIBRATION (AFTERBURNER ARCHITECTURE)");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 240.0f * s);
        if (gpu.voltage_mv > 0 && gpu.core_clock_mhz > 0) {
            ImGui::TextColored(ImVec4(0.00f, 0.90f, 0.46f, 1.0f), "LIVE: %d MHz @ %d mV", gpu.core_clock_mhz, gpu.voltage_mv);
        } else if (gpu.core_clock_mhz > 0) {
            ImGui::TextColored(ImVec4(0.00f, 0.90f, 0.46f, 1.0f), "LIVE: %d MHz", gpu.core_clock_mhz);
        }

        ImGui::TextDisabled("Interactive Voltage-Frequency curve editor. Drag control points (P1, P2, P3) directly on the plot or use precision inputs below.");
        ImGui::Separator();
        ImGui::Spacing();

        // Safe bounds
        int min_v = (is_amd && hw_ctrl.has_amd_od8()) ? hw_ctrl.get_amd_min_volt_mv() : 700;
        int max_v = (is_amd && hw_ctrl.has_amd_od8()) ? hw_ctrl.get_amd_max_volt_mv() : 1150;
        int min_c = (is_amd && hw_ctrl.has_amd_od8()) ? hw_ctrl.get_amd_min_core_mhz() : 800;
        int max_c = (is_amd && hw_ctrl.has_amd_od8()) ? hw_ctrl.get_amd_max_core_mhz() : 2000;

        // Initialize points if first time or 0
        if (oc.p1_freq_mhz <= 0 || oc.p3_freq_mhz <= 0) {
            oc.p1_freq_mhz = (hw_ctrl.has_amd_od8()) ? hw_ctrl.get_amd_p1_freq_mhz() : 800;
            oc.p1_volt_mv  = (hw_ctrl.has_amd_od8()) ? hw_ctrl.get_amd_p1_volt_mv() : 700;
            oc.p2_freq_mhz = (hw_ctrl.has_amd_od8()) ? hw_ctrl.get_amd_p2_freq_mhz() : 1290;
            oc.p2_volt_mv  = (hw_ctrl.has_amd_od8()) ? hw_ctrl.get_amd_p2_volt_mv() : 776;
            oc.p3_freq_mhz = (hw_ctrl.has_amd_od8()) ? hw_ctrl.get_amd_def_core_mhz() : 1750;
            oc.p3_volt_mv  = (hw_ctrl.has_amd_od8()) ? hw_ctrl.get_amd_def_volt_mv() : 950;
        }

        // Main ImPlot
        float plot_h = ImGui::GetContentRegionAvail().y - 150.0f * s;
        if (plot_h < 220.0f * s) plot_h = 220.0f * s;

        if (ImPlot::BeginPlot("##VfCurveInteractivePlot", ImVec2(-1, plot_h), ImPlotFlags_NoTitle)) {
            ImPlot::SetupAxes("Core Voltage (mV)", "Core Clock (MHz)", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
            ImPlot::SetupAxesLimits(min_v - 60.0, max_v + 60.0, min_c - 120.0, max_c + 120.0, ImPlotCond_Always);

            // Drag Points for P1, P2, P3
            double p1_x = static_cast<double>(oc.p1_volt_mv);
            double p1_y = static_cast<double>(oc.p1_freq_mhz);
            double p2_x = static_cast<double>(oc.p2_volt_mv);
            double p2_y = static_cast<double>(oc.p2_freq_mhz);
            double p3_x = static_cast<double>(oc.p3_volt_mv);
            double p3_y = static_cast<double>(oc.p3_freq_mhz);

            if (ImPlot::DragPoint(1, &p1_x, &p1_y, ImVec4(0.00f, 0.90f, 1.00f, 1.0f), 7.0f)) {
                oc.p1_volt_mv = std::clamp(static_cast<int>(std::round(p1_x)), min_v, oc.p2_volt_mv - 10);
                oc.p1_freq_mhz = std::clamp(static_cast<int>(std::round(p1_y)), min_c, oc.p2_freq_mhz - 50);
                oc.custom_vf_enabled = true;
            }
            if (ImPlot::DragPoint(2, &p2_x, &p2_y, ImVec4(0.00f, 0.90f, 1.00f, 1.0f), 7.0f)) {
                oc.p2_volt_mv = std::clamp(static_cast<int>(std::round(p2_x)), oc.p1_volt_mv + 10, oc.p3_volt_mv - 10);
                oc.p2_freq_mhz = std::clamp(static_cast<int>(std::round(p2_y)), oc.p1_freq_mhz + 50, oc.p3_freq_mhz - 50);
                oc.custom_vf_enabled = true;
            }
            if (ImPlot::DragPoint(3, &p3_x, &p3_y, ImVec4(0.00f, 0.90f, 0.46f, 1.0f), 8.0f)) {
                oc.p3_volt_mv = std::clamp(static_cast<int>(std::round(p3_x)), oc.p2_volt_mv + 10, max_v);
                oc.p3_freq_mhz = std::clamp(static_cast<int>(std::round(p3_y)), oc.p2_freq_mhz + 50, max_c);
                oc.custom_vf_enabled = true;
            }

            // Generate full smooth spline through P1 -> P2 -> P3 -> flatline
            std::vector<double> xs, ys;
            for (int i = 0; i <= 80; ++i) {
                double v = (min_v - 40.0) + ((max_v + 40.0) - (min_v - 40.0)) * (static_cast<double>(i) / 80.0);
                double freq = 0.0;
                if (v <= oc.p1_volt_mv) {
                    freq = oc.p1_freq_mhz;
                } else if (v <= oc.p2_volt_mv) {
                    double t = (v - oc.p1_volt_mv) / (oc.p2_volt_mv - oc.p1_volt_mv);
                    freq = oc.p1_freq_mhz + t * (oc.p2_freq_mhz - oc.p1_freq_mhz);
                } else if (v <= oc.p3_volt_mv) {
                    double t = (v - oc.p2_volt_mv) / (oc.p3_volt_mv - oc.p2_volt_mv);
                    freq = oc.p2_freq_mhz + t * (oc.p3_freq_mhz - oc.p2_freq_mhz);
                } else {
                    freq = oc.p3_freq_mhz; // Flattened past P3 (Afterburner style!)
                }
                xs.push_back(v);
                ys.push_back(freq);
            }

            // Draw line
            ImPlot::SetNextLineStyle(ImVec4(0.00f, 0.90f, 0.46f, 1.0f), 2.5f);
            ImPlot::PlotLine("V/F Curve", xs.data(), ys.data(), static_cast<int>(xs.size()));

            // Draw P-State markers
            double p_xs[4] = { static_cast<double>(oc.p1_volt_mv), static_cast<double>(oc.p2_volt_mv), static_cast<double>(oc.p3_volt_mv), static_cast<double>(max_v + 30) };
            double p_ys[4] = { static_cast<double>(oc.p1_freq_mhz), static_cast<double>(oc.p2_freq_mhz), static_cast<double>(oc.p3_freq_mhz), static_cast<double>(oc.p3_freq_mhz) };
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 7.0f, ImVec4(0.00f, 0.90f, 1.00f, 1.0f), 1.5f, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            ImPlot::PlotScatter("Nodes (P1, P2, P3, Cap)", p_xs, p_ys, 4);

            // Real-Time Live Operating Point Crosshair & Indicator
            if (gpu.voltage_mv >= (min_v - 40) && gpu.voltage_mv <= (max_v + 60) && gpu.core_clock_mhz > 0) {
                double cur_v = static_cast<double>(gpu.voltage_mv);
                double cur_f = static_cast<double>(gpu.core_clock_mhz);
                double pt_x[1] = { cur_v };
                double pt_y[1] = { cur_f };
                // Vertical live crosshair line
                double vl_x[2] = { cur_v, cur_v };
                double vl_y[2] = { static_cast<double>(min_c - 120), static_cast<double>(max_c + 120) };
                ImPlot::SetNextLineStyle(ImVec4(1.00f, 0.20f, 0.20f, 0.80f), 1.5f);
                ImPlot::PlotLine("##LiveVLine", vl_x, vl_y, 2);

                // Horizontal live crosshair line
                double hl_x[2] = { static_cast<double>(min_v - 60), static_cast<double>(max_v + 60) };
                double hl_y[2] = { cur_f, cur_f };
                ImPlot::SetNextLineStyle(ImVec4(1.00f, 0.20f, 0.20f, 0.50f), 1.0f);
                ImPlot::PlotLine("##LiveHLine", hl_x, hl_y, 2);

                // Live glowing point
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 8.0f, ImVec4(1.00f, 0.25f, 0.25f, 1.0f), 2.0f, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                ImPlot::PlotScatter("Live GPU State", pt_x, pt_y, 1);
            }

            ImPlot::EndPlot();
        }

        // Digital Fine-tuning Controls (P1, P2, P3)
        ImGui::Columns(3, "PStateCols", false);

        ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "P1 (Low State):");
        ImGui::SetNextItemWidth(90.0f * s);
        if (ImGui::InputInt("mV##P1V", &oc.p1_volt_mv, 5)) { oc.custom_vf_enabled = true; }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f * s);
        if (ImGui::InputInt("MHz##P1F", &oc.p1_freq_mhz, 10)) { oc.custom_vf_enabled = true; }

        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "P2 (Mid State):");
        ImGui::SetNextItemWidth(90.0f * s);
        if (ImGui::InputInt("mV##P2V", &oc.p2_volt_mv, 5)) { oc.custom_vf_enabled = true; }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f * s);
        if (ImGui::InputInt("MHz##P2F", &oc.p2_freq_mhz, 10)) { oc.custom_vf_enabled = true; }

        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.00f, 0.90f, 0.46f, 1.0f), "P3 (Peak State / Undervolt):");
        ImGui::SetNextItemWidth(90.0f * s);
        if (ImGui::InputInt("mV##P3V", &oc.p3_volt_mv, 5)) { oc.custom_vf_enabled = true; }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f * s);
        if (ImGui::InputInt("MHz##P3F", &oc.p3_freq_mhz, 10)) { oc.custom_vf_enabled = true; }

        ImGui::Columns(1);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Presets & Action Buttons
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.18f, 0.25f, 0.90f));
        if (ImGui::Button("Sweet Spot: 1750 MHz @ 940 mV (Navi 10)")) {
            oc.p1_freq_mhz = 800; oc.p1_volt_mv = 700;
            oc.p2_freq_mhz = 1300; oc.p2_volt_mv = 780;
            oc.p3_freq_mhz = 1750; oc.p3_volt_mv = 940;
            oc.custom_vf_enabled = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cool & Quiet: 1700 MHz @ 900 mV")) {
            oc.p1_freq_mhz = 800; oc.p1_volt_mv = 700;
            oc.p2_freq_mhz = 1250; oc.p2_volt_mv = 760;
            oc.p3_freq_mhz = 1700; oc.p3_volt_mv = 900;
            oc.custom_vf_enabled = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset to VBIOS Curve")) {
            oc.p1_freq_mhz = (hw_ctrl.has_amd_od8()) ? hw_ctrl.get_amd_p1_freq_mhz() : 800;
            oc.p1_volt_mv  = (hw_ctrl.has_amd_od8()) ? hw_ctrl.get_amd_p1_volt_mv() : 700;
            oc.p2_freq_mhz = (hw_ctrl.has_amd_od8()) ? hw_ctrl.get_amd_p2_freq_mhz() : 1290;
            oc.p2_volt_mv  = (hw_ctrl.has_amd_od8()) ? hw_ctrl.get_amd_p2_volt_mv() : 776;
            oc.p3_freq_mhz = (hw_ctrl.has_amd_od8()) ? hw_ctrl.get_amd_def_core_mhz() : 1750;
            oc.p3_volt_mv  = (hw_ctrl.has_amd_od8()) ? hw_ctrl.get_amd_def_volt_mv() : 1008;
            oc.custom_vf_enabled = false;
        }
        ImGui::PopStyleColor();

        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > 250.0f * s) {
            ImGui::SameLine(ImGui::GetWindowWidth() - 260.0f * s);
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.50f, 0.70f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.70f, 0.90f, 1.0f));
        ImGui::PushFont(fonts.bold);
        if (ImGui::Button("[ APPLY TO GPU ]", ImVec2(140.0f * s, 32.0f * s))) {
            oc.enabled = true;
            hw_ctrl.apply_overclock(oc);
            config_mgr.save_to_file();
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(90.0f * s, 32.0f * s))) {
            open = false;
        }
    }
    ImGui::End();

    show_curve_editor_modal_ = open;
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
    if (hud.show_cpu_per_core && !cpu.per_core_usage.empty()) {
        if (hud.cpu_cores_in_column) {
            hud_height += static_cast<float>(cpu.per_core_usage.size()) * 20.0f * hud.font_scale * fonts.dpi_scale;
        } else if (!hud.group_cpu_cores_on_one_line) {
            size_t per_row = (hud.cpu_cores_per_row > 0) ? static_cast<size_t>(hud.cpu_cores_per_row) : 8;
            size_t rows = (cpu.per_core_usage.size() + per_row - 1) / per_row;
            hud_height += static_cast<float>(rows) * 22.0f * hud.font_scale * fonts.dpi_scale;
        } else {
            hud_height += 22.0f * hud.font_scale * fonts.dpi_scale;
        }
    }
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

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.05f, 0.08f, hud.bg_opacity));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.00f, 0.85f, 1.00f, 0.50f));

    ImGui::Begin("AnyPerfRTSSHud", nullptr, flags);

    // Title / Switch button (HUD Style)
    ImGui::PushFont(fonts.bold);
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.0f), "ANYPERFOMANS");
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetWindowWidth() - 85.0f * hud.font_scale * fonts.dpi_scale);
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
        if (gpu.voltage_mv > 0) gpu_ss << "| " << gpu.voltage_mv << " mV ";
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
            if (hud.cpu_cores_in_column) {
                for (size_t c = 0; c < cpu.per_core_usage.size(); ++c) {
                    ImGui::TextColored(hex_to_imvec4(hud.color_cpu), "CPU%d : ", static_cast<int>(c + 1));
                    ImGui::SameLine();
                    ImGui::TextColored(hex_to_imvec4(hud.color_values), "%d%%", static_cast<int>(std::round(cpu.per_core_usage[c])));
                }
            } else if (hud.group_cpu_cores_on_one_line) {
                ImGui::TextColored(hex_to_imvec4(hud.color_cpu), "CORES: ");
                ImGui::SameLine();
                std::stringstream core_ss;
                for (size_t c = 0; c < cpu.per_core_usage.size(); ++c) {
                    if (c > 0) core_ss << " ";
                    core_ss << static_cast<int>(std::round(cpu.per_core_usage[c])) << "%";
                }
                ImGui::TextColored(hex_to_imvec4(hud.color_values), "%s", core_ss.str().c_str());
            } else {
                size_t per_row = (hud.cpu_cores_per_row > 0) ? static_cast<size_t>(hud.cpu_cores_per_row) : 8;
                for (size_t start_c = 0; start_c < cpu.per_core_usage.size(); start_c += per_row) {
                    size_t end_c = (std::min)(start_c + per_row, cpu.per_core_usage.size());
                    ImGui::TextColored(hex_to_imvec4(hud.color_cpu), "C%d-%d: ", static_cast<int>(start_c + 1), static_cast<int>(end_c));
                    ImGui::SameLine();
                    std::stringstream core_ss;
                    for (size_t c = start_c; c < end_c; ++c) {
                        if (c > start_c) core_ss << " ";
                        core_ss << static_cast<int>(std::round(cpu.per_core_usage[c])) << "%";
                    }
                    ImGui::TextColored(hex_to_imvec4(hud.color_values), "%s", core_ss.str().c_str());
                }
            }
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
