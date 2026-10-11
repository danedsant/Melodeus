#pragma once

#include <cstddef>
#include <vector>

namespace melodeus::app {

/// Muestreo de duraciones de frame (segundos) para HUD y selftest.
class FrameStats {
public:
    void record(double dt);
    void reset() {
        samples_.clear();
    }
    std::size_t size() const {
        return samples_.size();
    }
    const std::vector<double>& samples() const {
        return samples_;
    }

private:
    std::vector<double> samples_;
};

struct RunStats {
    std::size_t frames = 0;
    double avg_fps = 0.0;
    double min_fps_1s = 0.0; ///< peor promedio en cualquier ventana de 1 s
    double avg_ms = 0.0;
    double max_ms = 0.0;
};

/// Estadisticas sobre samples[begin..fin). min_fps_1s ignora la cola si no
/// alcanza 1 s de ventana.
RunStats compute_run_stats(const std::vector<double>& samples, std::size_t begin);

/// Conecta stdout a la consola del proceso padre (app WIN32-subsistema).
bool attach_parent_console();

/// HUD del spike: ventana ImGui con FPS, frame time, adapter y selftest.
void draw_hud(const RunStats& stats, const std::vector<double>& samples, const wchar_t* adapter,
              int width, int height, bool vsync, bool selftest, double elapsed, double duration);

} // namespace melodeus::app
