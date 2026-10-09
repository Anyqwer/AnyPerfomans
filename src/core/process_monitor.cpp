#include "anyperf/process_monitor.hpp"
#include <tlhelp32.h>
#include <psapi.h>
#include <algorithm>
#include <iostream>

namespace anyperf {

static uint64_t filetime_to_uint64(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

ProcessColor ProcessMonitor::assign_palette_color(size_t index) {
    static const ProcessColor palette[] = {
        { 0.26f, 0.78f, 0.95f, 1.0f }, // Vibrant Cyan (Default Primary)
        { 0.95f, 0.35f, 0.45f, 1.0f }, // Coral Red
        { 0.95f, 0.75f, 0.20f, 1.0f }, // Amber Gold
        { 0.75f, 0.45f, 0.95f, 1.0f }, // Purple Neon
        { 0.20f, 0.85f, 0.55f, 1.0f }, // Emerald Green
        { 1.00f, 0.55f, 0.20f, 1.0f }, // Neon Orange
        { 0.95f, 0.30f, 0.70f, 1.0f }, // Hot Pink
        { 0.40f, 0.65f, 1.00f, 1.0f }  // Sky Blue
    };
    constexpr size_t count = sizeof(palette) / sizeof(palette[0]);
    return palette[index % count];
}

ProcessMonitor::ProcessMonitor() {
    GetSystemTimes(&prev_idle_time_, &prev_kernel_time_, &prev_user_time_);
    last_sample_time_ = std::chrono::steady_clock::now();
}

ProcessMonitor::~ProcessMonitor() {
    clear_targets();
}

HANDLE ProcessMonitor::open_process_safe(DWORD pid) {
    if (pid == 0) return nullptr;
    // PROCESS_QUERY_LIMITED_INFORMATION is non-invasive and safe
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) {
        h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    }
    return h;
}

std::wstring ProcessMonitor::get_process_name_by_pid(DWORD pid) {
    if (pid == 0) return L"";
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return L"";

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::wstring name;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == pid) {
                name = entry.szExeFile;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return name;
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
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    return list;
}

bool ProcessMonitor::is_monitored(DWORD pid) const {
    for (const auto& target : monitored_targets_) {
        if (target.pid == pid) return true;
    }
    return false;
}

bool ProcessMonitor::add_target(DWORD pid, const std::wstring& name, bool is_primary) {
    if (pid == 0 || is_monitored(pid)) return false;

    ProcessMetrics metrics{};
    metrics.pid = pid;
    metrics.name = name.empty() ? get_process_name_by_pid(pid) : name;
    metrics.is_primary = is_primary;
    metrics.color = assign_palette_color(monitored_targets_.size());

    HANDLE h = open_process_safe(pid);
    metrics.is_alive = (h != nullptr);
    if (h) {
        open_handles_[pid] = h;
    }

    // If marked primary, ensure no other process is marked primary
    if (is_primary) {
        for (auto& t : monitored_targets_) {
            t.is_primary = false;
        }
    } else if (monitored_targets_.empty()) {
        // First target defaults to primary
        metrics.is_primary = true;
    }

    monitored_targets_.push_back(metrics);
    return true;
}

bool ProcessMonitor::remove_target(DWORD pid) {
    auto it = std::find_if(monitored_targets_.begin(), monitored_targets_.end(), [pid](const ProcessMetrics& m) {
        return m.pid == pid;
    });

    if (it == monitored_targets_.end()) return false;

    bool was_primary = it->is_primary;
    monitored_targets_.erase(it);

    auto hIt = open_handles_.find(pid);
    if (hIt != open_handles_.end()) {
        if (hIt->second) CloseHandle(hIt->second);
        open_handles_.erase(hIt);
    }

    prev_proc_kernel_.erase(pid);
    prev_proc_user_.erase(pid);
    prev_proc_cycles_.erase(pid);

    // If we removed primary and still have targets, designate first target as primary
    if (was_primary && !monitored_targets_.empty()) {
        monitored_targets_.front().is_primary = true;
    }

    return true;
}

void ProcessMonitor::clear_targets() {
    for (auto& pair : open_handles_) {
        if (pair.second) CloseHandle(pair.second);
    }
    open_handles_.clear();
    monitored_targets_.clear();
    prev_proc_kernel_.clear();
    prev_proc_user_.clear();
    prev_proc_cycles_.clear();
}

void ProcessMonitor::set_primary_target(DWORD pid) {
    for (auto& t : monitored_targets_) {
        t.is_primary = (t.pid == pid);
    }
}

const ProcessMetrics* ProcessMonitor::get_primary_target() const {
    for (const auto& t : monitored_targets_) {
        if (t.is_primary) return &t;
    }
    if (!monitored_targets_.empty()) {
        return &monitored_targets_.front();
    }
    return nullptr;
}

const ProcessMetrics* ProcessMonitor::get_target(DWORD pid) const {
    for (const auto& t : monitored_targets_) {
        if (t.pid == pid) return &t;
    }
    return nullptr;
}

void ProcessMonitor::sample_process(HANDLE hProcess, ProcessMetrics& metrics, uint64_t system_delta_time_100ns) {
    DWORD pid = metrics.pid;
    if (!hProcess || pid == 0) {
        metrics.is_alive = false;
        metrics.cpu_percent = 0.0;
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
        metrics.commit_charge = pmc.PrivateUsage;
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

    // Sample all monitored targets
    for (auto& target : monitored_targets_) {
        HANDLE h = nullptr;
        auto it = open_handles_.find(target.pid);
        if (it != open_handles_.end()) {
            h = it->second;
        } else {
            h = open_process_safe(target.pid);
            if (h) open_handles_[target.pid] = h;
        }

        sample_process(h, target, totalSysTime);
    }
}

double ProcessMonitor::get_total_secondary_cpu() const {
    double total = 0.0;
    for (const auto& t : monitored_targets_) {
        if (!t.is_primary) {
            total += t.cpu_percent;
        }
    }
    return total;
}

size_t ProcessMonitor::get_total_secondary_ram() const {
    size_t total = 0;
    for (const auto& t : monitored_targets_) {
        if (!t.is_primary) {
            total += t.private_working_set;
        }
    }
    return total;
}

} // namespace anyperf
