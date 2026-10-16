#include "anyperf/rtss_bridge.hpp"
#include "anyperf/utils.hpp"
#include <sstream>
#include <iomanip>
#include <intrin.h>

namespace anyperf {

// Self-contained RTSS Shared Memory V2 structures
#define RTSS_SIGNATURE 0x53535452 // 'RTSS' in little-endian

#pragma pack(push, 1)

struct RTSS_HEADER {
    DWORD dwSignature;
    DWORD dwVersion;
    DWORD dwAppEntrySize;
    DWORD dwAppArrOffset;
    DWORD dwAppArrSize;
    DWORD dwOSDEntrySize;
    DWORD dwOSDArrOffset;
    DWORD dwOSDArrSize;
    DWORD dwOSDFrame;
    LONG  dwBusy; // valid for v2.14+
    DWORD dwDesktopVideoCaptureFlags;
    DWORD dwDesktopVideoCaptureStat[5];
    DWORD dwLastForegroundApp;
    DWORD dwLastForegroundAppProcessID;
};

struct RTSS_OSD_SLOT {
    char szOSD[256];
    char szOSDOwner[256];
    char szOSDEx[4096];
};

struct RTSS_APP_SLOT {
    DWORD dwProcessID;
    char  szName[MAX_PATH];
    DWORD dwFlags;
    DWORD dwTime0;
    DWORD dwTime1;
    DWORD dwFrames;
    DWORD dwFrameTime; // in microseconds
};

#pragma pack(pop)

RTSSBridge::RTSSBridge() {
    connect();
}

RTSSBridge::~RTSSBridge() {
    clear_osd();
    disconnect();
}

bool RTSSBridge::connect() {
    if (is_connected_) return true;

    h_map_file_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, "RTSSSharedMemoryV2");
    if (!h_map_file_) {
        is_connected_ = false;
        return false;
    }

    map_addr_ = MapViewOfFile(h_map_file_, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!map_addr_) {
        CloseHandle(h_map_file_);
        h_map_file_ = nullptr;
        is_connected_ = false;
        return false;
    }

    auto* pHeader = reinterpret_cast<RTSS_HEADER*>(map_addr_);
    if (pHeader->dwSignature != RTSS_SIGNATURE || pHeader->dwVersion < 0x00020000) {
        disconnect();
        return false;
    }

    is_connected_ = true;
    return true;
}

void RTSSBridge::disconnect() {
    if (map_addr_) {
        UnmapViewOfFile(map_addr_);
        map_addr_ = nullptr;
    }
    if (h_map_file_) {
        CloseHandle(h_map_file_);
        h_map_file_ = nullptr;
    }
    is_connected_ = false;
}

bool RTSSBridge::write_to_shared_memory(const char* text) {
    if (!is_connected_ && !connect()) {
        return false;
    }

    auto* pHeader = reinterpret_cast<RTSS_HEADER*>(map_addr_);
    if (!pHeader || pHeader->dwSignature != RTSS_SIGNATURE) {
        disconnect();
        return false;
    }

    // Read detected foreground 3D application from RTSS
    if (pHeader->dwVersion >= 0x00020010 && pHeader->dwLastForegroundAppProcessID != 0) {
        foreground_pid_ = pHeader->dwLastForegroundAppProcessID;
        if (pHeader->dwLastForegroundApp < pHeader->dwAppArrSize) {
            auto* pApp = reinterpret_cast<RTSS_APP_SLOT*>(
                reinterpret_cast<LPBYTE>(pHeader) + pHeader->dwAppArrOffset + pHeader->dwLastForegroundApp * pHeader->dwAppEntrySize
            );
            foreground_name_ = pApp->szName;
        }
    }

    bool updated = false;

    // Two passes:
    // Pass 0: Find our existing slot owned by "AnyPerfomans"
    // Pass 1: Find first free slot (starting from index 1 to leave slot 0 for MSI Afterburner)
    for (DWORD pass = 0; pass < 2; ++pass) {
        for (DWORD entry = 1; entry < pHeader->dwOSDArrSize; ++entry) {
            auto* pEntry = reinterpret_cast<RTSS_OSD_SLOT*>(
                reinterpret_cast<LPBYTE>(pHeader) + pHeader->dwOSDArrOffset + entry * pHeader->dwOSDEntrySize
            );

            if (pass == 1 && pEntry->szOSDOwner[0] == '\0') {
                strcpy_s(pEntry->szOSDOwner, sizeof(pEntry->szOSDOwner), "AnyPerfomans");
            }

            if (strcmp(pEntry->szOSDOwner, "AnyPerfomans") == 0) {
                if (pHeader->dwVersion >= 0x00020007) {
                    // Extended text slot (up to 4096 bytes with rich formatting)
                    if (pHeader->dwVersion >= 0x0002000E) {
                        DWORD dwBusy = _interlockedbittestandset(&pHeader->dwBusy, 0);
                        if (!dwBusy) {
                            strncpy_s(pEntry->szOSDEx, sizeof(pEntry->szOSDEx), text, sizeof(pEntry->szOSDEx) - 1);
                            pHeader->dwBusy = 0;
                        }
                    } else {
                        strncpy_s(pEntry->szOSDEx, sizeof(pEntry->szOSDEx), text, sizeof(pEntry->szOSDEx) - 1);
                    }
                } else {
                    strncpy_s(pEntry->szOSD, sizeof(pEntry->szOSD), text, sizeof(pEntry->szOSD) - 1);
                }

                pHeader->dwOSDFrame++;
                updated = true;
                break;
            }
        }
        if (updated) break;
    }

    return updated;
}

