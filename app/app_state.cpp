#include "app_state.hpp"

#include "imgui.h"

#include <windows.h>
#include <consoleapi.h>

#include <cstdio>

namespace melodeus::app {

void FrameStats::record(double dt) {
    samples_.push_back(dt);
}

RunStats compute_run_stats(const std::vector<double>& samples, std::size_t begin) {
    RunStats out;
    if (begin >= samples.size()) {
        return out;
    }

    double sum = 0.0;
    double max_dt = 0.0;
    for (std::size_t i = begin; i < samples.size(); ++i) {
        sum += samples[i];
        if (samples[i] > max_dt) {
            max_dt = samples[i];
        }
    }
    const std::size_t n = samples.size() - begin;
    if (sum <= 0.0 || n == 0) {
        return out;
    }

    out.frames = n;
    out.avg_fps = static_cast<double>(n) / sum;
    out.avg_ms = 1000.0 * sum / static_cast<double>(n);
    out.max_ms = 1000.0 * max_dt;

    // Peor FPS medio en cualquier ventana contigua de >= 1 s (two-pointer).
    double min_fps = out.avg_fps;
    std::size_t tail = begin;
    double window_sum = 0.0;
    for (std::size_t head = begin; head < samples.size(); ++head) {
        window_sum += samples[head];
        while (window_sum - samples[tail] >= 1.0 && tail < head) {
            window_sum -= samples[tail];
            ++tail;
        }
        if (window_sum >= 1.0) {
            const double fps = static_cast<double>(head - tail + 1) / window_sum;
            if (fps < min_fps) {
                min_fps = fps;
            }
        }
    }
    out.min_fps_1s = min_fps;
    return out;
}

bool attach_parent_console() {
    // Si ya hay stdout (shell con redirección/pipes), no tocarlo.
    const HANDLE std_out = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (std_out != nullptr && std_out != INVALID_HANDLE_VALUE) {
        return true;
    }
    if (!::AttachConsole(ATTACH_PARENT_PROCESS)) {
        return false;
    }
    FILE* out = nullptr;
    const errno_t err = freopen_s(&out, "CONOUT$", "w", stdout);
    return err == 0 && out != nullptr;
}

void draw_hud(const RunStats& stats, const std::vector<double>& samples, const wchar_t* adapter,
              int width, int height, bool vsync, bool selftest, double elapsed, double duration) {
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 240.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Melodeus F0 — estado");

    ImGui::Text("FPS %.1f  |  min(1s) %.1f  |  avg %.2f ms  max %.2f ms", stats.avg_fps,
                stats.min_fps_1s, stats.avg_ms, stats.max_ms);
    ImGui::Text("Resolucion %dx%d  |  vsync %s", width, height, vsync ? "on" : "off");
    ImGui::Text("Adapter: %ls", adapter);

    // Ultimos 300 frames en ms.
    static std::vector<float> plot;
    const std::size_t count = samples.size() < 300 ? samples.size() : 300;
    plot.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t src = samples.size() - count + i;
        plot[i] = static_cast<float>(samples[src] * 1000.0);
    }
    if (count > 0) {
        float plot_max = 0.0f;
        for (const float v : plot) {
            if (v > plot_max) {
                plot_max = v;
            }
        }
        ImGui::PlotLines("ms/frame", plot.data(), static_cast<int>(count), 0, nullptr, 0.0f,
                         plot_max > 8.0f ? plot_max : 8.0f, ImVec2(-1.0f, 64.0f));
    }

    if (selftest) {
        ImGui::Separator();
        ImGui::Text("SELFTEST %.1f / %.1f s", elapsed, duration);
    }
    ImGui::End();
}

} // namespace melodeus::app
