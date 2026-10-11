#pragma once

#include <windows.h>

namespace melodeus::app {

/// Estado visible de un deck para el panel (F3: BPM/key en el display).
struct DeckUiState {
    const char* title = "";
    const char* artist = "";
    double bpm = 0.0;         ///< BPM del análisis (0 = sin analizar)
    double bpm_pitched = 0.0; ///< BPM efectivo con el pitch actual
    int camelot = 0;          ///< 1..12 (0 = sin key)
    bool camelot_minor = false;
    const char* key_name = ""; ///< nombre tradicional, p.ej. "Am"
    double position_sec = 0.0;
    double duration_sec = 0.0;
    bool playing = false;
    double pitch_pct = 0.0;
};

struct DeckPanelRects {
    RECT overview{}; ///< carril de onda overview (pista completa)
    RECT detail{};   ///< carril de onda detail (scrolling)
};

/// Dibuja el header del deck (play, BPM grande + pitched, key Camelot
/// (tradicional), tiempo/restante, título) y los dos carriles de onda.
/// Devuelve los rects en píxeles de pantalla para el pase D3D12.
/// `play_toggled` se pone a true si el usuario pulsó play/stop.
DeckPanelRects draw_deck_panel(const DeckUiState& state, float width_px, float scale,
                               bool* play_toggled);

} // namespace melodeus::app
