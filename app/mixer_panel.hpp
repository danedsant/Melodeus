#pragma once

namespace melodeus::app {

/// Estado del mixer mínimo (F3): crossfader, ganancias de canal y master.
struct MixerState {
    float crossfader = 0.5f; ///< 0 = canal B, 1 = canal A
    float ch_gain_a = 0.8f;  ///< lineal 0..1
    float ch_gain_b = 0.8f;
    float master = 0.8f; ///< lineal 0..1
};

/// Dibuja el panel del mixer (lado opuesto a la biblioteca) con VU meters
/// desde los peaks del motor. Modifica `state` con los inputs del usuario.
void draw_mixer_panel(MixerState& state, float peak_a, float peak_b, float master_env,
                      float width_px, float height_px, float scale);

} // namespace melodeus::app
