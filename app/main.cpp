// Melodeus — spike F0/F2: ventana Win32 + ImGui/DX12 con layout DJ,
// waveform en D3D12 (pase instanciado), espectro del master y selftest.
//
//   melodeus.exe                  (interactivo, vsync)
//   melodeus.exe --paced          (sin vsync, pacing a 144 Hz por waitable
//                                  timer de alta resolución)
//   melodeus.exe --selftest 15    (15 s sin vsync; imprime fps y sale con
//                                  0 si cumple >=144 fps)
//   melodeus.exe --selftest 15 --paced  (idem con pacing)

#include "app_state.hpp"
#include "render/device_d3d12.hpp"
#include "render/imgui_layer.hpp"
#include "render/waveform_pass.hpp"
#include "spike_winsnd.hpp"
#include "waveform/fft.hpp"
#include "waveform/waveform.hpp"

#include "imgui.h"
#include "imgui_impl_win32.h"

#include <shellapi.h>

#include <algorithm>
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
using melodeus::app::WaveformPass;

constexpr float kLaneOverviewPx = 44.0f;
constexpr float kLaneDetailPx = 110.0f;
constexpr float kMixerPx = 120.0f;
constexpr float kSpectrumPx = 96.0f;
constexpr double kDetailSpanSec = 6.0;
constexpr int kSpectrumBars = 48;
constexpr std::size_t kInstCap = WaveformPass::kMaxInstances / 4; // por carril

DeviceD3D12 g_device;
WaveformPass g_wave_pass;

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

struct CliOptions {
    int selftest_seconds = 0;
    bool paced = false;
};

CliOptions parse_cli() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    CliOptions opt;
    if (argv == nullptr) {
        return opt;
    }
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--selftest") == 0) {
            opt.selftest_seconds = (i + 1 < argc) ? _wtoi(argv[i + 1]) : 15;
        } else if (std::wcscmp(argv[i], L"--paced") == 0) {
            opt.paced = true;
        }
    }
    LocalFree(argv);
    return opt;
}

