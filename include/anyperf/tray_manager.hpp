#pragma once

#include <windows.h>
#include <shellapi.h>
#include <string>

namespace anyperf {

#define WM_TRAY_CALLBACK (WM_APP + 101)

enum TrayMenuCmd {
    TRAY_CMD_SHOW_DASHBOARD = 2001,
    TRAY_CMD_TOGGLE_HUD     = 2002,
    TRAY_CMD_RESTART        = 2003,
    TRAY_CMD_EXIT           = 2004
};

class TrayManager {
public:
    TrayManager();
    ~TrayManager();

    bool init(HWND hwnd);
    void shutdown();

    // Update tray icon with dynamic temperature number (e.g. "65" or "54") and rich tooltip
    void update_icon(int temp_c, bool is_gpu, bool show_temp_on_icon, const std::wstring& tooltip);

    // Show right-click popup context menu
    void show_context_menu();

    // Handle tray window messages (call from WndProc on WM_TRAY_CALLBACK)
    bool handle_tray_message(WPARAM wParam, LPARAM lParam, int& out_command);

    // Autostart with Windows via Task Scheduler (Admin without UAC prompt) + HKCU Run fallback
    static bool is_autostart_enabled();
    static bool set_autostart_enabled(bool enable, bool start_minimized);

private:
    HICON create_temperature_icon(int temp_c, bool is_gpu);

    HWND hwnd_ = nullptr;
    NOTIFYICONDATAW nid_{};
    bool is_initialized_ = false;
    HICON current_hicon_ = nullptr;
    int last_rendered_temp_ = -999;
    bool last_was_gpu_ = false;
    bool last_show_temp_ = false;
};

} // namespace anyperf
