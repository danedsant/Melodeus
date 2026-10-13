#include "help_ui.hpp"

#include "i18n.hpp"
#include "render/imgui_layer.hpp"
#include "render/logo_texture.hpp"
#include "theme.hpp"

#include "imgui.h"
#include "imgui_internal.h"

namespace melodeus::app {

bool draw_help_modal(bool* open) {
    if (open == nullptr || !*open) {
        return false;
    }
    bool should_close = false;

    const ImVec2 display = ImGui::GetIO().DisplaySize;

    // Fondo modal semi-transparente que atenúa visualmente y absorbe eventos del fondo.
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(display);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
    if (ImGui::Begin("##help_backdrop", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    }
    ImGui::End();
    ImGui::PopStyleColor();

    ImGui::SetNextWindowSize(ImVec2(740.0f, 620.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(18.0f / 255.0f, 20.0f / 255.0f,
                                                    28.0f / 255.0f, 1.0f));

    const bool visible =
        ImGui::Begin(tr(Str::HelpTitle), open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    }

    if (visible) {
        // --- Cabecera del modal: Logo + Título del Software ---
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        if (global_logo().is_valid()) {
            ImGui::Image(global_logo().texture_id(), ImVec2(56.0f, 56.0f));
            ImGui::SameLine(0.0f, 14.0f);
        }
        ImGui::BeginGroup();
        if (ImFont* title_font = imgui_font_title()) {
            ImGui::PushFont(title_font);
            ImGui::TextColored(ImVec4(0.95f, 0.96f, 1.0f, 1.0f), "MELODEUS DJ STUDIO");
            ImGui::PopFont();
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.96f, 1.0f, 1.0f), "MELODEUS DJ STUDIO");
        }
        ImGui::TextColored(ImVec4(0.68f, 0.45f, 1.0f, 1.0f), "v1.0.0 — Professional DJ Audio Workstation");
        ImGui::TextDisabled("DirectX 12 Low-Latency Engine · Dual Decks · Real-Time Audio Stems");
        ImGui::EndGroup();

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        // --- Pestañas de la Guía ---
        if (ImGui::BeginTabBar("##help_tabs")) {
            // 1. Decks & Reproducción
            if (ImGui::BeginTabItem(tr(Str::TabHelpDecks))) {
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::BeginChild("##help_decks_scroll", ImVec2(0.0f, 400.0f), false);

                ImGui::TextColored(ImVec4(0.85f, 0.65f, 1.0f, 1.0f), "1. Carga de Pistas");
                ImGui::BulletText("Arrastra cualquier cancion desde la biblioteca inferior o desde el Explorador de Windows al Deck deseado.");
                ImGui::BulletText("Selecciona una pista en la biblioteca y pulsa [ Enter ] para cargar en Deck A o [ Shift + Enter ] para Deck B.");

                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::TextColored(ImVec4(0.85f, 0.65f, 1.0f, 1.0f), "2. Transporte y Control (Play & CUE)");
                ImGui::BulletText("Boton PLAY / Barra Espaciadora: Inicia o pausa la reproduccion instantanea.");
                ImGui::BulletText("Boton CUE: Regresa inmediatamente al inicio de la pista o al punto CUE temporal guardado.");

                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::TextColored(ImVec4(0.85f, 0.65f, 1.0f, 1.0f), "3. Hot Cues (Botones 1, 2, 3)");
                ImGui::BulletText("Clic izquierdo en un slot vacio: Guarda un punto Hot Cue en la posicion actual.");
                ImGui::BulletText("Clic izquierdo en un slot activo: Salta inmediatamente a la marca guardada.");
                ImGui::BulletText("Clic derecho en un slot activo: Borra el Hot Cue.");

                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::TextColored(ImVec4(0.85f, 0.65f, 1.0f, 1.0f), "4. Bucles (Loops)");
                ImGui::BulletText("Boton LOOP: Activa o desactiva el bucle automatico sincronizado.");
                ImGui::BulletText("Control de compases < 4 >: Ajusta la duracion del bucle desde 1/8 de compas hasta 32 beats.");

                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::TextColored(ImVec4(0.85f, 0.65f, 1.0f, 1.0f), "5. Pitch, Master Tempo y Modulacion de Tonalidad");
                ImGui::BulletText("Knob de PITCH: Varia la velocidad y tempo. Doble clic para restablecer al 0.0%% original.");
                ImGui::BulletText("Boton MT (Master Tempo / Key Lock): Mantiene fija la tonalidad musical original de la pista aunque cambies el BPM.");
                ImGui::BulletText("Afinador [-] / [+]: Sube o baja semitonos musicales independientes sin alterar el tempo.");

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // 2. Mixer & Audio
            if (ImGui::BeginTabItem(tr(Str::TabHelpMixer))) {
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::BeginChild("##help_mixer_scroll", ImVec2(0.0f, 400.0f), false);

                ImGui::TextColored(ImVec4(0.85f, 0.65f, 1.0f, 1.0f), "1. Ecualizador de 3 Bandas (HI / MID / LOW)");
                ImGui::BulletText("Perillas de HI (Agudos), MID (Medios) y LOW (Graves) para cada deck.");
                ImGui::BulletText("Permite recortar o potenciar frecuencias para transiciones musicales fluidas.");
                ImGui::BulletText("Doble clic en cualquier knob para centrarlo exactamente en 0 dB.");

                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::TextColored(ImVec4(0.85f, 0.65f, 1.0f, 1.0f), "2. Filtro de Frecuencia Bipolar (FILTER)");
                ImGui::BulletText("Girar a la izquierda: Filtro Pasa-Bajos (corta agudos, dejando solo graves calidos).");
                ImGui::BulletText("Girar a la derecha: Filtro Pasa-Altos (corta graves, ideal para transiciones previas al drop).");

                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::TextColored(ImVec4(0.85f, 0.65f, 1.0f, 1.0f), "3. Crossfader y Control Master");
                ImGui::BulletText("CROSSFADER: Funde gradualmente el sonido entre el Deck A (izquierda) y el Deck B (derecha).");
                ImGui::BulletText("Monitor de Peak: Barra de nivel maestro en la esquina superior derecha con aviso 'CLIP' en caso de saturacion.");

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // 3. Biblioteca & Streaming
            if (ImGui::BeginTabItem(tr(Str::TabHelpLibrary))) {
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::BeginChild("##help_lib_scroll", ImVec2(0.0f, 400.0f), false);

                ImGui::TextColored(ImVec4(0.85f, 0.65f, 1.0f, 1.0f), "1. Biblioteca Local");
                ImGui::BulletText("Buscador instantaneo FTS5: Escribe cualquier parte del titulo, artista o album para filtrado inmediato.");
                ImGui::BulletText("Importar Carpetas: Pulsa el boton '[+] carpeta' para anadir carpetas enteras de musica a la base de datos.");
                ImGui::BulletText("Selector de Carpetas: Filtra la vista para mostrar solo las pistas de una carpeta en particular.");

                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::TextColored(ImVec4(0.85f, 0.65f, 1.0f, 1.0f), "2. Integracion con YouTube y SoundCloud");
                ImGui::BulletText("Haz clic en las pestanas 'YouTube' o 'SoundCloud' en la cabecera de la biblioteca.");
                ImGui::BulletText("Busqueda directa: Escribe el nombre de una cancion o artista y pulsa Enter o 'Buscar'.");
                ImGui::BulletText("Pegado de enlaces: Pega directamente cualquier URL (https://...) para descargar y reproducir.");
                ImGui::BulletText("Carga automatica: Puedes arrastrar los resultados en linea directamente a los Decks.");

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // 4. Atajos de Teclado
            if (ImGui::BeginTabItem(tr(Str::TabHelpShortcuts))) {
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::BeginChild("##help_shortcuts_scroll", ImVec2(0.0f, 400.0f), false);

                if (ImGui::BeginTable("##help_table_keys", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Accion", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Tecla por defecto", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                    ImGui::TableHeadersRow();

                    struct KeyHelp { const char* act; const char* k; };
                    const KeyHelp kList[] = {
                        {"Play / Pausa Deck A", "Q"},
                        {"Play / Pausa Deck B", "W"},
                        {"Detener ambos Decks", "Espacio"},
                        {"CUE Deck A", "C"},
                        {"CUE Deck B", "N"},
                        {"SYNC Deck A", "S"},
                        {"SYNC Deck B", "F"},
                        {"Pitch Deck A (- / +)", "Z / X"},
                        {"Pitch Deck B (- / +)", ", / ."},
                        {"Cargar pista en Deck A", "Enter"},
                        {"Cargar pista en Deck B", "Shift + Enter"},
                        {"Mover seleccion en Biblioteca", "Flechas Arriba / Abajo"},
                        {"Enfocar caja de busqueda", "/"},
                        {"Limpiar busqueda actual", "Escape"},
                    };

                    for (const auto& item : kList) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(item.act);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextColored(ImVec4(0.85f, 0.7f, 1.0f, 1.0f), "%s", item.k);
                    }
                    ImGui::EndTable();
                }

                ImGui::Dummy(ImVec2(0.0f, 8.0f));
                ImGui::TextDisabled("Nota: Puedes personalizar todos los atajos en Ajustes -> Atajos.");
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        // Botón Cerrar
        const float w = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (ImGui::Button(tr(Str::Close), ImVec2(w, 0.0f))) {
            should_close = true;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();

    if (should_close) {
        *open = false;
        return true;
    }
    return false;
}

} // namespace melodeus::app