/// Rellena las instancias de un carril (waveform + playhead) desde la
/// pirámide y la posición actual del deck. Devuelve el nº de instancias.
std::size_t fill_lane(melodeus::wave::WaveInstance* insts, std::size_t cap,
                      const melodeus::wave::WaveformPyramid& pyr, double pos_sec, float lane_w_px,
                      double span_sec, bool center_on_playhead) {
    if (pyr.total_frames() == 0 || cap < 2) {
        return 0;
    }
    const double rate = pyr.sample_rate();
    const double total = static_cast<double>(pyr.total_frames());
    const double span = span_sec * rate;
    const double center = center_on_playhead ? pos_sec * rate : total * 0.5;
    const double span_used = center_on_playhead ? span : total;
    const double spp = span_used / static_cast<double>(lane_w_px);
    const auto range = melodeus::wave::visible_range(pyr, center, span_used, spp);
    const double first = center - span_used * 0.5;

    std::size_t n = melodeus::wave::fill_wave_instances(pyr, range, first, span_used, -1.0f, 1.0f,
                                                        -1.0f, 1.0f, 0.95f, insts, cap - 2);
    if (center_on_playhead) {
        const double t = (pos_sec * rate - first) / span_used;
        if (t >= -0.02 && t <= 1.02 && n < cap) {
            const float x = static_cast<float>(-1.0 + 2.0 * t);
            const float w = 2.0f / lane_w_px; // ~2 px
            insts[n] = {x - w, x + w, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
            ++n;
        }
    }
    return n;
}

/// Layout DJ + relleno de instancias por frame. Devuelve los rects de los
/// 4 carriles de waveform (overview A, detail A, detail B, overview B).
struct DjLayout {
    RECT lanes[4]{};
    RECT spectrum{};
};

DjLayout draw_dj_layout(float scale, const melodeus::app::AudioSpikeView& view,
                        melodeus::wave::WaveInstance* insts_ov_a,
                        melodeus::wave::WaveInstance* insts_dt_a,
                        melodeus::wave::WaveInstance* insts_dt_b,
                        melodeus::wave::WaveInstance* insts_ov_b, std::size_t* counts) {
    DjLayout layout{};

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("dj", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto lane_px = [&](float h) {
        const ImVec2 mn = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(0.0f, h));
        const ImVec2 av = ImGui::GetContentRegionAvail();
        dl->AddRectFilled(mn, ImVec2(mn.x + av.x, mn.y + h), IM_COL32(18, 20, 26, 255));
        return RECT{static_cast<LONG>(mn.x), static_cast<LONG>(mn.y),
                    static_cast<LONG>(mn.x + av.x), static_cast<LONG>(mn.y + h)};
    };

    const float ov_h = kLaneOverviewPx * scale;
    const float dt_h = kLaneDetailPx * scale;
    layout.lanes[0] = lane_px(ov_h); // overview A
    layout.lanes[1] = lane_px(dt_h); // detail A
    lane_px(kMixerPx * scale);       // mixer placeholder (F3)
    {
        const ImVec2 mn = ImGui::GetCursorScreenPos();
        const ImVec2 av = ImGui::GetContentRegionAvail();
        ImGui::SetCursorScreenPos(ImVec2(mn.x + av.x * 0.5f - 80.0f * scale,
                                         mn.y + kMixerPx * scale * 0.5f - 10.0f * scale));
        ImGui::TextUnformatted("[ MIXER — F3 ]");
        ImGui::SetCursorScreenPos(ImVec2(mn.x + av.x * 0.5f - 80.0f * scale,
                                         mn.y + kMixerPx * scale * 0.5f + 10.0f * scale));
        ImGui::TextUnformatted("[ BROWSER — F4 ]");
        ImGui::SetCursorScreenPos(ImVec2(mn.x, mn.y + kMixerPx * scale));
    }
    layout.lanes[2] = lane_px(dt_h); // detail B
    layout.lanes[3] = lane_px(ov_h); // overview B
    layout.spectrum = lane_px(kSpectrumPx * scale);

    if (view.pyramid_a != nullptr && view.pyramid_b != nullptr) {
        const float w0 = static_cast<float>(layout.lanes[0].right - layout.lanes[0].left);
        const float w1 = static_cast<float>(layout.lanes[1].right - layout.lanes[1].left);
        const float w2 = static_cast<float>(layout.lanes[2].right - layout.lanes[2].left);
        const float w3 = static_cast<float>(layout.lanes[3].right - layout.lanes[3].left);
        counts[0] = fill_lane(insts_ov_a, kInstCap, *view.pyramid_a, view.deck_a_seconds, w0,
                              kDetailSpanSec, false);
        counts[1] = fill_lane(insts_dt_a, kInstCap, *view.pyramid_a, view.deck_a_seconds, w1,
                              kDetailSpanSec, true);
        counts[2] = fill_lane(insts_dt_b, kInstCap, *view.pyramid_b, view.deck_b_seconds, w2,
                              kDetailSpanSec, true);
        counts[3] = fill_lane(insts_ov_b, kInstCap, *view.pyramid_b, view.deck_b_seconds, w3,
                              kDetailSpanSec, false);
    }

    // Espectro del master (FFT del tap del callback; recalcular solo si
    // cambia el seq del bloque SPSC).
    static melodeus::wave::FftScratch fft(1024);
    static unsigned last_tap_seq = ~0u;
    static float bar_disp[kSpectrumBars]{};
    if (view.tap != nullptr && view.tap_len >= 128 && view.tap_seq != last_tap_seq) {
        last_tap_seq = view.tap_seq;
        fft.compute(view.tap, view.tap_len);
        const float* mag = fft.magnitudes();
        const int bins = fft.bins();
        float peak = 1e-9f;
        for (int i = 1; i < bins; ++i) {
            peak = (std::max)(peak, mag[i]);
        }
        for (int b = 0; b < kSpectrumBars; ++b) {
            const float t0 = static_cast<float>(b) / kSpectrumBars;
            const float t1 = static_cast<float>(b + 1) / kSpectrumBars;
            const int i0 = 1 + static_cast<int>((bins - 2) * t0 * t0);
            const int i1 = 1 + static_cast<int>((bins - 2) * t1 * t1);
            float m = 0.0f;
            for (int i = i0; i < i1 && i < bins; ++i) {
                m = (std::max)(m, mag[i]);
            }
            const float target = m / peak;
            bar_disp[b] = (target > bar_disp[b]) ? target : bar_disp[b] * 0.85f;
        }
    }
    {
        const float x0 = static_cast<float>(layout.spectrum.left);
        const float x1 = static_cast<float>(layout.spectrum.right);
        const float y1 = static_cast<float>(layout.spectrum.bottom);
        const float bw = (x1 - x0) / kSpectrumBars;
        for (int b = 0; b < kSpectrumBars; ++b) {
            const float h = bar_disp[b] * (y1 - static_cast<float>(layout.spectrum.top));
            const ImU32 col = IM_COL32(static_cast<int>(40 + 200 * bar_disp[b]),
                                       static_cast<int>(180 - 100 * bar_disp[b]), 60, 255);
            dl->AddRectFilled(ImVec2(x0 + bw * b + 1.0f, y1 - h),
                              ImVec2(x0 + bw * (b + 1) - 1.0f, y1), col);
        }
        dl->AddRect(ImVec2(static_cast<float>(layout.spectrum.left),
                           static_cast<float>(layout.spectrum.top)),
                    ImVec2(x1, y1), IM_COL32(40, 45, 55, 255));
    }

    ImGui::End();
    return layout;
}

/// Pacing de frames por QPC: duerme la mayor parte del periodo y hace un
/// spin corto sobre el deadline. Se usó waitable timer de alta resolución
/// primero, pero no expira en este entorno (ni el timer clásico); QPC es
/// portable y basta para el spike.
class FramePacer {
public:
    void init(int fps) {
        LARGE_INTEGER f{};
        ::QueryPerformanceFrequency(&f);
        freq_ = f.QuadPart > 0 ? f.QuadPart : 10000000LL;
        period_ = fps > 0 ? freq_ / fps : 0;
        deadline_ = 0;
    }

    void wait() {
        if (period_ <= 0) {
            return;
        }
        LARGE_INTEGER now{};
        ::QueryPerformanceCounter(&now);
        if (deadline_ == 0 || now.QuadPart >= deadline_) {
            deadline_ = now.QuadPart + period_;
            return;
        }
        const long long spin_margin = freq_ / 2000; // 0.5 ms
        while (now.QuadPart + spin_margin < deadline_) {
            ::Sleep(1);
            ::QueryPerformanceCounter(&now);
        }
        while (now.QuadPart < deadline_) {
            ::QueryPerformanceCounter(&now);
        }
        deadline_ += period_;
        if (now.QuadPart > deadline_ + period_) { // re-sincronizar si atrasa
            deadline_ = now.QuadPart + period_;
        }
    }

private:
    long long freq_ = 10000000LL;
    long long period_ = 0;
    long long deadline_ = 0;
};

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    melodeus::app::attach_parent_console();

    const CliOptions cli = parse_cli();
    const int selftest_seconds = (cli.selftest_seconds < 0) ? 0 : cli.selftest_seconds;

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
    g_hwnd = ::CreateWindowW(wc.lpszClassName, L"Melodeus — spike F2", WS_OVERLAPPEDWINDOW, 100,
                             100, static_cast<int>(1280 * scale), static_cast<int>(720 * scale),
                             nullptr, nullptr, wc.hInstance, nullptr);
    if (g_hwnd == nullptr) {
        return 1;
    }

    if (!g_device.init(g_hwnd)) {
        std::printf("[f2] no se pudo inicializar D3D12\n");
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    if (!melodeus::app::imgui_layer_init(g_hwnd, g_device)) {
        std::printf("[f2] no se pudo inicializar ImGui\n");
        g_device.shutdown();
        ::DestroyWindow(g_hwnd);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    const bool have_wave_pass = g_wave_pass.init(g_device);
    if (!have_wave_pass) {
        std::printf("[f2] waveform.vso/.pso no encontrados junto al exe; carriles vacios.\n");
    }

    ::ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(g_hwnd);

    const bool selftest = selftest_seconds > 0;
    const bool paced = cli.paced;
    if (selftest) {
        std::printf("[SELFTEST] adapter=%ls\n", g_device.adapter_name().c_str());
        std::printf("[SELFTEST] %d s sin vsync%s (objetivo >= 144 fps)...\n", selftest_seconds,
                    paced ? " + paced 144Hz" : "");
        melodeus::app::audio_spike_start(selftest_seconds);
    }

    // Pacing: sleep+spin a 144 Hz con --paced (sólo). Sin él, en iGPU la
    // integración de gráficos degrada el render (barrido libre + vsync off
    // dispara la temperatura).
    FramePacer pacer;
    if (paced) {
        pacer.init(144);
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

    static melodeus::wave::WaveInstance insts_ov_a[kInstCap];
    static melodeus::wave::WaveInstance insts_dt_a[kInstCap];
    static melodeus::wave::WaveInstance insts_dt_b[kInstCap];
    static melodeus::wave::WaveInstance insts_ov_b[kInstCap];
    std::size_t lane_counts[4]{};

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

        melodeus::app::AudioSpikeView view{};
        const bool audio_on = melodeus::app::audio_spike_view(&view);
        const DjLayout layout = draw_dj_layout(scale, view, insts_ov_a, insts_dt_a, insts_dt_b,
                                               insts_ov_b, lane_counts);

        const float clear[4] = {0.055f, 0.06f, 0.08f, 1.0f};
        ID3D12GraphicsCommandList* cmd = g_device.begin_frame(clear);
        melodeus::app::imgui_layer_render(cmd);
        if (have_wave_pass) {
            g_wave_pass.begin_frame(g_device.frame_slot());
            melodeus::wave::WaveInstance* bufs[4] = {insts_ov_a, insts_dt_a, insts_dt_b,
                                                     insts_ov_b};
            for (int i = 0; i < 4; ++i) {
                if (lane_counts[i] > 0) {
                    g_wave_pass.draw_lane(cmd, bufs[i], lane_counts[i], layout.lanes[i]);
                }
            }
            (void)audio_on;
        }
        g_device.end_frame(vsync);

        if (paced) {
            pacer.wait();
        }
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

    g_wave_pass.shutdown();
    melodeus::app::imgui_layer_shutdown();
    g_device.shutdown();
    ::DestroyWindow(g_hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return rc;
}
