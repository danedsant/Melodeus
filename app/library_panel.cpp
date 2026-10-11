#include "library_panel.hpp"

#include "render/imgui_layer.hpp"
#include "theme.hpp"

#include "imgui.h"

#include <cstdio>

namespace melodeus::app {

LibraryAction draw_library_panel(LibraryUiState& state, float width_px, float height_px,
                                 float scale) {
    LibraryAction action = LibraryAction::None;
    if (state.needs_refresh) {
        state.refresh();
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx = {mn.x + width_px, mn.y + height_px};
    theme::glass(dl, mn, mx);

    // --- Cabecera: título teñido + contador -------------------------------
    const float header_h = 28.0f * scale;
    const ImU32 title_col = state.accent_a ? theme::kAccentA : theme::kAccentB;
    if (ImFont* title = imgui_font_title()) {
        dl->AddText(title, title->LegacySize, ImVec2(mn.x + 12.0f * scale, mn.y + 4.0f * scale),
                    title_col, "BIBLIOTECA");
    } else {
        dl->AddText(ImVec2(mn.x + 12.0f * scale, mn.y + 5.0f * scale), title_col, "BIBLIOTECA");
    }
    const int total = (state.db != nullptr) ? state.db->count() : 0;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "(%d)", total);
    const ImVec2 cs = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(mx.x - 12.0f * scale - cs.x, mn.y + 7.0f * scale), theme::kTextDim, buf);
    ImGui::Dummy(ImVec2(0.0f, header_h));

