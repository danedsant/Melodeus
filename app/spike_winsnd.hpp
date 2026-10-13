#pragma once

#include "waveform/waveform.hpp"

#include <cstdint>

namespace melodeus::app {

/// Spike F0.3/F2/F3: abre el audio por JUCE/WASAPI (exclusivo con fallback a
/// compartido de baja latencia) y mezcla 2 pistas por el motor real durante
/// `seconds` (o hasta audio_spike_finish()). F2 añade pirámides de waveform
/// y tap del master; F3 añade carga de archivos con staging seguro y mixer
/// remoto para la UI.
void audio_spike_start(int seconds);

/// Detiene el spike, imprime el informe (dispositivo, modo, latencia,
/// muestras, xruns, tiempo de proceso) y devuelve 0 si todo OK.
int audio_spike_finish();

/// Instantánea para el hilo de render (posiciones, pirámides, mixer, tap).
/// Devuelve false si el audio no corre.
struct AudioSpikeView {
    double deck_a_seconds = 0.0;
    double deck_b_seconds = 0.0;
    double duration_a_seconds = 0.0;
    double duration_b_seconds = 0.0;
    double sample_rate = 48000.0;
    bool playing_a = false;
    bool playing_b = false;
    double pitch_a_pct = 0.0;
    double pitch_b_pct = 0.0;
    float peak_a = 0.0f;
    float peak_b = 0.0f;
    float master_env = 0.0f;
    const char* title_a = "";
    const char* title_b = "";
    const wave::WaveformPyramid* pyramid_a = nullptr;
    const wave::WaveformPyramid* pyramid_b = nullptr;
    /// Último bloque mono publicado (1024 muestras); estable entre cambios
    /// de tap_seq. Recalcular el espectro sólo si cambia el seq.
    const float* tap = nullptr;
    int tap_len = 0;
    unsigned tap_seq = 0;
    /// Contador de cargas aplicadas por deck (incrementado en el callback
    /// cuando se hace el swap; útil para el indicador de staging de la UI).
    int load_gen[2] = {0, 0};
    double key_shift_a = 0.0;
    double key_shift_b = 0.0;
    bool master_tempo_a = false;
    bool master_tempo_b = false;
    bool loading_a = false;
    bool loading_b = false;
};

bool audio_spike_view(AudioSpikeView* out);

/// Carga asíncrona en segundo plano (decode + pirámide en worker, swap O(1)
/// en el hilo de audio sin bloquear la UI ni el motor).
bool audio_spike_load_async(int deck, const char* path, const char* title);

/// Indica si hay una carga en progreso en segundo plano para el deck especificado.
bool audio_spike_is_loading(int deck);

/// Carga síncrona tradicional (usada en selftests o cuando se requiere espera).
bool audio_spike_load(int deck, const char* path, const char* title);

/// Igual que audio_spike_load pero con samples ya decodificados (camino de
/// streaming F4: la descarga se hace en otro hilo y aquí sólo se estagea).
bool audio_spike_load_samples(int deck, const float* interleaved, std::int64_t frames, int channels,
                              double sample_rate, const char* title);

/// Alterna play/stop del deck (aplicado en el hilo de audio).
void audio_spike_toggle_play(int deck);

/// Mixer remoto (aplicado en el hilo de audio con smoothing del motor).
void audio_spike_set_mixer(float crossfader, float gain_a, float gain_b, float master);

/// Pitch del deck en % (aplicado en el hilo de audio).
void audio_spike_set_pitch(int deck, float pct);

/// Key shift del deck en semitonos [-12..+12] (modifica el tono sin alterar BPM).
void audio_spike_set_key_shift(int deck, float semitones);

/// Master Tempo (Key Lock): cuando está activo, el pitch slider varía el BPM
/// pero mantiene la afinación fija.
void audio_spike_set_master_tempo(int deck, bool on);

/// Salta a `seconds` (CUE = 0.0). Aplicado en el hilo de audio.
void audio_spike_seek(int deck, double seconds);

/// EQ de 3 bandas del canal (-26..+6 dB por banda; 0 = plano).
void audio_spike_set_eq(int deck, float low_db, float mid_db, float high_db);

/// Filtro DJ del canal: -1 = LP full, 0 = bypass, +1 = HP full.
void audio_spike_set_filter(int deck, float pos);

/// Loop del deck: al alcanzar start+len vuelve a start. len <= 0 desactiva.
/// Aplicado en el hilo de audio al inicio del callback.
void audio_spike_set_loop(int deck, double start_sec, double len_sec);

/// Último error de carga (decode/staging); vacío si la última fue OK.
/// Cadena estable hasta la siguiente llamada a load.
const char* audio_spike_last_error();

} // namespace melodeus::app
