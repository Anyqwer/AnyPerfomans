#include "anyperf/rtss_bridge.hpp"
#include "anyperf/utils.hpp"
#include <sstream>
#include <iomanip>
#include <intrin.h>
#include <tlhelp32.h>
#include <shellapi.h>

namespace anyperf {

// Self-contained RTSS Shared Memory V2 structures
#define RTSS_SIGNATURE 0x52545353 // 'RTSS' in MSVC

static inline bool is_rtss_signature_valid(DWORD sig) {
    return sig == 0x52545353 || sig == 0x53535452 || sig == 'RTSS';
}

#define RTSS_EMBEDDED_OBJECT_GRAPH_SIGNATURE 'GR00'

#define RTSS_EMBEDDED_OBJECT_GRAPH_FLAG_FILLED        1
#define RTSS_EMBEDDED_OBJECT_GRAPH_FLAG_FRAMERATE     2
#define RTSS_EMBEDDED_OBJECT_GRAPH_FLAG_FRAMETIME     4
#define RTSS_EMBEDDED_OBJECT_GRAPH_FLAG_BAR           8
#define RTSS_EMBEDDED_OBJECT_GRAPH_FLAG_BGND          16
#define RTSS_EMBEDDED_OBJECT_GRAPH_FLAG_VERTICAL      32
#define RTSS_EMBEDDED_OBJECT_GRAPH_FLAG_MIRRORED      64
#define RTSS_EMBEDDED_OBJECT_GRAPH_FLAG_AUTOSCALE     128

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

struct RTSS_EMBEDDED_OBJECT {
    DWORD dwSignature;
    DWORD dwSize;
    LONG dwWidth;
    LONG dwHeight;
    LONG dwMargin;
};

struct RTSS_EMBEDDED_OBJECT_GRAPH {
    RTSS_EMBEDDED_OBJECT header;
    DWORD dwFlags;
    FLOAT fltMin;
    FLOAT fltMax;
    DWORD dwDataCount;
    FLOAT fltData[1];
};

struct RTSS_OSD_SLOT {
    char  szOSD[256];
    char  szOSDOwner[256];
    char  szOSDEx[4096];
    BYTE  buffer[262144];
    char  szOSDEx2[32768];
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

static std::string strip_rtss_tags(const char* input) {
    if (!input) return "";
    std::string out;
    bool in_tag = false;
    for (size_t i = 0; input[i] != '\0'; ++i) {
        if (input[i] == '<') {
            in_tag = true;
        } else if (input[i] == '>') {
            in_tag = false;
        } else if (!in_tag) {
            out += input[i];
        }
    }
    return out;
}

static DWORD embed_graph(
    BYTE* pBuffer,
    DWORD max_buffer_size,
    DWORD& dwOffset,
    const float* data,
    DWORD data_count,
    LONG width,
    LONG height,
    LONG margin,
    float min_val,
    float max_val,
    DWORD flags
) {
    DWORD required_size = sizeof(RTSS_EMBEDDED_OBJECT) + 3 * sizeof(DWORD) + 2 * sizeof(FLOAT) + data_count * sizeof(FLOAT);
    dwOffset = (dwOffset + 3) & ~3; // 4-byte alignment
    if (dwOffset + required_size > max_buffer_size) {
        return 0;
    }

    auto* lpGraph = reinterpret_cast<RTSS_EMBEDDED_OBJECT_GRAPH*>(pBuffer + dwOffset);
    lpGraph->header.dwSignature = 'GR00';
    lpGraph->header.dwSize = required_size;
    lpGraph->header.dwWidth = width;
    lpGraph->header.dwHeight = height;
    lpGraph->header.dwMargin = margin;
    lpGraph->dwFlags = flags;
    lpGraph->fltMin = min_val;
    lpGraph->fltMax = max_val;
    lpGraph->dwDataCount = data_count;

    if (data && data_count > 0) {
        for (DWORD i = 0; i < data_count; ++i) {
            lpGraph->fltData[i] = data[i];
        }
    }

    DWORD result_size = lpGraph->header.dwSize;
    dwOffset += result_size;
    return result_size;
}

bool RTSSBridge::is_rtss_process_running() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"RTSS.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return found;
}

std::wstring RTSSBridge::get_rtss_executable_path() {
    // 1. Query registry HKLM\SOFTWARE\WOW6432Node\Unwinder\RTSS -> InstallPath
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Unwinder\\RTSS", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t path[MAX_PATH] = { 0 };
        DWORD size = sizeof(path);
        if (RegQueryValueExW(hKey, L"InstallPath", nullptr, nullptr, reinterpret_cast<LPBYTE>(path), &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
                return path;
            }
        }
        RegCloseKey(hKey);
    }

