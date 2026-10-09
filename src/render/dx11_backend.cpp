#include "anyperf/dx11_backend.hpp"
#include <dwmapi.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <implot.h>
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace anyperf {

static Dx11Backend* g_backend_instance = nullptr;

static LRESULT WINAPI StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        return true;
    }

    switch (msg) {
        case WM_SIZE:
            if (g_backend_instance && wParam != SIZE_MINIMIZED) {
                g_backend_instance->handle_resize(static_cast<UINT>(LOWORD(lParam)), static_cast<UINT>(HIWORD(lParam)));
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
                return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

Dx11Backend::Dx11Backend() {
    g_backend_instance = this;
}

Dx11Backend::~Dx11Backend() {
    shutdown();
    g_backend_instance = nullptr;
}

void Dx11Backend::setup_modern_imgui_style() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;

    style.WindowPadding = ImVec2(14.0f, 14.0f);
    style.FramePadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);

    // ASUS ROG Red & Black + Liquid Glass Palette
    colors[ImGuiCol_Text]                  = ImVec4(0.96f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.55f, 0.58f, 0.65f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.06f, 0.07f, 0.09f, 0.98f); // Obsidian Deep Black
    colors[ImGuiCol_ChildBg]               = ImVec4(0.09f, 0.10f, 0.14f, 0.82f); // Dark Liquid Glass
    colors[ImGuiCol_PopupBg]               = ImVec4(0.08f, 0.09f, 0.12f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.85f, 0.18f, 0.25f, 0.35f); // Subtle Crimson Edge Glow
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.12f, 0.14f, 0.19f, 0.85f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.24f, 0.14f, 0.18f, 0.85f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.32f, 0.16f, 0.22f, 0.90f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.14f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.06f, 0.07f, 0.09f, 0.75f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.09f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.06f, 0.07f, 0.09f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.35f, 0.15f, 0.20f, 0.80f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.55f, 0.18f, 0.25f, 0.90f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.80f, 0.18f, 0.25f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(1.00f, 0.22f, 0.32f, 1.00f); // ROG Crimson
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.92f, 0.20f, 0.28f, 0.90f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(1.00f, 0.30f, 0.38f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.70f, 0.14f, 0.22f, 0.80f); // Crimson Glass Button
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.92f, 0.20f, 0.30f, 0.90f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.55f, 0.10f, 0.16f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.40f, 0.12f, 0.18f, 0.70f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.60f, 0.16f, 0.24f, 0.80f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.75f, 0.18f, 0.28f, 0.90f);
    colors[ImGuiCol_Separator]             = ImVec4(0.45f, 0.15f, 0.20f, 0.40f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.12f, 0.14f, 0.18f, 0.80f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.45f, 0.15f, 0.22f, 0.80f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.75f, 0.15f, 0.22f, 0.95f);
    colors[ImGuiCol_PlotLines]             = ImVec4(1.00f, 0.24f, 0.32f, 1.00f); // Red Glow Lines
    colors[ImGuiCol_PlotLinesHovered]      = ImVec4(1.00f, 0.50f, 0.40f, 1.00f);
    colors[ImGuiCol_PlotHistogram]         = ImVec4(1.00f, 0.30f, 0.38f, 1.00f);
}

bool Dx11Backend::init(int width, int height, const std::wstring& title) {
    width_ = width;
    height_ = height;

    wc_ = {
        sizeof(WNDCLASSEXW),
        CS_CLASSDC,
        StaticWndProc,
        0L, 0L,
        GetModuleHandle(nullptr),
        nullptr, nullptr, nullptr, nullptr,
        L"AnyPerfomansWindowClass",
        nullptr
    };
    RegisterClassExW(&wc_);

    hwnd_ = CreateWindowExW(
        0,
        wc_.lpszClassName,
        title.c_str(),
        WS_OVERLAPPEDWINDOW,
        100, 100, width_, height_,
        nullptr, nullptr, wc_.hInstance, nullptr
    );

    if (!hwnd_) return false;

    if (!create_device_and_swapchain()) {
        cleanup_render_target();
        if (swap_chain_) { swap_chain_->Release(); swap_chain_ = nullptr; }
        if (d3d_context_) { d3d_context_->Release(); d3d_context_ = nullptr; }
        if (d3d_device_) { d3d_device_->Release(); d3d_device_ = nullptr; }
        UnregisterClassW(wc_.lpszClassName, wc_.hInstance);
        return false;
    }

    create_render_target();
    ShowWindow(hwnd_, SW_SHOWDEFAULT);
    UpdateWindow(hwnd_);

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    setup_modern_imgui_style();

    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(d3d_device_, d3d_context_);

    is_initialized_ = true;
    return true;
}

