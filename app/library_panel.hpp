#pragma once

#include "library/database.hpp"
#include "streaming/stream_search.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace melodeus::app {

/// Acción del usuario en la biblioteca.
enum class LibraryAction {
    None,
    LoadDeckA,
    LoadDeckB,
    AddFolder,           ///< pide carpeta al usuario (picker COM en main)
    LoadUrl,             ///< carga una URL (YT/SC/fichero) vía streaming F4
    DeleteTrack,         ///< eliminar pista seleccionada de la biblioteca
    StartOnlineSearch,   ///< dispara búsqueda online en YT o SC
    LoadOnlineDeckA,     ///< cargar resultado online seleccionado en Deck A
    LoadOnlineDeckB,     ///< cargar resultado online seleccionado en Deck B
    DownloadOnlineOnly,  ///< solo descargar resultado online a la biblioteca
};

/// Payload ImGui para arrastrar una fila de la biblioteca a un deck.
struct TrackDragPayload {
    int row = -1; ///< índice en LibraryUiState::rows
};
inline constexpr const char* kTrackDragType = "MD_TRACK";

/// Payload ImGui para arrastrar un resultado de búsqueda online a un deck.
struct OnlineTrackDragPayload {
    int index = -1; ///< índice en LibraryUiState::online_results
};
inline constexpr const char* kOnlineTrackDragType = "MD_ONLINE_TRACK";

/// Pestañas de la biblioteca (fuente de pistas)
enum class LibraryTab : int {
    Local = 0,
    YouTube = 1,
    SoundCloud = 2,
};

/// Sub-vistas dentro de pestañas de streaming (descargadas vs resultados online)
enum class StreamSubView : int {
    Downloaded = 0,
    OnlineResults = 1,
};

/// Columnas de la tabla de la biblioteca
enum TrackColId : int {
    ColTitle = 0,
    ColArtist = 1,
    ColAlbum = 2,
    ColBpm = 3,
    ColKey = 4,
    ColTime = 5,
    ColCount = 6
};

/// Estado del panel de biblioteca (búsqueda FTS5 + tabla). Hay dos paneles
/// gemelos (izq/der) que comparten db; cada uno con su buscador/selección.
struct LibraryUiState {
    const library::Database* db = nullptr; ///< nullptr = panel vacío
    LibraryTab active_tab = LibraryTab::Local;
    StreamSubView subview_yt = StreamSubView::Downloaded;
    StreamSubView subview_sc = StreamSubView::Downloaded;
    char search[128] = {};
    char search_yt[256] = {};
    char search_sc[256] = {};
    char url[1024] = {};
    char status[64] = {}; ///< texto del footer (progreso de import, etc.)
    std::vector<library::TrackMeta> rows;
    std::vector<std::string> folders; ///< lista de carpetas únicas detectadas en la biblioteca
    int selected_folder = 0;          ///< 0 = Todas las carpetas, 1..N = carpeta específica
    int selected = -1;
    bool needs_refresh = true;       ///< buscar al abrir o al cambiar el texto
    bool focus_search = false;       ///< atajo '/' → foco en el buscador
    bool scroll_to_selected = false; ///< al cambiar la selección por teclado
    bool show_url = false;           ///< sólo el panel izquierdo (streaming)
    bool show_url_input = false;     ///< estado colapsable del input de streaming
    bool show_folder = false;        ///< sólo el panel izquierdo (import)
    bool accent_a = true;            ///< teñir el título con el acento del deck
    // Orden por columna (F3.8): 0=título 1=BPM 2=Key 3=Time.
    int sort_col = 0;
    bool sort_asc = true;
    // Indicador de carga (F3.8): fila enviada a un deck, se atenúa sola.
    int loading_row = -1;
    double loading_until = 0.0; ///< ImGui::GetTime() en el que se apaga

    // Estado de búsqueda en línea separado para YouTube y SoundCloud
    std::vector<streaming::OnlineTrackResult> online_results_yt;
    std::vector<streaming::OnlineTrackResult> online_results_sc;
    int selected_online = -1;
    int online_action_idx = -1;
    bool is_searching_online = false;
    char online_status_msg[128] = {};

    [[nodiscard]] const std::vector<streaming::OnlineTrackResult>& current_online_results() const {
        return (active_tab == LibraryTab::SoundCloud) ? online_results_sc : online_results_yt;
    }
    [[nodiscard]] std::vector<streaming::OnlineTrackResult>& current_online_results() {
        return (active_tab == LibraryTab::SoundCloud) ? online_results_sc : online_results_yt;
    }

