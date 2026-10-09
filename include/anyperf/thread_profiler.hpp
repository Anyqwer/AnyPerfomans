#pragma once

#include "anyperf/types.hpp"
#include <vector>
#include <unordered_map>
#include <memory>

namespace anyperf {

class ThreadProfiler {
public:
    ThreadProfiler();
    ~ThreadProfiler() = default;

    // Refresh threads for the target PID and update their cycle consumption
    void update(DWORD target_pid);

    // Get list of threads sorted by CPU cycle delta (heaviest first)
    const std::vector<ThreadMetrics>& get_sorted_threads() const { return cached_threads_; }

    size_t get_thread_count() const { return cached_threads_.size(); }

private:
    uintptr_t query_thread_start_address(HANDLE hThread);
    std::wstring resolve_module_name_for_address(HANDLE hProcess, uintptr_t address);

    DWORD current_pid_ = 0;
    std::unordered_map<DWORD, uint64_t> prev_thread_cycles_;
    std::vector<ThreadMetrics> cached_threads_;

    // NTDLL function pointer
    using pfnNtQueryInformationThread = LONG(NTAPI*)(
        HANDLE ThreadHandle,
        ULONG ThreadInformationClass,
        PVOID ThreadInformation,
        ULONG ThreadInformationLength,
        PULONG ReturnLength
    );
    pfnNtQueryInformationThread pfn_nt_query_thread_ = nullptr;
};

} // namespace anyperf