void Dx11Backend::set_overlay_mode(OverlayMode mode) {
    if (mode == current_mode_ || !hwnd_) return;
    current_mode_ = mode;

    if (mode == OverlayMode::MiniHud) {
        // Set transparent click-through topmost window
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT);

        // Remove window borders
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);

        // Extend DWM frame for transparent alpha blending
        MARGINS margins = { -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(hwnd_, &margins);

        // Position in top-right or desired corner of screen
        SetWindowPos(hwnd_, HWND_TOPMOST, 20, 20, 360, 240, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    } else {
        // Revert to normal windowed dashboard
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, (exStyle & ~WS_EX_LAYERED & ~WS_EX_TRANSPARENT & ~WS_EX_TOPMOST));
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);

        MARGINS margins = { 0, 0, 0, 0 };
        DwmExtendFrameIntoClientArea(hwnd_, &margins);

        SetWindowPos(hwnd_, HWND_NOTOPMOST, 100, 100, 1280, 800, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    }
}

void Dx11Backend::set_overlay_position(int x, int y, int w, int h) {
    if (!hwnd_ || current_mode_ != OverlayMode::MiniHud) return;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

bool Dx11Backend::create_device_and_swapchain() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd_;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevelArray, 2,
        D3D11_SDK_VERSION,
        &sd,
        &swap_chain_,
        &d3d_device_,
        &featureLevel,
        &d3d_context_
    );

    return SUCCEEDED(hr);
}

void Dx11Backend::create_render_target() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    if (swap_chain_ && SUCCEEDED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer)))) {
        d3d_device_->CreateRenderTargetView(pBackBuffer, nullptr, &main_render_target_view_);
        pBackBuffer->Release();
    }
}

void Dx11Backend::cleanup_render_target() {
    if (main_render_target_view_) {
        main_render_target_view_->Release();
        main_render_target_view_ = nullptr;
    }
}

void Dx11Backend::handle_resize(UINT width, UINT height) {
    if (swap_chain_ && width > 0 && height > 0) {
        width_ = width;
        height_ = height;
        cleanup_render_target();
        swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        create_render_target();
    }
}

bool Dx11Backend::process_messages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) {
            return false;
        }
    }
    return true;
}

void Dx11Backend::begin_frame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Dx11Backend::end_frame() {
    ImGui::Render();

    // Clear background
    float clear_color[4] = { 0.05f, 0.05f, 0.07f, 1.00f };
    if (current_mode_ == OverlayMode::MiniHud) {
        // Transparent clear for overlay HUD
        clear_color[0] = 0.0f;
        clear_color[1] = 0.0f;
        clear_color[2] = 0.0f;
        clear_color[3] = 0.0f;
    }

    d3d_context_->OMSetRenderTargets(1, &main_render_target_view_, nullptr);
    d3d_context_->ClearRenderTargetView(main_render_target_view_, clear_color);

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    swap_chain_->Present(1, 0); // VSync enabled
}

void Dx11Backend::shutdown() {
    if (!is_initialized_) return;
    is_initialized_ = false;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    cleanup_render_target();
    if (swap_chain_) { swap_chain_->Release(); swap_chain_ = nullptr; }
    if (d3d_context_) { d3d_context_->Release(); d3d_context_ = nullptr; }
    if (d3d_device_) { d3d_device_->Release(); d3d_device_ = nullptr; }

    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    UnregisterClassW(wc_.lpszClassName, wc_.hInstance);
}

} // namespace anyperf
