#include "anyperf/process_monitor.hpp"
#include <tlhelp32.h>
#include <psapi.h>
#include <iostream>
#include <algorithm>

namespace anyperf {

static uint64_t filetime_to_uint64(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

ProcessMonitor::ProcessMonitor() {
    GetSystemTimes(&prev_idle_time_, &prev_kernel_time_, &prev_user_time_);
    last_sample_time_ = std::chrono::steady_clock::now();
}

ProcessMonitor::~ProcessMonitor() {
    if (h_game_) CloseHandle(h_game_);
    if (h_aux_) CloseHandle(h_aux_);
}

HANDLE ProcessMonitor::open_process_safe(DWORD pid) {
    if (pid == 0) return nullptr;
    // PROCESS_QUERY_LIMITED_INFORMATION is non-invasive and permitted for non-elevated callers
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) {
        // Fallback with just QUERY_LIMITED_INFORMATION
        h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    }
    return h;
}

std::vector<ProcessInfo> ProcessMonitor::enumerate_processes() {
    std::vector<ProcessInfo> list;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return list;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID > 0) {
                ProcessInfo info;
                info.pid = entry.th32ProcessID;
                info.name = entry.szExeFile;
                list.push_back(info);
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);

    std::sort(list.begin(), list.end(), [](const ProcessInfo& a, const ProcessInfo& b) {
        return a.name < b.name;
    });

    return list;
}

void ProcessMonitor::set_game_pid(DWORD pid) {
    if (game_pid_ == pid) return;
    if (h_game_) {
        CloseHandle(h_game_);
        h_game_ = nullptr;
    }
    game_pid_ = pid;
    game_metrics_ = ProcessMetrics{};
    game_metrics_.pid = pid;
    if (pid != 0) {
        h_game_ = open_process_safe(pid);
        game_metrics_.is_alive = (h_game_ != nullptr);
    }
}

void ProcessMonitor::set_aux_pid(DWORD pid) {
    if (aux_pid_ == pid) return;
    if (h_aux_) {
        CloseHandle(h_aux_);
        h_aux_ = nullptr;
    }
    aux_pid_ = pid;
    aux_metrics_ = ProcessMetrics{};
    aux_metrics_.pid = pid;
    if (pid != 0) {
        h_aux_ = open_process_safe(pid);
        aux_metrics_.is_alive = (h_aux_ != nullptr);
    }
}

bool ProcessMonitor::auto_detect_game() {
    auto procs = enumerate_processes();
    for (const auto& p : procs) {
        if (_wcsicmp(p.name.c_str(), L"cs2.exe") == 0) {
            set_game_pid(p.pid);
            game_metrics_.name = p.name;
            return true;
        }
    }
    return false;
}

void ProcessMonitor::sample_process(DWORD pid, HANDLE hProcess, ProcessMetrics& metrics, uint64_t system_delta_time_100ns) {
    if (!hProcess || pid == 0) {
        metrics.is_alive = false;
        metrics.cpu_percent = 0.0;
        metrics.commit_charge = 0;
        metrics.working_set = 0;
        metrics.private_working_set = 0;
        return;
    }

    DWORD exitCode = 0;
    if (GetExitCodeProcess(hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
        metrics.is_alive = false;
        return;
    }
    metrics.is_alive = true;

    // 1. Process CPU Times
    FILETIME creationTime{}, exitTime{}, kernelTime{}, userTime{};
    if (GetProcessTimes(hProcess, &creationTime, &exitTime, &kernelTime, &userTime)) {
        uint64_t kTime = filetime_to_uint64(kernelTime);
        uint64_t uTime = filetime_to_uint64(userTime);

        if (prev_proc_kernel_.find(pid) != prev_proc_kernel_.end()) {
            uint64_t prevK = filetime_to_uint64(prev_proc_kernel_[pid]);
            uint64_t prevU = filetime_to_uint64(prev_proc_user_[pid]);

            uint64_t deltaK = (kTime >= prevK) ? (kTime - prevK) : 0;
            uint64_t deltaU = (uTime >= prevU) ? (uTime - prevU) : 0;
            uint64_t totalProcTime = deltaK + deltaU;

            if (system_delta_time_100ns > 0) {
                // Percentage across all CPU cores
                metrics.cpu_percent = (static_cast<double>(totalProcTime) / static_cast<double>(system_delta_time_100ns)) * 100.0;
            }
        }

        prev_proc_kernel_[pid] = kernelTime;
        prev_proc_user_[pid] = userTime;
    }

    // 2. High-precision CPU Cycles
    ULONG64 cycles = 0;
    if (QueryProcessCycleTime(hProcess, &cycles)) {
        if (prev_proc_cycles_.find(pid) != prev_proc_cycles_.end()) {
            uint64_t prevC = prev_proc_cycles_[pid];
            metrics.cycle_delta = (cycles >= prevC) ? (cycles - prevC) : 0;
        }
        prev_proc_cycles_[pid] = cycles;
        metrics.last_cycles = cycles;
    }

    // 3. Memory Metrics (PSAPI)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(hProcess, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        metrics.working_set = pmc.WorkingSetSize;
        metrics.commit_charge = pmc.PrivateUsage; // Private commit bytes
        metrics.private_working_set = pmc.PrivateUsage;
    }

    // 4. Handles count
    DWORD handleCount = 0;
    if (GetProcessHandleCount(hProcess, &handleCount)) {
        metrics.handle_count = handleCount;
    }
}

void ProcessMonitor::update() {
    FILETIME idleTime{}, kernelTime{}, userTime{};
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        return;
    }

    uint64_t prevSysK = filetime_to_uint64(prev_kernel_time_);
    uint64_t prevSysU = filetime_to_uint64(prev_user_time_);
    uint64_t curSysK  = filetime_to_uint64(kernelTime);
    uint64_t curSysU  = filetime_to_uint64(userTime);

    uint64_t deltaSysK = (curSysK >= prevSysK) ? (curSysK - prevSysK) : 0;
    uint64_t deltaSysU = (curSysU >= prevSysU) ? (curSysU - prevSysU) : 0;
    uint64_t totalSysTime = deltaSysK + deltaSysU;

    prev_idle_time_ = idleTime;
    prev_kernel_time_ = kernelTime;
    prev_user_time_ = userTime;

    // Sample Game
    if (game_pid_ != 0) {
        if (!h_game_) h_game_ = open_process_safe(game_pid_);
        sample_process(game_pid_, h_game_, game_metrics_, totalSysTime);
    }

    // Sample Aux
    if (aux_pid_ != 0) {
        if (!h_aux_) h_aux_ = open_process_safe(aux_pid_);
        sample_process(aux_pid_, h_aux_, aux_metrics_, totalSysTime);
    }
}

} // namespace anyperf
