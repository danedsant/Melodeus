#pragma once

#include <windows.h>

namespace melodeus::app {

/// Estado visible de un deck para el panel (display DJ: BPM/key/tiempo).
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

/// Salida del panel: qué pulsó el usuario y si soltó un track encima.
struct DeckPanelOut {
    DeckPanelRects rects{};
    bool play_toggled = false;
    bool cue_pressed = false;   ///< CUE = volver a 0
    bool pitch_changed = false; ///< usuario movió el fader de pitch
    double pitch_new = 0.0;
    int drop_row = -1; ///< fila de biblioteca soltada en el deck
};

/// Dibuja el header del deck (PLAY grande, CUE, BPM gigante + pitched,
/// key Camelot (tradicional), tiempo/restante, título, pitch fader ±8%)
/// y los dos carriles de onda (drop targets de la biblioteca).
DeckPanelOut draw_deck_panel(const DeckUiState& state, int deck_idx, float width_px, float scale);

} // namespace melodeus::app
