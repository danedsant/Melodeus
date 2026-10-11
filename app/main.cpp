// Melodeus — spike F0: ventana Win32 + ImGui/DX12 con HUD y modo selftest.
//
//   melodeus.exe                  (interactivo, vsync)
//   melodeus.exe --selftest 15    (15 s sin vsync; imprime fps y sale con
//                                  0 si cumple >=144 fps)

#include "app_state.hpp"
#include "render/device_d3d12.hpp"
#include "render/imgui_layer.hpp"
#include "spike_winsnd.hpp"

#include "imgui.h"
#include "imgui_impl_win32.h"

#include <shellapi.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

// ImGui declara este handler dentro de un '#if 0' para no arrastrar windows.h;
// la app ya la incluye, así que se redeclara aquí (ver imgui_impl_win32.h).
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace {

using melodeus::app::DeviceD3D12;

DeviceD3D12 g_device;
HWND g_hwnd = nullptr;

LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return 1;
    }
    switch (msg) {
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            g_device.resize(LOWORD(lparam), HIWORD(lparam));
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU) { // sin menu ALT
            return 0;
        }
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

int parse_selftest_seconds() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return 0;
    }
    int seconds = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--selftest") == 0) {
            seconds = (i + 1 < argc) ? _wtoi(argv[i + 1]) : 15;
        }
    }
    LocalFree(argv);
    return seconds;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    melodeus::app::attach_parent_console();

    int selftest_seconds = parse_selftest_seconds();
    if (selftest_seconds < 0) {
        selftest_seconds = 0;
    }

    ImGui_ImplWin32_EnableDpiAwareness();
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc = {sizeof(wc),
                      CS_CLASSDC,
                      wnd_proc,
                      0L,
                      0L,
                      GetModuleHandle(nullptr),
                      nullptr,
                      nullptr,
                      nullptr,
                      nullptr,
                      L"MelodeusF0",
                      nullptr};
    ::RegisterClassExW(&wc);
    g_hwnd = ::CreateWindowW(wc.lpszClassName, L"Melodeus — spike F0", WS_OVERLAPPEDWINDOW, 100,
                             100, static_cast<int>(1280 * scale), static_cast<int>(720 * scale),
                             nullptr, nullptr, wc.hInstance, nullptr);
    if (g_hwnd == nullptr) {
        return 1;
    }

    if (!g_device.init(g_hwnd)) {
        std::printf("[f0] no se pudo inicializar D3D12\n");
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    if (!melodeus::app::imgui_layer_init(g_hwnd, g_device)) {
        std::printf("[f0] no se pudo inicializar ImGui\n");
        g_device.shutdown();
        ::DestroyWindow(g_hwnd);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(g_hwnd);

    const bool selftest = selftest_seconds > 0;
    if (selftest) {
        std::printf("[SELFTEST] adapter=%ls\n", g_device.adapter_name().c_str());
        std::printf("[SELFTEST] %d s sin vsync (objetivo >= 144 fps)...\n", selftest_seconds);
        melodeus::app::audio_spike_start(selftest_seconds);
    }

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    const auto to_secs = [&t0](clock::time_point t) {
        return std::chrono::duration<double>(t - t0).count();
    };

    melodeus::app::FrameStats stats;
    double last = 0.0;
    std::size_t warmup_index = 0;
    const bool vsync = !selftest;
    bool done = false;

    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
        }
        if (done) {
            break;
        }

        const double now = to_secs(clock::now());
        if (selftest && now >= selftest_seconds) {
            break;
        }
        if (::IsIconic(g_hwnd)) {
            ::Sleep(10);
            continue;
        }

        const double dt = now - last;
        last = now;
        if (dt > 0.0) {
            stats.record(dt);
        }
        if (selftest && warmup_index == 0 && now >= 0.5) {
            warmup_index = stats.size();
        }

        RECT client{};
        ::GetClientRect(g_hwnd, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;

        melodeus::app::imgui_layer_new_frame();
        const melodeus::app::RunStats live = melodeus::app::compute_run_stats(stats.samples(), 0);
        melodeus::app::draw_hud(live, stats.samples(), g_device.adapter_name().c_str(), width,
                                height, vsync, selftest, now,
                                static_cast<double>(selftest_seconds));

        const float clear[4] = {0.055f, 0.06f, 0.08f, 1.0f};
        ID3D12GraphicsCommandList* cmd = g_device.begin_frame(clear);
        melodeus::app::imgui_layer_render(cmd);
        g_device.end_frame(vsync);
    }

    g_device.wait_idle();

    int rc = 0;
    if (selftest) {
        RECT client{};
        ::GetClientRect(g_hwnd, &client);
        const melodeus::app::RunStats rs =
            melodeus::app::compute_run_stats(stats.samples(), warmup_index);
        const bool fps_ok = rs.avg_fps >= 144.0 && rs.min_fps_1s >= 120.0;
        std::printf("[SELFTEST] adapter=%ls res=%dx%d frames=%zu\n",
                    g_device.adapter_name().c_str(), client.right - client.left,
                    client.bottom - client.top, rs.frames);
        std::printf("[SELFTEST] fps avg=%.1f min1s=%.1f | frame ms avg=%.2f max=%.2f\n", rs.avg_fps,
                    rs.min_fps_1s, rs.avg_ms, rs.max_ms);
        std::printf("[SELFTEST] ventana 144fps: %s\n", fps_ok ? "OK" : "FAIL");
        const int audio_rc = melodeus::app::audio_spike_finish();
        rc = (fps_ok && audio_rc == 0) ? 0 : 1;
        std::printf("SELFTEST=%d\n", rc);
    }

    melodeus::app::imgui_layer_shutdown();
    g_device.shutdown();
    ::DestroyWindow(g_hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return rc;
}
