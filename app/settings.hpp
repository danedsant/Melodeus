#pragma once

// Configuración persistente de la app (F3.10): idioma + atajos rebindables.
// Formato de disco: pares clave=valor, uno por línea (# comentario). Portable:
// vive junto al exe (settings.json). Sin dependencias externas.

#include "imgui.h"

#include <array>
#include <string>

namespace melodeus::app {

/// Idioma de la UI.
enum class Language : int { Es = 0, En = 1, PtBr = 2, Count };

/// Acciones rebindables (atajos de teclado).
enum class Action : int {
    PlayA,
    PlayB,
    PitchADown,
    PitchAUp,
    PitchBDown,
    PitchBUp,
    CueA,
    CueB,
    SyncA,
    SyncB,
    LoadA,     // Enter / 1 (cargar selección en deck A)
    LoadB,     // Shift+Enter / 2
    Search,    // / → foco en buscador
    ClearSearch, // Esc
    StopBoth,  // Space
    NavUp,     // ↑
    NavDown,   // ↓
    Count
};

struct Settings {
    Language lang = Language::Es;
    std::array<ImGuiKey, static_cast<std::size_t>(Action::Count)> keys{};
    float color_deck_a[3] = {178.0f / 255.0f, 60.0f / 255.0f, 240.0f / 255.0f}; // Electric Purple
    float color_deck_b[3] = {236.0f / 255.0f, 130.0f / 255.0f, 250.0f / 255.0f}; // Hot Purple
    int pitch_range = 16; // 8, 16, 50, 100
    bool quantize = true;
    bool show_beatgrid = true;
    int theme_preset = 0; // 0=Electric Purple, 1=Pioneer Club, 2=Cyberpunk, 3=Acid Neon, etc., -1=Custom
    std::string download_dir{}; // vacio = carpeta 'downloads' por defecto junto a la db

    /// Valores por defecto (atajos originales).
    static Settings defaults();

    /// Carga desde archivo (clave=valor). false si no existe o I/O falla
    /// (los campos no tocados conservan los defaults).
    bool load(const std::string& path);
    /// Guarda todo el estado. false si I/O falla.
    bool save(const std::string& path) const;

    ImGuiKey key(Action a) const;
    void set_key(Action a, ImGuiKey k);

    /// Nombre mostrable de la acción (para la UI de rebind; traducido).
    static const char* action_label(Action a);
    /// Nombre mostrable de una tecla (p. ej. "Q", "Shift+Enter").
    static const char* key_label(ImGuiKey k);
};

/// Instancia global de la configuración (cargada una vez al arrancar).
Settings& global_settings();

/// Ruta del archivo de settings: junto al exe (portable) o cwd en dev.
std::string settings_path();

} // namespace melodeus::app
