#pragma once

#include "library/database.hpp"

#include <vector>

namespace melodeus::app {

/// Acción del usuario en la biblioteca.
enum class LibraryAction {
    None,
    LoadDeckA,
    LoadDeckB,
    AddFolder, ///< pide carpeta al usuario (picker COM en main)
    LoadUrl,   ///< carga una URL (YT/SC/fichero) vía streaming F4
};

/// Payload ImGui para arrastrar una fila de la biblioteca a un deck.
struct TrackDragPayload {
    int row = -1; ///< índice en LibraryUiState::rows
};
inline constexpr const char* kTrackDragType = "MD_TRACK";

/// Estado del panel de biblioteca (búsqueda FTS5 + tabla). Hay dos paneles
/// gemelos (izq/der) que comparten db; cada uno con su buscador/selección.
struct LibraryUiState {
    const library::Database* db = nullptr; ///< nullptr = panel vacío
    char search[128] = {};
    char url[1024] = {};
    char status[64] = {}; ///< texto del footer (progreso de import, etc.)
    std::vector<library::TrackMeta> rows;
    int selected = -1;
    bool needs_refresh = true;       ///< buscar al abrir o al cambiar el texto
    bool focus_search = false;       ///< atajo '/' → foco en el buscador
    bool scroll_to_selected = false; ///< al cambiar la selección por teclado
    bool show_url = false;           ///< sólo el panel izquierdo (streaming)
    bool show_folder = false;        ///< sólo el panel izquierdo (import)
    bool accent_a = true;            ///< teñir el título con el acento del deck

    void refresh() {
        rows.clear();
        if (db != nullptr && db->ok()) {
            rows = db->search(search[0] != '\0' ? search : "", 500);
        }
        if (selected >= static_cast<int>(rows.size())) {
            selected = -1;
        }
        needs_refresh = false;
    }

    /// Mueve la selección con ↑/↓ (la_visibilidad la pide el panel).
    void move_selection(int delta) {
        if (rows.empty()) {
            selected = -1;
            return;
        }
        if (selected < 0) {
            selected = (delta > 0) ? 0 : static_cast<int>(rows.size()) - 1;
        } else {
            selected += delta;
            if (selected < 0) {
                selected = 0;
            }
            if (selected >= static_cast<int>(rows.size())) {
                selected = static_cast<int>(rows.size()) - 1;
            }
        }
        scroll_to_selected = true;
    }
};

/// Dibuja el panel (cabecera con título, buscador, tabla con Selectable +
/// drag & drop, footer). Devuelve la acción del frame.
LibraryAction draw_library_panel(LibraryUiState& state, float width_px, float height_px,
                                 float scale);

} // namespace melodeus::app
