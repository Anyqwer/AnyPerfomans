#include "anyperf/cpu_monitor.hpp"
#include <comdef.h>
#include <wbemidl.h>
#include <iostream>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace anyperf {

CpuMonitor::CpuMonitor() {
    init_cpu_info();

    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        pfn_NtQuerySystemInformation_ = reinterpret_cast<pfnNtQuerySystemInformation>(
            GetProcAddress(hNtdll, "NtQuerySystemInformation")
        );
    }

    worker_running_ = true;
    wmi_worker_ = std::thread(&CpuMonitor::wmi_worker_loop, this);
    update();
}

CpuMonitor::~CpuMonitor() {
    worker_running_ = false;
    if (wmi_worker_.joinable()) {
        wmi_worker_.join();
    }
}

void CpuMonitor::init_cpu_info() {
    SYSTEM_INFO sysInfo{};
    GetSystemInfo(&sysInfo);
    metrics_.logical_core_count = sysInfo.dwNumberOfProcessors;
    metrics_.per_core_usage.resize(metrics_.logical_core_count, 0.0);
    prev_core_perf_.resize(metrics_.logical_core_count);

    // Read CPU Model Name from Registry
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t nameBuf[256] = { 0 };
        DWORD bufSize = sizeof(nameBuf);
        if (RegQueryValueExW(hKey, L"ProcessorNameString", nullptr, nullptr, reinterpret_cast<LPBYTE>(nameBuf), &bufSize) == ERROR_SUCCESS) {
            metrics_.cpu_model_name = nameBuf;
        }
        RegCloseKey(hKey);
    }

    if (metrics_.cpu_model_name.empty()) {
        metrics_.cpu_model_name = L"Generic x64 CPU (" + std::to_wstring(metrics_.logical_core_count) + L" Cores)";
    }
}

void CpuMonitor::update_per_core_usage() {
    if (!pfn_NtQuerySystemInformation_ || metrics_.logical_core_count == 0) return;

    ULONG bufSize = static_cast<ULONG>(metrics_.logical_core_count * sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION));
    std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> current_perf(metrics_.logical_core_count);
    ULONG returnLength = 0;

    NTSTATUS status = pfn_NtQuerySystemInformation_(
        8, // SystemProcessorPerformanceInformation
        current_perf.data(),
        bufSize,
        &returnLength
    );

    if (status != 0) return;

    if (!has_initial_sample_) {
        prev_core_perf_ = current_perf;
        has_initial_sample_ = true;
        return;
    }

    double max_usage = 0.0;
    int max_idx = 0;
    double sum_usage = 0.0;

    for (uint32_t i = 0; i < metrics_.logical_core_count; ++i) {
        ULONGLONG idleDelta = current_perf[i].IdleTime.QuadPart - prev_core_perf_[i].IdleTime.QuadPart;
        ULONGLONG kernelDelta = current_perf[i].KernelTime.QuadPart - prev_core_perf_[i].KernelTime.QuadPart;
        ULONGLONG userDelta = current_perf[i].UserTime.QuadPart - prev_core_perf_[i].UserTime.QuadPart;

        // In Windows, KernelTime includes IdleTime
        ULONGLONG activeTime = (kernelDelta > idleDelta ? (kernelDelta - idleDelta) : 0) + userDelta;
        ULONGLONG totalTime = activeTime + idleDelta;

        double usage = 0.0;
        if (totalTime > 0) {
            usage = (static_cast<double>(activeTime) / static_cast<double>(totalTime)) * 100.0;
        }
        usage = std::clamp(usage, 0.0, 100.0);
        metrics_.per_core_usage[i] = usage;
        sum_usage += usage;

        if (usage > max_usage) {
            max_usage = usage;
            max_idx = static_cast<int>(i);
        }
    }

    metrics_.max_core_usage = max_usage;
    metrics_.max_core_index = max_idx;
    metrics_.total_usage_percent = sum_usage / static_cast<double>(metrics_.logical_core_count);

    prev_core_perf_ = current_perf;
}

void CpuMonitor::wmi_worker_loop() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)hr;

    IWbemLocator* pLoc = nullptr;
    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, reinterpret_cast<LPVOID*>(&pLoc));
    if (FAILED(hr) || !pLoc) {
        CoUninitialize();
        return;
    }

    IWbemServices* pSvc = nullptr;
    hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &pSvc);
    if (FAILED(hr) || !pSvc) {
        pLoc->Release();
        CoUninitialize();
        return;
    }

    CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    while (worker_running_) {
        IEnumWbemClassObject* pEnumerator = nullptr;
        hr = pSvc->ExecQuery(
            bstr_t("WQL"),
            bstr_t("SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr,
            &pEnumerator
        );

        if (SUCCEEDED(hr) && pEnumerator) {
            IWbemClassObject* pclsObj = nullptr;
            ULONG uReturned = 0;
            int max_temp_c = 0;

            while (pEnumerator) {
                hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturned);
                if (uReturned == 0 || !pclsObj) break;

                VARIANT vtProp;
                VariantInit(&vtProp);
                hr = pclsObj->Get(L"CurrentTemperature", 0, &vtProp, nullptr, nullptr);
                if (SUCCEEDED(hr) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) {
                    long rawKelvinTenths = vtProp.lVal;
                    // Convert tenths of Kelvin to Celsius: (K / 10) - 273.15
                    int celsius = static_cast<int>(std::round((rawKelvinTenths / 10.0) - 273.15));
                    if (celsius > 10 && celsius < 115) {
                        if (celsius > max_temp_c) {
                            max_temp_c = celsius;
                        }
                    }
                }
                VariantClear(&vtProp);
                pclsObj->Release();
            }
            pEnumerator->Release();

            if (max_temp_c > 0) {
                cached_temp_c_.store(max_temp_c);
            }
        }

        // Sleep 1500ms in 50ms slices to allow responsive shutdown
        for (int i = 0; i < 30 && worker_running_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    pSvc->Release();
    pLoc->Release();
    CoUninitialize();
}

void CpuMonitor::update_thermal_and_power() {
    int t = cached_temp_c_.load();
    if (t > 0) {
        metrics_.package_temp_c = t;
    } else {
        // Honest telemetry: if motherboard ACPI thermal zones are not exposed via WMI,
        // do NOT simulate or invent temperature from CPU load. Report 0 (N/A).
        metrics_.package_temp_c = 0;
    }

    // Honest telemetry: package power is 0 unless exposed by real hardware counters/MSR.
    metrics_.package_power_w = 0;
}

void CpuMonitor::update() {
    update_per_core_usage();
    update_thermal_and_power();
}

} // namespace anyperf
