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

    // --- Cabecera: título + contador + toggle de lado ---------------------
    const float header_h = 30.0f * scale;
    if (ImFont* title = imgui_font_title()) {
        dl->AddText(title, title->LegacySize, ImVec2(mn.x + 12.0f * scale, mn.y + 5.0f * scale),
                    theme::kText, "BIBLIOTECA");
    } else {
        dl->AddText(ImVec2(mn.x + 12.0f * scale, mn.y + 6.0f * scale), theme::kText, "BIBLIOTECA");
    }
    const int total = (state.db != nullptr) ? state.db->count() : 0;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "(%d)", total);
    const ImVec2 cs = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(mx.x - 52.0f * scale - cs.x, mn.y + 8.0f * scale), theme::kTextDim, buf);

    const ImVec2 t0 = {mx.x - 44.0f * scale, mn.y + 5.0f * scale};
    const ImVec2 t1 = {mx.x - 10.0f * scale, mn.y + header_h - 5.0f * scale};
    const bool toggle_hover = ImGui::IsMouseHoveringRect(t0, t1);
    dl->AddRectFilled(t0, t1, toggle_hover ? IM_COL32(84, 96, 122, 160) : IM_COL32(62, 70, 90, 100),
                      theme::kRadiusControl);
    const char* toggle = state.on_left ? ">>" : "<<";
    const ImVec2 ts = ImGui::CalcTextSize(toggle);
    dl->AddText(ImVec2((t0.x + t1.x - ts.x) * 0.5f, (t0.y + t1.y - ts.y) * 0.5f), theme::kTextHot,
                toggle);
    if (ImGui::IsMouseClicked(0) && toggle_hover) {
        state.on_left = !state.on_left;
        action = LibraryAction::ToggleSide;
    }
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

    // --- Fila URL (streaming F4) ------------------------------------------
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
    const float url_btn_w = 64.0f * scale;
    ImGui::SetNextItemWidth(inner_w - url_btn_w - 4.0f * scale);
    ImGui::InputTextWithHint("##url", "https://... (YouTube/SoundCloud)", state.url,
                             sizeof(state.url));
    ImGui::SameLine(0.0f, 4.0f * scale);
    if (ImGui::Button("cargar", ImVec2(url_btn_w, 0.0f)) && state.url[0] != '\0') {
        action = LibraryAction::LoadUrl;
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));

    // --- Tabla de pistas ---------------------------------------------------
    const float footer_h = 28.0f * scale;
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
        ImGui::TableSetupColumn("BPM", ImGuiTableColumnFlags_WidthFixed, 56.0f * scale);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 44.0f * scale);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 50.0f * scale);
        ImGui::TableSetupColumn("##ab", ImGuiTableColumnFlags_WidthFixed, 50.0f * scale);
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
                    action = LibraryAction::LoadDeckA;
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

    // --- Footer: [+ carpeta] con icono + estado ----------------------------
    const ImVec2 foot = {mn.x + pad, mn.y + height_px - footer_h + 4.0f * scale};
    ImGui::SetCursorScreenPos(foot);
    const ImVec2 f0 = ImGui::GetCursorScreenPos();
    const std::string folder_icon = theme::icons::utf8(theme::icons::folder_plus);
    ImGui::TextColored(ImVec4(0.48f, 0.68f, 0.95f, 1.0f), "%s carpeta", folder_icon.c_str());
    const ImVec2 f1 = ImGui::GetItemRectMax();
    const bool folder_hover = ImGui::IsMouseHoveringRect(f0, f1);
    if (folder_hover) {
        ImGui::GetWindowDrawList()->AddText(ImVec2(f0.x, f1.y + 1.0f), theme::kTextDim, "_");
    }
    if (ImGui::IsMouseClicked(0) && folder_hover) {
        action = LibraryAction::AddFolder;
    }
    ImGui::SameLine();
    if (state.status[0] != '\0') {
        ImGui::TextColored(ImVec4(0.65f, 0.72f, 0.82f, 1.0f), "%s", state.status);
    } else {
        ImGui::TextColored(ImVec4(0.45f, 0.48f, 0.55f, 1.0f), "%d pistas",
                           static_cast<int>(state.rows.size()));
    }
    return action;
}

} // namespace melodeus::app
