#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <functional>
#include <string>
#include "anyperf/types.hpp"

namespace anyperf {

class Dx11Backend {
public:
    Dx11Backend();
    ~Dx11Backend();

    bool init(int width = 1280, int height = 800, const std::wstring& title = L"AnyPerfomans - Performance Profiler");
    void shutdown();

    bool process_messages();
    void begin_frame();
    void end_frame();

    void set_overlay_mode(OverlayMode mode);
    OverlayMode get_overlay_mode() const { return current_mode_; }

    HWND get_hwnd() const { return hwnd_; }
    ID3D11Device* get_device() const { return d3d_device_; }
    ID3D11DeviceContext* get_context() const { return d3d_context_; }

    void handle_resize(UINT width, UINT height);

private:
    bool create_device_and_swapchain();
    void create_render_target();
    void cleanup_render_target();
    void setup_modern_imgui_style();

    HWND hwnd_ = nullptr;
    WNDCLASSEXW wc_{};
    int width_ = 1280;
    int height_ = 800;

    ID3D11Device* d3d_device_ = nullptr;
    ID3D11DeviceContext* d3d_context_ = nullptr;
    IDXGISwapChain* swap_chain_ = nullptr;
    ID3D11RenderTargetView* main_render_target_view_ = nullptr;

    OverlayMode current_mode_ = OverlayMode::Dashboard;
    bool is_initialized_ = false;
};

} // namespace anyperf
