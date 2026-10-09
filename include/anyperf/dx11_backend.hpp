#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <functional>
#include <string>
#include "anyperf/types.hpp"

struct ImFont;

namespace anyperf {

struct AppFonts {
    ImFont* regular = nullptr;    // Segoe UI 16px (smooth crisp UI text)
    ImFont* bold = nullptr;       // Segoe UI Bold 18px (section headers)
    ImFont* title = nullptr;      // Segoe UI Bold 22px (app title)
    ImFont* large_stat = nullptr; // Segoe UI Bold 32px (high-contrast KPI numbers)
    ImFont* mono = nullptr;       // Cascadia/Consolas 15px (HUD & metrics monospace)
    ImFont* mono_bold = nullptr;  // Cascadia/Consolas Bold 17px
    float dpi_scale = 1.0f;
};

class Dx11Backend {
public:
    Dx11Backend();
    ~Dx11Backend();

    bool init(int width = 1320, int height = 840, const std::wstring& title = L"AnyPerfomans - ROG Liquid Glass Edition");
    void shutdown();

    bool process_messages();
    void begin_frame();
    void end_frame();

    void set_overlay_mode(OverlayMode mode);
    void set_overlay_position(int x, int y, int w, int h);
    OverlayMode get_overlay_mode() const { return current_mode_; }

    HWND get_hwnd() const { return hwnd_; }
    ID3D11Device* get_device() const { return d3d_device_; }
    ID3D11DeviceContext* get_context() const { return d3d_context_; }
    const AppFonts& get_fonts() const { return fonts_; }

    void handle_resize(UINT width, UINT height);

private:
    bool create_device_and_swapchain();
    void create_render_target();
    void cleanup_render_target();
    void setup_modern_imgui_style();
    void load_high_dpi_fonts();

    HWND hwnd_ = nullptr;
    WNDCLASSEXW wc_{};
    int width_ = 1320;
    int height_ = 840;

    ID3D11Device* d3d_device_ = nullptr;
    ID3D11DeviceContext* d3d_context_ = nullptr;
    IDXGISwapChain* swap_chain_ = nullptr;
    ID3D11RenderTargetView* main_render_target_view_ = nullptr;

    OverlayMode current_mode_ = OverlayMode::Dashboard;
    AppFonts fonts_;
    bool is_initialized_ = false;
};

} // namespace anyperf
