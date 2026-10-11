#pragma once

namespace melodeus::app {

/// Estado del mixer (F3 + F3.5): crossfader, ganancias, master, EQ de 3
/// bandas por canal (-26..+6 dB) y filtro DJ (-1 LP .. +1 HP).
struct MixerState {
    float crossfader = 0.5f; ///< 0 = canal B, 1 = canal A
    float ch_gain_a = 0.8f;  ///< lineal 0..1
    float ch_gain_b = 0.8f;
    float master = 0.8f;            ///< lineal 0..1
    float eq_db[2][3]{};            ///< [canal][0=low,1=mid,2=high] en dB
    float filter[2] = {0.0f, 0.0f}; ///< -1 = LP full, 0 = bypass, +1 = HP full
};

/// Dibuja el mixer como columna central vertical (F3.6.x): EQ + FILTRO de
/// ambos canales arriba, crossfader al medio, master abajo. Sin VUs (el
/// peak del master vive en la top bar).
void draw_mixer_panel(MixerState& state, float width_px, float height_px, float scale);

} // namespace melodeus::app