    // --- Buscador (con icono lupa) ------------------------------------------
    const float pad = 10.0f * scale;
    const float inner_w = width_px - pad * 2.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
    ImGui::SetNextItemWidth(inner_w - 26.0f * scale);
    if (state.focus_search) {
        ImGui::SetKeyboardFocusHere();
        state.focus_search = false;
    }
    if (ImGui::InputTextWithHint("##search", "buscar (titulo, artista, album)...", state.search,
                                 sizeof(state.search), ImGuiInputTextFlags_EnterReturnsTrue)) {
        state.needs_refresh = true;
    }
    if (ImGui::IsItemEdited()) {
        state.needs_refresh = true;
    }
    ImGui::SameLine(0.0f, 4.0f * scale);
    dl->AddText(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + 4.0f * scale),
                theme::kTextDim, theme::icons::utf8(theme::icons::magnifier).c_str());
    ImGui::Dummy(ImVec2(20.0f * scale, 0.0f));

    // --- Fila URL (sólo panel izquierdo; streaming F4) --------------------
    if (state.show_url) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
        const float url_btn_w = 64.0f * scale;
        ImGui::SetNextItemWidth(inner_w - url_btn_w - 4.0f * scale);
        ImGui::InputTextWithHint("##url", "https://... (YouTube/SoundCloud)", state.url,
                                 sizeof(state.url));
        ImGui::SameLine(0.0f, 4.0f * scale);
        if (ImGui::Button("cargar", ImVec2(url_btn_w, 0.0f)) && state.url[0] != '\0') {
            action = LibraryAction::LoadUrl;
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));

    // --- Tabla de pistas ---------------------------------------------------
    const float footer_h = 26.0f * scale;
    const float table_h = mn.y + height_px - footer_h - ImGui::GetCursorScreenPos().y;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
    ImFont* mono = imgui_font_mono();
    if (ImGui::BeginTable("##lib", 5,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Hideable,
                          ImVec2(inner_w, table_h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Título", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("BPM", ImGuiTableColumnFlags_WidthFixed, 52.0f * scale);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 40.0f * scale);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 46.0f * scale);
        ImGui::TableSetupColumn("##ab", ImGuiTableColumnFlags_WidthFixed, 46.0f * scale);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(state.rows.size()); ++i) {
            const library::TrackMeta& t = state.rows[static_cast<std::size_t>(i)];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 23.0f * scale);

            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            const bool sel = (i == state.selected);
            if (ImGui::Selectable(t.title.c_str(), sel,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0.0f, 19.0f * scale))) {
                state.selected = i;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    action = state.accent_a ? LibraryAction::LoadDeckA : LibraryAction::LoadDeckB;
                }
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
                state.selected = i;
            }
            if (ImGui::BeginDragDropSource()) {
                TrackDragPayload payload;
                payload.row = i;
                ImGui::SetDragDropPayload(kTrackDragType, &payload, sizeof(payload));
                ImGui::Text("%s", t.title.c_str());
                ImGui::EndDragDropSource();
            }
            if (state.scroll_to_selected && sel) {
                ImGui::SetScrollHereY();
                state.scroll_to_selected = false;
            }

            char cell[64];
            ImGui::TableSetColumnIndex(1);
            if (t.bpm > 0.0) {
                std::snprintf(cell, sizeof(cell), "%.1f", t.bpm);
            } else {
                std::snprintf(cell, sizeof(cell), "--");
            }
            if (mono != nullptr) {
                ImGui::PushFont(mono);
                ImGui::TextDisabled("%s", cell);
                ImGui::PopFont();
            } else {
                ImGui::TextDisabled("%s", cell);
            }

            ImGui::TableSetColumnIndex(2);
            if (t.camelot > 0) {
                std::snprintf(cell, sizeof(cell), "%d%c", t.camelot, t.camelot_minor ? 'A' : 'B');
            } else {
                std::snprintf(cell, sizeof(cell), "--");
            }
            ImGui::TextColored(ImVec4(0.42f, 0.72f, 0.55f, 1.0f), "%s", cell);

            ImGui::TableSetColumnIndex(3);
            const int secs = static_cast<int>(t.duration_ms / 1000);
            std::snprintf(cell, sizeof(cell), "%d:%02d", secs / 60, secs % 60);
            if (mono != nullptr) {
                ImGui::PushFont(mono);
                ImGui::TextDisabled("%s", cell);
                ImGui::PopFont();
            } else {
                ImGui::TextDisabled("%s", cell);
            }

            // Col 4: cargar a A/B (acentos del deck).
            ImGui::TableSetColumnIndex(4);
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(40, 74, 130, 120));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(60, 110, 190, 200));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70, 130, 220, 240));
            if (ImGui::SmallButton("A")) {
                state.selected = i;
                action = LibraryAction::LoadDeckA;
            }
            ImGui::SameLine(0.0f, 3.0f * scale);
            ImGui::PopStyleColor(3);
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(130, 74, 40, 120));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(190, 110, 60, 200));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(220, 130, 70, 240));
            if (ImGui::SmallButton("B")) {
                state.selected = i;
                action = LibraryAction::LoadDeckB;
            }
            ImGui::PopStyleColor(3);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // --- Footer: [+ carpeta] (sólo izq) + estado ---------------------------
    const ImVec2 foot = {mn.x + pad, mn.y + height_px - footer_h + 2.0f * scale};
    ImGui::SetCursorScreenPos(foot);
    if (state.show_folder) {
        const ImVec2 f0 = ImGui::GetCursorScreenPos();
        const std::string folder_icon = theme::icons::utf8(theme::icons::folder_plus);
        ImGui::TextColored(ImVec4(0.48f, 0.68f, 0.95f, 1.0f), "%s carpeta", folder_icon.c_str());
        const ImVec2 f1 = ImGui::GetItemRectMax();
        const bool folder_hover = ImGui::IsMouseHoveringRect(f0, f1);
        if (ImGui::IsMouseClicked(0) && folder_hover) {
            action = LibraryAction::AddFolder;
        }
        ImGui::SameLine();
    }
    if (state.status[0] != '\0') {
        ImGui::TextColored(ImVec4(0.65f, 0.72f, 0.82f, 1.0f), "%s", state.status);
    } else {
        ImGui::TextColored(ImVec4(0.45f, 0.48f, 0.55f, 1.0f), "%d pistas",
                           static_cast<int>(state.rows.size()));
    }
    return action;
}

} // namespace melodeus::app
