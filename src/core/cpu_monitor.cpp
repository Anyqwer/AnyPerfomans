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

    init_wmi();
    update();
}

CpuMonitor::~CpuMonitor() {
    cleanup_wmi();
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

bool CpuMonitor::init_wmi() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)hr;

    IWbemLocator* pLoc = nullptr;
    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, reinterpret_cast<LPVOID*>(&pLoc));
    if (FAILED(hr) || !pLoc) return false;

    IWbemServices* pSvc = nullptr;
    hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &pSvc);
    if (FAILED(hr) || !pSvc) {
        pLoc->Release();
        return false;
    }

    CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    wmi_locator_ = pLoc;
    wmi_services_ = pSvc;
    wmi_initialized_ = true;
    return true;
}

void CpuMonitor::query_wmi_temperature() {
    if (!wmi_initialized_ || !wmi_services_) return;

    auto* pSvc = reinterpret_cast<IWbemServices*>(wmi_services_);
    IEnumWbemClassObject* pEnumerator = nullptr;

    HRESULT hr = pSvc->ExecQuery(
        bstr_t("WQL"),
        bstr_t("SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr,
        &pEnumerator
    );

    if (FAILED(hr) || !pEnumerator) return;

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
        metrics_.package_temp_c = max_temp_c;
    }
}

void CpuMonitor::cleanup_wmi() {
    if (wmi_services_) {
        reinterpret_cast<IWbemServices*>(wmi_services_)->Release();
        wmi_services_ = nullptr;
    }
    if (wmi_locator_) {
        reinterpret_cast<IWbemLocator*>(wmi_locator_)->Release();
        wmi_locator_ = nullptr;
    }
    wmi_initialized_ = false;
}

void CpuMonitor::update_thermal_and_power() {
    query_wmi_temperature();

    // Estimate live CPU package power (Watts) based on load and core count if direct MSR is unavailable
    // Typical modern desktop x64 CPU: 15W idle, up to 65W - 125W load
    double base_idle_watts = 15.0;
    double max_boost_watts = 65.0;
    if (metrics_.logical_core_count >= 16) {
        max_boost_watts = 125.0;
    } else if (metrics_.logical_core_count >= 8) {
        max_boost_watts = 85.0;
    }
    double live_watts = base_idle_watts + (metrics_.total_usage_percent / 100.0) * (max_boost_watts - base_idle_watts);
    metrics_.package_power_w = static_cast<int>(std::round(live_watts));
}

void CpuMonitor::update() {
    update_per_core_usage();
    update_thermal_and_power();
}

} // namespace anyperf