void RTSSBridge::clear_osd() {
    if (!is_connected_ && !connect()) return;

    auto* pHeader = reinterpret_cast<RTSS_HEADER*>(map_addr_);
    if (!pHeader || pHeader->dwSignature != RTSS_SIGNATURE) return;

    for (DWORD entry = 1; entry < pHeader->dwOSDArrSize; ++entry) {
        auto* pEntry = reinterpret_cast<RTSS_OSD_SLOT*>(
            reinterpret_cast<LPBYTE>(pHeader) + pHeader->dwOSDArrOffset + entry * pHeader->dwOSDEntrySize
        );

        if (strcmp(pEntry->szOSDOwner, "AnyPerfomans") == 0) {
            pEntry->szOSD[0] = '\0';
            pEntry->szOSDEx[0] = '\0';
            pEntry->szOSDOwner[0] = '\0';
            pHeader->dwOSDFrame++;
            break;
        }
    }
}

bool RTSSBridge::update_osd(
    const ProcessMonitor& proc_mon,
    const GpuMetrics& gpu,
    const FrameStats& frames,
    const ConfigManager& config_mgr,
    bool is_benchmarking,
    double benchmark_elapsed_sec
) {
    const auto& hud = config_mgr.get_hud();
    if (!hud.output_to_rtss) {
        clear_osd();
        return false;
    }

    std::stringstream ss;

    // 1. Live Benchmark Recording Badge (RTSS Style)
    if (is_benchmarking) {
        int sec = static_cast<int>(benchmark_elapsed_sec);
        int mins = sec / 60;
        int rem_sec = sec % 60;

        ss << "<C=FF1744><S=105>[REC ";
        ss << std::setfill('0') << std::setw(2) << mins << ":"
           << std::setfill('0') << std::setw(2) << rem_sec << "] ";
        ss << "AVG: " << std::fixed << std::setprecision(0) << frames.avg_fps << " | "
           << "1%: " << std::fixed << std::setprecision(0) << frames.one_percent_low << " | "
           << "0.1%: " << std::fixed << std::setprecision(0) << frames.zero_one_percent_low << " | "
           << "STUTTERS: " << frames.stutter_count << "</S></C>\n";
    }

    // 2. GPU Telemetry Row
    if (hud.show_gpu_usage || hud.show_gpu_temp || hud.show_gpu_vram || hud.show_gpu_clock) {
        ss << "<C=00E5FF>GPU : </C><C=FFFFFF>";
        if (hud.show_gpu_usage) ss << gpu.core_usage_percent << "% ";
        if (hud.show_gpu_temp && gpu.temperature_c > 0) ss << "| " << gpu.temperature_c << " C ";
        if (hud.show_gpu_clock && gpu.core_clock_mhz > 0) ss << "| " << gpu.core_clock_mhz << " MHz ";
        if (hud.show_gpu_vram) {
            float vram_used_mb = static_cast<float>(gpu.vram_used_bytes) / (1024.0f * 1024.0f);
            float vram_tot_mb = static_cast<float>(gpu.vram_total_bytes) / (1024.0f * 1024.0f);
            if (vram_tot_mb > 0) {
                ss << "| " << static_cast<int>(vram_used_mb) << "/" << static_cast<int>(vram_tot_mb) << " MB";
            } else {
                ss << "| " << static_cast<int>(vram_used_mb) << " MB";
            }
        }
        ss << "</C>\n";
    }

    // 3. CPU Telemetry Row
    if (hud.show_cpu_usage) {
        const auto* primary = proc_mon.get_primary_target();
        ss << "<C=5CC8FF>CPU : </C><C=FFFFFF>";
        if (primary && primary->is_alive) {
            ss << std::fixed << std::setprecision(1) << primary->cpu_percent << "% ";
            if (primary->cpu_percent < 0.05) {
                ss << "[IDLE] ";
            }
            ss << "(" << wide_to_utf8(primary->name) << ")";
        } else {
            ss << "Idle";
        }
        ss << "</C>\n";
    }

    // 4. System RAM & Pagefile Row
    if (hud.show_system_ram) {
        float ram_used_gb = static_cast<float>(proc_mon.get_system_ram_used()) / (1024.0f * 1024.0f * 1024.0f);
        float ram_tot_gb = static_cast<float>(proc_mon.get_system_ram_total()) / (1024.0f * 1024.0f * 1024.0f);
        if (ram_tot_gb > 0.0f) {
            ss << "<C=FFB300>RAM : </C><C=FFFFFF>"
               << std::fixed << std::setprecision(1) << ram_used_gb << " / "
               << std::fixed << std::setprecision(1) << ram_tot_gb << " GB";
            if (hud.show_pagefile) {
                float page_used_gb = static_cast<float>(proc_mon.get_system_commit_used()) / (1024.0f * 1024.0f * 1024.0f);
                ss << " | Page: " << std::fixed << std::setprecision(1) << page_used_gb << " GB";
            }
            ss << "</C>\n";
        }
    }

    // 5. FPS, Percentiles, Frametime & Stutters Row
    if (hud.show_fps) {
        ss << "<C=00E676>FPS : </C><C=FFFFFF>"
           << std::fixed << std::setprecision(0) << frames.current_fps;

        if (hud.show_1pct_low && frames.one_percent_low > 0.0) {
            ss << "  (1%: " << std::fixed << std::setprecision(0) << frames.one_percent_low;
            if (hud.show_01pct_low && frames.zero_one_percent_low > 0.0) {
                ss << ", 0.1%: " << std::fixed << std::setprecision(0) << frames.zero_one_percent_low;
            }
            if (hud.show_001pct_low && frames.zero_zero_one_percent_low > 0.0) {
                ss << ", 0.01%: " << std::fixed << std::setprecision(0) << frames.zero_zero_one_percent_low;
            }
            ss << ")";
        }

        if (hud.show_frametime_text) {
            ss << "  FT: " << std::fixed << std::setprecision(1) << frames.frametime_ms << "ms";
        }

        if (hud.show_stutters && frames.stutter_count > 0) {
            ss << "  <C=FF1744>[HITCH: " << frames.stutter_count << "]</C>";
        }
        ss << "</C>\n";
    }

    // 6. Monitored Secondary Background Applications (AnyPerfomans exclusive!)
    if (hud.show_secondary_apps) {
        const auto& targets = proc_mon.get_monitored_targets();
        bool has_secondary = false;
        for (const auto& t : targets) {
            if (t.is_primary) continue;
            has_secondary = true;
            float ram_mb = static_cast<float>(t.private_working_set) / (1024.0f * 1024.0f);
            std::string short_name = wide_to_utf8(t.name);
            if (short_name.length() > 10) short_name = short_name.substr(0, 10);

            // Format in clean distinct color
            ss << "<C=FF5577>" << short_name << "</C> : <C=FFFFFF>"
               << std::fixed << std::setprecision(1) << t.cpu_percent << "% | "
               << static_cast<int>(ram_mb) << " MB</C>\n";
        }

        if (has_secondary) {
            double total_sec_cpu = proc_mon.get_total_secondary_cpu();
            float total_sec_ram = static_cast<float>(proc_mon.get_total_secondary_ram()) / (1024.0f * 1024.0f);
            ss << "<C=FF9100>App Tax : </C><C=FF3D00>+"
               << std::fixed << std::setprecision(1) << total_sec_cpu << "% CPU | "
               << static_cast<int>(total_sec_ram) << " MB RAM</C>\n";
        }
    }

    return write_to_shared_memory(ss.str().c_str());
}

} // namespace anyperf
