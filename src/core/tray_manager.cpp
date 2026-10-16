#include "anyperf/tray_manager.hpp"
#include <iostream>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace anyperf {

TrayManager::TrayManager() = default;

TrayManager::~TrayManager() {
    shutdown();
}

bool TrayManager::init(HWND hwnd) {
    if (is_initialized_ || !hwnd) return false;
    hwnd_ = hwnd;

    ZeroMemory(&nid_, sizeof(nid_));
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1001;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAY_CALLBACK;

    // Load default application or system icon initially
    current_hicon_ = LoadIcon(nullptr, IDI_APPLICATION);
    nid_.hIcon = current_hicon_;
    wcscpy_s(nid_.szTip, L"AnyPerfomans Telemetry Engine");

    if (Shell_NotifyIconW(NIM_ADD, &nid_)) {
        is_initialized_ = true;
        return true;
    }
    return false;
}

void TrayManager::shutdown() {
    if (is_initialized_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        is_initialized_ = false;
    }
    if (current_hicon_) {
        DestroyIcon(current_hicon_);
        current_hicon_ = nullptr;
    }
}

HICON TrayManager::create_temperature_icon(int temp_c, bool is_gpu) {
    const int size = 16;
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = size; // bottom-up
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hbmColor = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmColor);

    // 1. Draw solid dark background rounded tile (#0C0E14)
    RECT rc = { 0, 0, size, size };
    HBRUSH hBgBrush = CreateSolidBrush(RGB(12, 14, 20));
    FillRect(hdcMem, &rc, hBgBrush);
    DeleteObject(hBgBrush);

    // 2. Select font & text color
    COLORREF textCol;
    if (temp_c < 60) {
        textCol = is_gpu ? RGB(0, 229, 255) : RGB(92, 200, 255); // Cyan / Ice Blue
    } else if (temp_c < 80) {
        textCol = RGB(255, 171, 0); // Amber / Gold
    } else {
        textCol = RGB(255, 23, 68); // Alert Red
    }

    SetTextColor(hdcMem, textCol);
    SetBkMode(hdcMem, TRANSPARENT);

    HFONT hFont = CreateFontW(
        -11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    HFONT hFontOld = (HFONT)SelectObject(hdcMem, hFont);

    wchar_t numStr[16];
    if (temp_c > 0 && temp_c < 200) {
        swprintf_s(numStr, L"%d", temp_c);
    } else {
        swprintf_s(numStr, L"--");
    }

    DrawTextW(hdcMem, numStr, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 3. Bottom 1-pixel color strip indicating sensor type (Cyan for GPU, Orange for CPU)
    RECT stripRc = { 1, size - 2, size - 1, size };
    HBRUSH hStripBrush = CreateSolidBrush(is_gpu ? RGB(0, 229, 255) : RGB(255, 171, 0));
    FillRect(hdcMem, &stripRc, hStripBrush);
    DeleteObject(hStripBrush);

    SelectObject(hdcMem, hFontOld);
    DeleteObject(hFont);
    SelectObject(hdcMem, hbmOld);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    // Create 1-bit monochrome mask (all 0 for opaque)
    HBITMAP hbmMask = CreateBitmap(size, size, 1, 1, nullptr);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = hbmMask;
    ii.hbmColor = hbmColor;
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hbmColor);
    DeleteObject(hbmMask);

    return hIcon;
}

