#pragma once

#include "library/database.hpp"

#include <vector>

namespace melodeus::app {

/// Acción del usuario en la biblioteca.
enum class LibraryAction {
    None,
    LoadDeckA,
    LoadDeckB,
    ToggleSide, ///< mueve el panel al otro lado
};

/// Estado del panel de biblioteca (búsqueda FTS5 + tabla).
struct LibraryUiState {
    const library::Database* db = nullptr; ///< nullptr = panel vacío
    char search[128] = {};
    std::vector<library::TrackMeta> rows;
    int selected = -1;
    bool on_left = true;
    bool needs_refresh = true; ///< buscar al abrir o al cambiar el texto

    void refresh() {
        rows.clear();
        selected = -1;
        if (db != nullptr && db->ok()) {
            rows = db->search(search[0] != '\0' ? search : "", 500);
        }
        needs_refresh = false;
    }
};

/// Dibuja el panel (cabecera con toggle, buscador, tabla). Devuelve la
/// acción del frame (carga en deck, toggle de lado).
LibraryAction draw_library_panel(LibraryUiState& state, float width_px, float height_px,
                                 float scale);

} // namespace melodeus::app
