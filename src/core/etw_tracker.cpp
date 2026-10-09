#include "anyperf/etw_tracker.hpp"
#include <windows.h>
#include <shellapi.h>
#include <numeric>
#include <algorithm>
#include <iostream>

#include <evntrace.h>
#include <evntcons.h>

// Provider GUID: Microsoft-Windows-DXGI {CA11C036-0102-4A2D-A6AD-F03CFED5D3C9}
static const GUID DXGI_PROVIDER_GUID = {
    0xca11c036, 0x102, 0x4a2d, { 0xa6, 0xad, 0xf0, 0x3c, 0xfe, 0xd5, 0xd3, 0xc9 }
};

// Provider GUID: Microsoft-Windows-DxgKrnl {80E80400-9270-46EE-9829-B9560C607260}
static const GUID DXGKRNL_PROVIDER_GUID = {
    0x80e80400, 0x9270, 0x46ee, { 0x98, 0x29, 0xb9, 0x56, 0xc, 0x60, 0x72, 0x60 }
};

namespace anyperf {

static const wchar_t* K_ETW_SESSION_NAME = L"AnyPerfomansETWSession";
static EtwTracker* g_etw_tracker_instance = nullptr;

EtwTracker::EtwTracker() {
    g_etw_tracker_instance = this;
    LARGE_INTEGER li;
    if (QueryPerformanceFrequency(&li)) {
        qpc_freq_ = li.QuadPart;
    }
}

EtwTracker::~EtwTracker() {
    stop();
    g_etw_tracker_instance = nullptr;
}

void EtwTracker::set_target_pid(DWORD pid) {
    target_pid_.store(pid);
    std::lock_guard<std::mutex> lock(stats_mutex_);
    recent_frametimes_.clear();
    last_present_qpc_ = 0;
    current_stats_ = FrameStats{};
}

void WINAPI EtwTracker::EventRecordCallback(PEVENT_RECORD pEventRecord) {
    if (!pEventRecord || !g_etw_tracker_instance) return;

    DWORD target = g_etw_tracker_instance->get_target_pid();
    if (target == 0) return;

    DWORD eventPid = pEventRecord->EventHeader.ProcessId;
    if (eventPid == target) {
        // Opcode 1 or event id 42 represents DXGI Present
        UCHAR opcode = pEventRecord->EventHeader.EventDescriptor.Opcode;
        USHORT id = pEventRecord->EventHeader.EventDescriptor.Id;

        if (opcode == 1 || id == 42 || opcode == 0) {
            uint64_t qpc = pEventRecord->EventHeader.TimeStamp.QuadPart;
            g_etw_tracker_instance->on_present(qpc, eventPid);
        }
    }
}

void EtwTracker::on_present(uint64_t qpc_timestamp, DWORD /*pid*/) {
    if (qpc_freq_ == 0) return;

    if (last_present_qpc_ == 0) {
        last_present_qpc_ = qpc_timestamp;
        return;
    }

    if (qpc_timestamp <= last_present_qpc_) {
        return;
    }

    double delta_ms = static_cast<double>(qpc_timestamp - last_present_qpc_) * 1000.0 / static_cast<double>(qpc_freq_);
    last_present_qpc_ = qpc_timestamp;

    // Filter out aberrant delta timestamps (e.g. paused rendering or system sleep)
    if (delta_ms < 0.1 || delta_ms > 1000.0) {
        return;
    }

    std::lock_guard<std::mutex> lock(stats_mutex_);
    total_captured_frames_++;
    current_stats_.frametime_ms = delta_ms;
    current_stats_.current_fps = 1000.0 / delta_ms;
    current_stats_.total_frames = total_captured_frames_;

    recent_frametimes_.push_back(delta_ms);
    if (recent_frametimes_.size() > 300) {
        recent_frametimes_.pop_front();
    }

    // Compute rolling average FPS
    double sum = std::accumulate(recent_frametimes_.begin(), recent_frametimes_.end(), 0.0);
    current_stats_.avg_fps = 1000.0 / (sum / recent_frametimes_.size());

    // Compute 1% Low and 0.1% Low
    std::vector<double> sorted_ft(recent_frametimes_.begin(), recent_frametimes_.end());
    std::sort(sorted_ft.begin(), sorted_ft.end());

    size_t idx_1pct = static_cast<size_t>(sorted_ft.size() * 0.99);
    if (idx_1pct < sorted_ft.size()) {
        current_stats_.one_percent_low = 1000.0 / sorted_ft[idx_1pct];
    }

    size_t idx_01pct = static_cast<size_t>(sorted_ft.size() * 0.999);
    if (idx_01pct < sorted_ft.size()) {
        current_stats_.zero_one_percent_low = 1000.0 / sorted_ft[idx_01pct];
    }
}

void EtwTracker::trace_thread_proc() {
    if (trace_handle_ != 0 && trace_handle_ != INVALID_PROCESSTRACE_HANDLE) {
        ProcessTrace(&trace_handle_, 1, nullptr, nullptr);
    }
}

bool EtwTracker::start(DWORD target_pid) {
    stop();
    target_pid_.store(target_pid);
    stop_requested_.store(false);

    ULONG bufferSize = sizeof(EVENT_TRACE_PROPERTIES) + 1024;
    std::vector<uint8_t> propBuffer(bufferSize, 0);
    PEVENT_TRACE_PROPERTIES pProps = reinterpret_cast<PEVENT_TRACE_PROPERTIES>(propBuffer.data());

    pProps->Wnode.BufferSize = bufferSize;
    pProps->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    pProps->Wnode.ClientContext = 1; // QueryPerformanceCounter clock
    pProps->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    pProps->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    // Stop existing stale session if present
    ControlTraceW(0, K_ETW_SESSION_NAME, pProps, EVENT_TRACE_CONTROL_STOP);

    // Re-initialize properties
    std::memset(propBuffer.data(), 0, propBuffer.size());
    pProps->Wnode.BufferSize = bufferSize;
    pProps->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    pProps->Wnode.ClientContext = 1;
    pProps->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    pProps->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    ULONG status = StartTraceW(&session_handle_, K_ETW_SESSION_NAME, pProps);
    if (status == ERROR_ACCESS_DENIED) {
        state_.store(EtwState::AccessDenied);
        return false;
    }
    if (status != ERROR_SUCCESS) {
        state_.store(EtwState::FailedToStart);
        return false;
    }

    // Enable Microsoft-Windows-DXGI Provider
    status = EnableTraceEx2(
        session_handle_,
        &DXGI_PROVIDER_GUID,
        EVENT_CONTROL_CODE_ENABLE_PROVIDER,
        TRACE_LEVEL_INFORMATION,
        0, 0, 0, nullptr
    );

    // Open Real-Time Trace Consumer
    EVENT_TRACE_LOGFILEW logFile{};
    logFile.LoggerName = const_cast<LPWSTR>(K_ETW_SESSION_NAME);
    logFile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logFile.EventRecordCallback = &EtwTracker::EventRecordCallback;

    trace_handle_ = OpenTraceW(&logFile);
    if (trace_handle_ == INVALID_PROCESSTRACE_HANDLE) {
        stop();
        state_.store(EtwState::FailedToStart);
        return false;
    }

    state_.store(EtwState::Running);
    worker_thread_ = std::thread(&EtwTracker::trace_thread_proc, this);
    return true;
}

void EtwTracker::stop() {
    stop_requested_.store(true);

    if (trace_handle_ != 0 && trace_handle_ != INVALID_PROCESSTRACE_HANDLE) {
        CloseTrace(trace_handle_);
        trace_handle_ = 0;
    }

    if (session_handle_ != 0) {
        ULONG bufferSize = sizeof(EVENT_TRACE_PROPERTIES) + 1024;
        std::vector<uint8_t> propBuffer(bufferSize, 0);
        PEVENT_TRACE_PROPERTIES pProps = reinterpret_cast<PEVENT_TRACE_PROPERTIES>(propBuffer.data());
        pProps->Wnode.BufferSize = bufferSize;
        pProps->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

        ControlTraceW(session_handle_, K_ETW_SESSION_NAME, pProps, EVENT_TRACE_CONTROL_STOP);
        session_handle_ = 0;
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    state_.store(EtwState::Stopped);
}

FrameStats EtwTracker::get_frame_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return current_stats_;
}

bool EtwTracker::relaunch_as_admin() {
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return false;

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas"; // Requests UAC elevation
    sei.lpFile = exePath;
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei)) {
        ExitProcess(0);
    }
    return false;
}

} // namespace anyperf