void TrayManager::update_icon(int temp_c, bool is_gpu, bool show_temp_on_icon, const std::wstring& tooltip) {
    if (!is_initialized_) return;

    bool needs_icon_redraw = (temp_c != last_rendered_temp_) ||
                             (is_gpu != last_was_gpu_) ||
                             (show_temp_on_icon != last_show_temp_);

    if (needs_icon_redraw) {
        last_rendered_temp_ = temp_c;
        last_was_gpu_ = is_gpu;
        last_show_temp_ = show_temp_on_icon;

        HICON hNewIcon = nullptr;
        if (show_temp_on_icon && temp_c > 0) {
            hNewIcon = create_temperature_icon(temp_c, is_gpu);
        } else {
            hNewIcon = LoadIcon(nullptr, IDI_APPLICATION);
        }

        if (hNewIcon) {
            nid_.hIcon = hNewIcon;
            if (current_hicon_) {
                DestroyIcon(current_hicon_);
            }
            current_hicon_ = hNewIcon;
        }
    }

    if (!tooltip.empty()) {
        wcsncpy_s(nid_.szTip, tooltip.c_str(), sizeof(nid_.szTip) / sizeof(wchar_t) - 1);
    }

    nid_.uFlags = NIF_ICON | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void TrayManager::show_context_menu() {
    if (!hwnd_) return;

    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, TRAY_CMD_SHOW_DASHBOARD, L"Open AnyPerfomans Dashboard");
    AppendMenuW(hMenu, MF_STRING, TRAY_CMD_TOGGLE_HUD, L"Toggle Mini HUD [F11]");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, TRAY_CMD_RESTART, L"Restart AnyPerfomans");
    AppendMenuW(hMenu, MF_STRING, TRAY_CMD_EXIT, L"Exit AnyPerfomans");

    SetForegroundWindow(hwnd_);
    int cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(hMenu);

    if (cmd > 0) {
        PostMessageW(hwnd_, WM_COMMAND, cmd, 0);
    }
}

bool TrayManager::handle_tray_message(WPARAM wParam, LPARAM lParam, int& out_command) {
    (void)wParam;
    out_command = 0;

    if (lParam == WM_RBUTTONUP) {
        show_context_menu();
        return true;
    } else if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
        out_command = TRAY_CMD_SHOW_DASHBOARD;
        return true;
    }
    return false;
}

bool TrayManager::is_autostart_enabled() {
    // 1. Check Task Scheduler
    DWORD exitCode = 1;
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
    sei.lpVerb = L"open";
    sei.lpFile = L"schtasks.exe";
    sei.lpParameters = L"/query /tn \"AnyPerfomans\"";
    sei.nShow = SW_HIDE;

    if (ShellExecuteExW(&sei) && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 1000);
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        if (exitCode == 0) return true;
    }

    // 2. Fallback check registry
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t path[MAX_PATH] = { 0 };
        DWORD size = sizeof(path);
        bool found = (RegQueryValueExW(hKey, L"AnyPerfomans", nullptr, nullptr, reinterpret_cast<LPBYTE>(path), &size) == ERROR_SUCCESS);
        RegCloseKey(hKey);
        if (found) return true;
    }

    return false;
}

bool TrayManager::set_autostart_enabled(bool enable, bool start_minimized) {
    wchar_t myPath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, myPath, MAX_PATH);

    if (enable) {
        // Create scheduled task for launch with highest admin privileges without UAC prompt
        wchar_t params[1024];
        if (start_minimized) {
            swprintf_s(params, L"/create /tn \"AnyPerfomans\" /tr \"\\\"%s\\\" --minimized\" /sc onlogon /rl highest /f", myPath);
        } else {
            swprintf_s(params, L"/create /tn \"AnyPerfomans\" /tr \"\\\"%s\\\"\" /sc onlogon /rl highest /f", myPath);
        }

        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
        sei.lpVerb = L"open";
        sei.lpFile = L"schtasks.exe";
        sei.lpParameters = params;
        sei.nShow = SW_HIDE;
        if (ShellExecuteExW(&sei) && sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 2000);
            CloseHandle(sei.hProcess);
        }

        // Also add to HKCU Run as secondary fallback
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            std::wstring regVal = L"\"" + std::wstring(myPath) + L"\"";
            if (start_minimized) regVal += L" --minimized";
            RegSetValueExW(hKey, L"AnyPerfomans", 0, REG_SZ, reinterpret_cast<const BYTE*>(regVal.c_str()), static_cast<DWORD>((regVal.length() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }
    } else {
        // Delete Task
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
        sei.lpVerb = L"open";
        sei.lpFile = L"schtasks.exe";
        sei.lpParameters = L"/delete /tn \"AnyPerfomans\" /f";
        sei.nShow = SW_HIDE;
        if (ShellExecuteExW(&sei) && sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 2000);
            CloseHandle(sei.hProcess);
        }

        // Delete from HKCU Run
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, L"AnyPerfomans");
            RegCloseKey(hKey);
        }
    }

    return true;
}

} // namespace anyperf
