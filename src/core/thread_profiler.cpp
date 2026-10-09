#include "anyperf/thread_profiler.hpp"
#include <tlhelp32.h>
#include <psapi.h>
#include <algorithm>
#include <iostream>

#ifndef ThreadQuerySetWin32StartAddress
#define ThreadQuerySetWin32StartAddress 9
#endif

namespace anyperf {

ThreadProfiler::ThreadProfiler() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        pfn_nt_query_thread_ = reinterpret_cast<pfnNtQueryInformationThread>(
            GetProcAddress(hNtdll, "NtQueryInformationThread")
        );
    }
}

uintptr_t ThreadProfiler::query_thread_start_address(HANDLE hThread) {
    if (!pfn_nt_query_thread_ || !hThread) return 0;
    uintptr_t startAddr = 0;
    ULONG returnLength = 0;
    LONG status = pfn_nt_query_thread_(
        hThread,
        ThreadQuerySetWin32StartAddress,
        &startAddr,
        sizeof(startAddr),
        &returnLength
    );
    if (status >= 0) { // NT_SUCCESS
        return startAddr;
    }
    return 0;
}

std::wstring ThreadProfiler::resolve_module_name_for_address(HANDLE hProcess, uintptr_t address) {
    if (!hProcess || address == 0) return L"Unknown";

    HMODULE hMods[1024];
    DWORD cbNeeded = 0;
    if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
        DWORD count = cbNeeded / sizeof(HMODULE);
        for (DWORD i = 0; i < count; i++) {
            MODULEINFO mi{};
            if (GetModuleInformation(hProcess, hMods[i], &mi, sizeof(mi))) {
                uintptr_t base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
                uintptr_t end  = base + mi.SizeOfImage;
                if (address >= base && address < end) {
                    wchar_t modPath[MAX_PATH];
                    if (GetModuleBaseNameW(hProcess, hMods[i], modPath, MAX_PATH)) {
                        return modPath;
                    }
                }
            }
        }
    }
    return L"[Unknown/JIT]";
}

void ThreadProfiler::update(DWORD target_pid) {
    if (target_pid == 0) {
        cached_threads_.clear();
        prev_thread_cycles_.clear();
        current_pid_ = 0;
        return;
    }

    if (current_pid_ != target_pid) {
        prev_thread_cycles_.clear();
        current_pid_ = target_pid;
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, target_pid);
    if (!hProcess) {
        hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target_pid);
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        if (hProcess) CloseHandle(hProcess);
        return;
    }

    THREADENTRY32 te{};
    te.dwSize = sizeof(te);

    std::vector<ThreadMetrics> current_threads;
    uint64_t total_delta_all_threads = 0;

    if (Thread32First(snapshot, &te)) {
        do {
            if (te.th32OwnerProcessID == target_pid) {
                ThreadMetrics tm{};
                tm.tid = te.th32ThreadID;

                HANDLE hThread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
                if (hThread) {
                    ULONG64 cycles = 0;
                    if (QueryThreadCycleTime(hThread, &cycles)) {
                        tm.last_cycles = cycles;
                        if (prev_thread_cycles_.find(tm.tid) != prev_thread_cycles_.end()) {
                            uint64_t prev = prev_thread_cycles_[tm.tid];
                            tm.cycle_delta = (cycles >= prev) ? (cycles - prev) : 0;
                        }
                        prev_thread_cycles_[tm.tid] = cycles;
                        total_delta_all_threads += tm.cycle_delta;
                    }

                    tm.start_address = query_thread_start_address(hThread);
                    if (hProcess && tm.start_address != 0) {
                        tm.module_name = resolve_module_name_for_address(hProcess, tm.start_address);
                    } else {
                        tm.module_name = L"N/A";
                    }

                    CloseHandle(hThread);
                }

                current_threads.push_back(tm);
            }
        } while (Thread32Next(snapshot, &te));
    }

    CloseHandle(snapshot);
    if (hProcess) CloseHandle(hProcess);

    // Compute relative thread CPU % within the process
    for (auto& tm : current_threads) {
        if (total_delta_all_threads > 0) {
            tm.cpu_percent = (static_cast<double>(tm.cycle_delta) / static_cast<double>(total_delta_all_threads)) * 100.0;
        } else {
            tm.cpu_percent = 0.0;
        }
    }

    // Sort descending by cycle_delta (most CPU consuming threads at the top)
    std::sort(current_threads.begin(), current_threads.end(), [](const ThreadMetrics& a, const ThreadMetrics& b) {
        return a.cycle_delta > b.cycle_delta;
    });

    cached_threads_ = std::move(current_threads);
}

} // namespace anyperf