    void refresh() {
        rows.clear();
        folders.clear();
        if (db != nullptr && db->ok()) {
            const int target_source = static_cast<int>(active_tab);
            const char* q = "";
            if (active_tab == LibraryTab::Local) {
                q = (search[0] != '\0') ? search : "";
            } else if (active_tab == LibraryTab::YouTube) {
                // Si search_yt es una URL, no filtramos por FTS para mostrar toda la colección
                const std::string s(search_yt);
                if (s.rfind("http://", 0) != 0 && s.rfind("https://", 0) != 0) {
                    q = search_yt;
                }
            } else if (active_tab == LibraryTab::SoundCloud) {
                const std::string s(search_sc);
                if (s.rfind("http://", 0) != 0 && s.rfind("https://", 0) != 0) {
                    q = search_sc;
                }
            }

            std::vector<library::TrackMeta> all_tracks = db->search(q, 500, target_source);

            // Extraer carpetas únicas solo en pestaña Local
            if (active_tab == LibraryTab::Local) {
                std::vector<std::string> unique_folders;
                for (const auto& r : all_tracks) {
                    if (!r.uri.empty()) {
                        std::filesystem::path p(r.uri);
                        std::string parent = p.parent_path().filename().string();
                        if (parent.empty()) {
                            parent = p.parent_path().string();
                        }
                        if (!parent.empty()) {
                            if (std::find(unique_folders.begin(), unique_folders.end(), parent) ==
                                unique_folders.end()) {
                                unique_folders.push_back(parent);
                            }
                        }
                    }
                }
                std::sort(unique_folders.begin(), unique_folders.end());
                folders = std::move(unique_folders);

                // Filtrar por carpeta si hay una seleccionada
                if (selected_folder > 0 && selected_folder <= static_cast<int>(folders.size())) {
                    const std::string& target_f = folders[static_cast<std::size_t>(selected_folder - 1)];
                    for (auto& r : all_tracks) {
                        std::filesystem::path p(r.uri);
                        std::string parent = p.parent_path().filename().string();
                        if (parent.empty()) {
                            parent = p.parent_path().string();
                        }
                        if (parent == target_f) {
                            rows.push_back(std::move(r));
                        }
                    }
                } else {
                    rows = std::move(all_tracks);
                }
            } else {
                rows = std::move(all_tracks);
            }
            apply_sort();
        }
        if (selected >= static_cast<int>(rows.size())) {
            selected = -1;
        }
        needs_refresh = false;
    }

    /// Reordena `rows` según sort_col/sort_asc (sin volver a consultar la DB).
    void apply_sort() {
        if (rows.size() < 2 || sort_col < 0 || sort_col >= ColCount) {
            return;
        }
        const int col = sort_col;
        const bool asc = sort_asc;

        const auto cmp_str_ci = [](const std::string& sa, const std::string& sb) -> int {
            if (sa.empty() && sb.empty()) {
                return 0;
            }
            if (sa.empty()) {
                return 1;
            }
            if (sb.empty()) {
                return -1;
            }
            const std::size_t n = std::min(sa.size(), sb.size());
            for (std::size_t i = 0; i < n; ++i) {
                const int ca = std::tolower(static_cast<unsigned char>(sa[i]));
                const int cb = std::tolower(static_cast<unsigned char>(sb[i]));
                if (ca != cb) {
                    return (ca < cb) ? -1 : 1;
                }
            }
            if (sa.size() != sb.size()) {
                return (sa.size() < sb.size()) ? -1 : 1;
            }
            return 0;
        };

        const auto cmp3 = [&](const library::TrackMeta& a,
                              const library::TrackMeta& b) -> int {
            switch (col) {
            case ColArtist:
                return cmp_str_ci(a.artist, b.artist);
            case ColAlbum:
                return cmp_str_ci(a.album, b.album);
            case ColBpm:
                if (a.bpm <= 0.0 && b.bpm <= 0.0) {
                    return 0;
                }
                if (a.bpm <= 0.0) {
                    return 1; // a al final
                }
                if (b.bpm <= 0.0) {
                    return -1;
                }
                if (a.bpm < b.bpm) {
                    return -1;
                }
                if (a.bpm > b.bpm) {
                    return 1;
                }
                return 0;
            case ColKey: // Camelot (1A < 1B < 2A...); sin key al final.
                if (a.camelot <= 0 && b.camelot <= 0) {
                    return 0;
                }
                if (a.camelot <= 0) {
                    return 1;
                }
                if (b.camelot <= 0) {
                    return -1;
                }
                if (a.camelot != b.camelot) {
                    return (a.camelot < b.camelot) ? -1 : 1;
                }
                if (a.camelot_minor != b.camelot_minor) {
                    return a.camelot_minor ? 1 : -1; // A (menor) antes que B
                }
                return 0;
            case ColTime: // Duración
                if (a.duration_ms < b.duration_ms) {
                    return -1;
                }
                if (a.duration_ms > b.duration_ms) {
                    return 1;
                }
                return 0;
            default: // ColTitle
                return cmp_str_ci(a.title, b.title);
            }
        };
        std::stable_sort(rows.begin(), rows.end(),
                         [&cmp3, asc](const library::TrackMeta& a, const library::TrackMeta& b) {
                             const int c = cmp3(a, b);
                             return asc ? (c < 0) : (c > 0);
                         });
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