    // 2. Standard Program Files candidates
    const wchar_t* candidates[] = {
        L"C:\\Program Files (x86)\\RivaTuner Statistics Server\\RTSS.exe",
        L"C:\\Program Files\\RivaTuner Statistics Server\\RTSS.exe"
    };
    for (const auto* cand : candidates) {
        if (GetFileAttributesW(cand) != INVALID_FILE_ATTRIBUTES) {
            return cand;
        }
    }

    return L"";
}

bool RTSSBridge::is_rtss_installed() {
    return !get_rtss_executable_path().empty();
}

bool RTSSBridge::launch_rtss() {
    std::wstring path = get_rtss_executable_path();
    if (path.empty()) return false;

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"open";
    sei.lpFile = path.c_str();
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_FLAG_NO_UI;

    return ShellExecuteExW(&sei) == TRUE;
}

bool RTSSBridge::kill_rtss() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool terminated = false;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"RTSS.exe") == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    TerminateProcess(hProc, 0);
                    CloseHandle(hProc);
                    terminated = true;
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return terminated;
}

bool RTSSBridge::restart_rtss() {
    kill_rtss();
    Sleep(500);
    return launch_rtss();
}

bool RTSSBridge::install_exclusion_profile() {
    std::wstring rtss_exe = get_rtss_executable_path();
    if (rtss_exe.empty()) return false;

    // Get RTSS root directory
    wchar_t rtss_dir[MAX_PATH] = { 0 };
    wcscpy_s(rtss_dir, rtss_exe.c_str());
    wchar_t* last_slash = wcsrchr(rtss_dir, L'\\');
    if (last_slash) *last_slash = L'\0';

    // Get current process executable name (e.g. "AnyPerfomans.exe")
    wchar_t my_path[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, my_path, MAX_PATH);
    const wchar_t* my_exe_name = wcsrchr(my_path, L'\\');
    if (my_exe_name) my_exe_name++;
    else my_exe_name = L"AnyPerfomans.exe";

    std::string exe_name_ascii = wide_to_utf8(my_exe_name);

    // 1. Direct profile file creation/overwrite if directory is writable
    std::wstring cfg_path = std::wstring(rtss_dir) + L"\\Profiles\\" + my_exe_name + L".cfg";
    HANDLE hFile = CreateFileW(cfg_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        const char cfg_data[] =
            "[Hooking]\n"
            "EnableHooking=0\n"
            "[OSD]\n"
            "EnableOSD=0\n";
        DWORD written = 0;
        WriteFile(hFile, cfg_data, sizeof(cfg_data) - 1, &written, nullptr);
        CloseHandle(hFile);
    }

    // 2. Also invoke official RTSSHooks64.dll API to notify running RTSS service
    std::wstring hooks_dll = std::wstring(rtss_dir) + L"\\RTSSHooks64.dll";
    HMODULE hHooks = LoadLibraryW(hooks_dll.c_str());
    if (hHooks) {
        typedef void (*PFNLOADPROFILE)(LPCSTR);
        typedef void (*PFNSAVEPROFILE)(LPCSTR);
        typedef BOOL (*PFNSETPROFILEPROPERTY)(LPCSTR, LPBYTE, DWORD);
        typedef void (*PFNUPDATEPROFILES)();

        auto pfnLoad = (PFNLOADPROFILE)GetProcAddress(hHooks, "LoadProfile");
        auto pfnSet = (PFNSETPROFILEPROPERTY)GetProcAddress(hHooks, "SetProfileProperty");
        auto pfnSave = (PFNSAVEPROFILE)GetProcAddress(hHooks, "SaveProfile");
        auto pfnUpdate = (PFNUPDATEPROFILES)GetProcAddress(hHooks, "UpdateProfiles");

        if (pfnLoad && pfnSet && pfnSave && pfnUpdate) {
            pfnLoad(exe_name_ascii.c_str());
            DWORD dwZero = 0;
            pfnSet("AppDetectionLevel", (LPBYTE)&dwZero, sizeof(dwZero));
            pfnSet("EnableOSD", (LPBYTE)&dwZero, sizeof(dwZero));
            pfnSet("EnableHooking", (LPBYTE)&dwZero, sizeof(dwZero));
            pfnSave(exe_name_ascii.c_str());
            pfnUpdate();
        }
        FreeLibrary(hHooks);
    }

    return true;
}

