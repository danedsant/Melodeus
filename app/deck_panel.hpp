#pragma once

#include "analysis/beatgrid.hpp"

#include <windows.h>

namespace melodeus::app {

/// Estado visible de un deck para el panel (display DJ: BPM/key/tiempo).
struct DeckUiState {
    const char* title = "";
    const char* artist = "";
    const char* album = "";
    double bpm = 0.0;         ///< BPM del análisis (0 = sin analizar)
    double bpm_pitched = 0.0; ///< BPM efectivo con el pitch actual
    int camelot = 0;          ///< 1..12 (0 = sin key)
    bool camelot_minor = false;
    const char* key_name = ""; ///< nombre tradicional, p.ej. "Am"
    double position_sec = 0.0;
    double duration_sec = 0.0;
    bool playing = false;
    double pitch_pct = 0.0;
    int pitch_range = 16;     ///< rango ±8, ±16, ±50, ±100
    double detail_span_sec = 6.0; ///< ventana (s) del carril detail para el scrub
    double hot_cues[3] = {-1.0, -1.0, -1.0}; ///< slots de hot cue (-1 = vacío)
    bool loop_on = false;         ///< loop activo en este deck
    double loop_len_sec = 4.0;    ///< longitud actual del loop (s)
    bool sync_on = false;         ///< sync BPM activo (este deck sigue al otro)
    double key_shift = 0.0;       ///< cambio de tono en semitonos [-12..+12]
    bool master_tempo = false;    ///< Key Lock activo
    bool is_loading = false;      ///< pista cargándose en segundo plano
    melodeus::analysis::BeatGrid beatgrid{}; ///< beatgrid para visualización y sync
    bool show_beatgrid = true;
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
    bool pitch_range_cycled = false; ///< usuario hizo click en el rango de pitch
    int key_shift_delta = 0;    ///< -1 (bajar semitono) o +1 (subir semitono)
    bool key_shift_reset = false; ///< volver a 0 semitonos
    bool master_tempo_toggled = false; ///< alternar Master Tempo / Key Lock
    int drop_row = -1;        ///< fila de biblioteca soltada en el deck
    int drop_online_row = -1; ///< resultado online soltado en el deck (-1 = ninguno)
    bool seek_requested = false; ///< usuario hizo click/arrastre en una onda
    double seek_seconds = 0.0;   ///< posición (s) a la que saltar
    int hot_cue_slot = -1;       ///< slot 0..2 pulsado (-1 = ninguno)
    bool hot_cue_set = false;    ///< true = guardar en slot; false = saltar a él
    int hot_cue_clear = -1;      ///< slot 0..2 a borrar (-1 = ninguno)
    bool loop_toggled = false;   ///< usuario pulsó LOOP (activar/desactivar)
    int loop_len_delta = 0;      ///< ‹ = -1 (acortar), › = +1 (alargar)
    bool sync_pressed = false;   ///< usuario pulsó SYNC (toggle BPM-matching)
};

/// Dibuja el deck en 3 bloques verticales (F3.6.x): display glass (BPM/key/
/// tiempo/título) arriba, los dos carriles de onda (drop targets) en el medio
/// y los controles (PLAY, CUE, pitch fader ±8%) abajo.
DeckPanelOut draw_deck_panel(const DeckUiState& state, int deck_idx, float width_px, float scale);

/// Altura total exacta (Panel 1 + mt-2 + Panel 2) que dibujará
/// draw_deck_panel. El caller la usa para dimensionar el child.
float deck_panel_height(float scale);

} // namespace melodeus::app
