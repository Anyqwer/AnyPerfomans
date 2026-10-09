#pragma once

#include "anyperf/types.hpp"
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <cstdint>
#include <atomic>
#include <thread>
#include <mutex>
#include <deque>

namespace anyperf {

enum class EtwState {
    Uninitialized,
    Running,
    AccessDenied,   // Requires Administrator or Performance Log Users group
    FailedToStart,
    Stopped
};

class EtwTracker {
public:
    EtwTracker();
    ~EtwTracker();

    // Start ETW trace session targeting a specific PID for DXGI Present tracking
    bool start(DWORD target_pid);
    void stop();
    void set_target_pid(DWORD pid);

    EtwState get_state() const { return state_; }
    bool is_active() const { return state_ == EtwState::Running; }
    DWORD get_target_pid() const { return target_pid_.load(); }

    // Retrieve atomic snapshot of latest frame statistics
    FrameStats get_frame_stats() const;

    // Helper: Restart application with Administrator privileges
    static bool relaunch_as_admin();

private:
    static VOID WINAPI EventRecordCallback(PEVENT_RECORD pEventRecord);
    void on_present(uint64_t qpc_timestamp, DWORD pid);
    void trace_thread_proc();

    std::atomic<EtwState> state_{ EtwState::Uninitialized };
    std::atomic<DWORD> target_pid_{ 0 };
    std::atomic<bool> stop_requested_{ false };

    TRACEHANDLE session_handle_ = 0;
    TRACEHANDLE trace_handle_ = 0;
    std::thread worker_thread_;

    // Performance timer
    uint64_t qpc_freq_ = 1;
    uint64_t last_present_qpc_ = 0;

    // Rolling statistics (guarded)
    mutable std::mutex stats_mutex_;
    FrameStats current_stats_{};
    std::deque<double> recent_frametimes_; // Last 300 frametimes
    uint64_t total_captured_frames_ = 0;
};

} // namespace anyperf