RTSSBridge::RTSSBridge() {
    install_exclusion_profile();
    connect();
}

RTSSBridge::~RTSSBridge() {
    clear_osd();
    disconnect();
}

bool RTSSBridge::connect() {
    install_exclusion_profile();

    if (is_connected_) return true;

    h_map_file_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, "RTSSSharedMemoryV2");
    if (!h_map_file_) {
        // If not running but installed, auto-raise RTSS in background
        if (!is_rtss_process_running() && is_rtss_installed()) {
            launch_rtss();
            launched_by_us_ = true;
            for (int i = 0; i < 15; ++i) {
                Sleep(100);
                h_map_file_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, "RTSSSharedMemoryV2");
                if (h_map_file_) break;
            }
        }

        if (!h_map_file_) {
            is_connected_ = false;
            return false;
        }
    }

    map_addr_ = MapViewOfFile(h_map_file_, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!map_addr_) {
        CloseHandle(h_map_file_);
        h_map_file_ = nullptr;
        is_connected_ = false;
        return false;
    }

    auto* pHeader = reinterpret_cast<RTSS_HEADER*>(map_addr_);
    if (!is_rtss_signature_valid(pHeader->dwSignature) || pHeader->dwVersion < 0x00020000) {
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

bool RTSSBridge::write_to_shared_memory(const char* text, const BYTE* graph_buffer, DWORD graph_buffer_size) {
    if (!is_connected_ && !connect()) {
        return false;
    }

    auto* pHeader = reinterpret_cast<RTSS_HEADER*>(map_addr_);
    if (!pHeader || !is_rtss_signature_valid(pHeader->dwSignature)) {
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

    std::string clean_text = strip_rtss_tags(text);
    bool updated = false;

    // Search for slot:
    // Pass 0: check if we already own a slot
    // Pass 1: find unused slot. Following Unwinder's SDK guide, check slots 1..N first (leaving slot 0 for MSI Afterburner / EVGA Precision).
    for (DWORD pass = 0; pass < 2; ++pass) {
        for (DWORD entry_idx = 0; entry_idx < pHeader->dwOSDArrSize; ++entry_idx) {
            DWORD entry = entry_idx;
            if (pass == 1) {
                // Check slots 1 to dwOSDArrSize-1 first, then fallback to slot 0
                entry = (entry_idx + 1) % pHeader->dwOSDArrSize;
            }

            auto* pEntry = reinterpret_cast<RTSS_OSD_SLOT*>(
                reinterpret_cast<LPBYTE>(pHeader) + pHeader->dwOSDArrOffset + entry * pHeader->dwOSDEntrySize
            );

            if (pass == 1 && pEntry->szOSDOwner[0] == '\0') {
                strcpy_s(pEntry->szOSDOwner, sizeof(pEntry->szOSDOwner), "AnyPerfomans");
            }

            if (strcmp(pEntry->szOSDOwner, "AnyPerfomans") == 0) {
                // Acquire busy lock if supported (v2.14+)
                bool acquired = false;
                if (pHeader->dwVersion >= 0x0002000E) {
                    for (int retry = 0; retry < 50; ++retry) {
                        if (!_interlockedbittestandset(&pHeader->dwBusy, 0)) {
                            acquired = true;
                            break;
                        }
                        Sleep(1);
                    }
                } else {
                    acquired = true;
                }

                if (!acquired) {
                    return false;
                }

                // 1. Write extended formatted OSD
                if (pHeader->dwVersion >= 0x00020007) {
                    strncpy_s(pEntry->szOSDEx, sizeof(pEntry->szOSDEx), text, sizeof(pEntry->szOSDEx) - 1);
                }

                // 2. Write embedded graphs buffer
                if (graph_buffer && graph_buffer_size > 0 && pHeader->dwVersion >= 0x0002000C) {
                    memcpy(pEntry->buffer, graph_buffer, min(graph_buffer_size, (DWORD)sizeof(pEntry->buffer)));
                }

                // 3. Write universal fallback OSD (unformatted clean text for legacy renderers)
                strncpy_s(pEntry->szOSD, sizeof(pEntry->szOSD), clean_text.c_str(), sizeof(pEntry->szOSD) - 1);

                if (pHeader->dwVersion >= 0x0002000E) {
                    pHeader->dwBusy = 0;
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
    if (!pHeader || !is_rtss_signature_valid(pHeader->dwSignature)) return;

    for (DWORD entry = 0; entry < pHeader->dwOSDArrSize; ++entry) {
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

    // Maintain history buffers for graphs
    const auto* primary = proc_mon.get_primary_target();
    gpu_history_.push_back(static_cast<float>(gpu.core_usage_percent));
    if (gpu_history_.size() > 64) gpu_history_.erase(gpu_history_.begin());

    cpu_history_.push_back(static_cast<float>(primary && primary->is_alive ? primary->cpu_percent : 0.0));
    if (cpu_history_.size() > 64) cpu_history_.erase(cpu_history_.begin());

    float ram_gb = static_cast<float>(proc_mon.get_system_ram_used()) / (1024.0f * 1024.0f * 1024.0f);
    ram_history_.push_back(ram_gb);
    if (ram_history_.size() > 64) ram_history_.erase(ram_history_.begin());

    std::stringstream ss;
    BYTE graph_buffer[262144] = { 0 };
    DWORD dwBufferOffset = 0;

    // 1. Live Benchmark Recording Badge
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
           << "STUTTERS: " << frames.stutter_count << "<S><C>\n";
    }

    // 2. GPU Telemetry Row & Embedded Graph
    std::string gpu_name = hud.label_gpu.empty() ? "GPU" : hud.label_gpu;
    if (hud.mode_gpu != SensorDisplayMode::Disabled) {
        if (hud.mode_gpu == SensorDisplayMode::TextOnly || hud.mode_gpu == SensorDisplayMode::TextAndGraph) {
            ss << "<C=00E5FF>" << gpu_name << " : <C><C=FFFFFF>";
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
            ss << "<C>\n";
        }
        if (hud.mode_gpu == SensorDisplayMode::GraphOnly || hud.mode_gpu == SensorDisplayMode::TextAndGraph) {
            DWORD graph_offset = dwBufferOffset;
            DWORD dwFlags = (hud.rtss_graph_filled ? RTSS_EMBEDDED_OBJECT_GRAPH_FLAG_FILLED : 0);
            DWORD sz = embed_graph(graph_buffer, sizeof(graph_buffer), dwBufferOffset, gpu_history_.data(), (DWORD)gpu_history_.size(), hud.rtss_graph_width, hud.rtss_graph_height, 1, 0.0f, 100.0f, dwFlags);
            if (sz > 0) {
                char obj_tag[32];
                sprintf_s(obj_tag, "<OBJ=%08X>\n", graph_offset);
                ss << "<C=00E5FF>" << obj_tag << "<C>";
            }
        }
    }

    // 3. CPU Telemetry Row & Embedded Graph
    std::string cpu_name = hud.label_cpu.empty() ? "CPU" : hud.label_cpu;
    if (hud.mode_cpu != SensorDisplayMode::Disabled) {
        if (hud.mode_cpu == SensorDisplayMode::TextOnly || hud.mode_cpu == SensorDisplayMode::TextAndGraph) {
            ss << "<C=5CC8FF>" << cpu_name << " : <C><C=FFFFFF>";
            if (primary && primary->is_alive) {
                ss << std::fixed << std::setprecision(1) << primary->cpu_percent << "% ";
                if (primary->cpu_percent < 0.05) {
                    ss << "[IDLE] ";
                }
                ss << "(" << wide_to_utf8(primary->name) << ")";
            } else {
                ss << "Idle";
            }
            ss << "<C>\n";
        }
        if (hud.mode_cpu == SensorDisplayMode::GraphOnly || hud.mode_cpu == SensorDisplayMode::TextAndGraph) {
            DWORD graph_offset = dwBufferOffset;
            DWORD dwFlags = (hud.rtss_graph_filled ? RTSS_EMBEDDED_OBJECT_GRAPH_FLAG_FILLED : 0);
            DWORD sz = embed_graph(graph_buffer, sizeof(graph_buffer), dwBufferOffset, cpu_history_.data(), (DWORD)cpu_history_.size(), hud.rtss_graph_width, hud.rtss_graph_height, 1, 0.0f, 100.0f, dwFlags);
            if (sz > 0) {
                char obj_tag[32];
                sprintf_s(obj_tag, "<OBJ=%08X>\n", graph_offset);
                ss << "<C=5CC8FF>" << obj_tag << "<C>";
            }
        }
    }

    // 4. System RAM Row
    std::string ram_name = hud.label_ram.empty() ? "RAM" : hud.label_ram;
    if (hud.mode_ram != SensorDisplayMode::Disabled) {
        float ram_used_gb = static_cast<float>(proc_mon.get_system_ram_used()) / (1024.0f * 1024.0f * 1024.0f);
        float ram_tot_gb = static_cast<float>(proc_mon.get_system_ram_total()) / (1024.0f * 1024.0f * 1024.0f);
        if (hud.mode_ram == SensorDisplayMode::TextOnly || hud.mode_ram == SensorDisplayMode::TextAndGraph) {
            if (ram_tot_gb > 0.0f) {
                ss << "<C=FFB300>" << ram_name << " : <C><C=FFFFFF>"
                   << std::fixed << std::setprecision(1) << ram_used_gb << " / "
                   << std::fixed << std::setprecision(1) << ram_tot_gb << " GB";
                if (hud.show_pagefile) {
                    float page_used_gb = static_cast<float>(proc_mon.get_system_commit_used()) / (1024.0f * 1024.0f * 1024.0f);
                    ss << " | Page: " << std::fixed << std::setprecision(1) << page_used_gb << " GB";
                }
                ss << "<C>\n";
            }
        }
        if (hud.mode_ram == SensorDisplayMode::GraphOnly || hud.mode_ram == SensorDisplayMode::TextAndGraph) {
            DWORD graph_offset = dwBufferOffset;
            DWORD dwFlags = (hud.rtss_graph_filled ? RTSS_EMBEDDED_OBJECT_GRAPH_FLAG_FILLED : 0);
            DWORD sz = embed_graph(graph_buffer, sizeof(graph_buffer), dwBufferOffset, ram_history_.data(), (DWORD)ram_history_.size(), hud.rtss_graph_width, hud.rtss_graph_height, 1, 0.0f, ram_tot_gb > 0 ? ram_tot_gb : 32.0f, dwFlags);
            if (sz > 0) {
                char obj_tag[32];
                sprintf_s(obj_tag, "<OBJ=%08X>\n", graph_offset);
                ss << "<C=FFB300>" << obj_tag << "<C>";
            }
        }
    }

    // 5. FPS, Percentiles & Stutters Row
    std::string fps_name = hud.label_fps.empty() ? "FPS" : hud.label_fps;
    std::string ft_name = hud.label_frametime.empty() ? "FT" : hud.label_frametime;
    if (hud.mode_fps != SensorDisplayMode::Disabled) {
        ss << "<C=00E676>" << fps_name << " : <C><C=FFFFFF>"
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

        if (hud.mode_frametime == SensorDisplayMode::TextOnly || hud.mode_frametime == SensorDisplayMode::TextAndGraph) {
            ss << "  " << ft_name << ": " << std::fixed << std::setprecision(1) << frames.frametime_ms << "ms";
        }

        if (hud.show_stutters && frames.stutter_count > 0) {
            ss << "  <C=FF1744>[HITCH: " << frames.stutter_count << "]<C>";
        }
        ss << "<C>\n";
    }

    // 6. FRAMETIME HARDWARE LINE GRAPH (MSI Afterburner Style)
    if (hud.mode_frametime == SensorDisplayMode::GraphOnly || hud.mode_frametime == SensorDisplayMode::TextAndGraph) {
        DWORD graph_offset = dwBufferOffset;
        DWORD dwFlags = (hud.rtss_graph_filled ? RTSS_EMBEDDED_OBJECT_GRAPH_FLAG_FILLED : 0) | RTSS_EMBEDDED_OBJECT_GRAPH_FLAG_FRAMETIME;
        float max_ft = (hud.rtss_frametime_max_ms > 0.0f) ? (hud.rtss_frametime_max_ms * 1000.0f) : 50000.0f; // in microseconds
        DWORD sz = embed_graph(graph_buffer, sizeof(graph_buffer), dwBufferOffset, nullptr, 0, hud.rtss_graph_width, hud.rtss_graph_height, 1, 0.0f, max_ft, dwFlags);
        if (sz > 0) {
            char obj_tag[32];
            sprintf_s(obj_tag, "<OBJ=%08X>\n", graph_offset);
            ss << "<C=00E676>" << obj_tag << "<C>";
        }
    }

    // 7. Monitored Secondary Background Applications (AnyPerfomans exclusive!)
    if (hud.show_secondary_apps) {
        const auto& targets = proc_mon.get_monitored_targets();
        bool has_secondary = false;
        for (const auto& t : targets) {
            if (t.is_primary) continue;
            has_secondary = true;
            float ram_mb = static_cast<float>(t.private_working_set) / (1024.0f * 1024.0f);
            std::string short_name = wide_to_utf8(t.name);
            if (short_name.length() > 10) short_name = short_name.substr(0, 10);

            ss << "<C=FF5577>" << short_name << "<C> : <C=FFFFFF>"
               << std::fixed << std::setprecision(1) << t.cpu_percent << "% | "
               << static_cast<int>(ram_mb) << " MB<C>\n";
        }

        if (has_secondary) {
            double total_sec_cpu = proc_mon.get_total_secondary_cpu();
            float total_sec_ram = static_cast<float>(proc_mon.get_total_secondary_ram()) / (1024.0f * 1024.0f);
            ss << "<C=FF9100>App Tax : <C><C=FF3D00>+"
               << std::fixed << std::setprecision(1) << total_sec_cpu << "% CPU | "
               << static_cast<int>(total_sec_ram) << " MB RAM<C>\n";
        }
    }

    return write_to_shared_memory(ss.str().c_str(), graph_buffer, dwBufferOffset);
}

} // namespace anyperf
