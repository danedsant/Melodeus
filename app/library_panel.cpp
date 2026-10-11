#include "library_panel.hpp"

#include "imgui.h"

#include <cstdio>

namespace melodeus::app {

namespace {

constexpr ImU32 kColPanelBg = IM_COL32(20, 22, 28, 255);
constexpr ImU32 kColBorder = IM_COL32(45, 50, 60, 255);
constexpr ImU32 kColText = IM_COL32(200, 208, 220, 255);
constexpr ImU32 kColTextDim = IM_COL32(130, 140, 155, 255);
constexpr ImU32 kColTextHot = IM_COL32(235, 240, 248, 255);
constexpr ImU32 kColBtnA = IM_COL32(45, 85, 145, 255);
constexpr ImU32 kColBtnB = IM_COL32(140, 85, 55, 255);

} // namespace

LibraryAction draw_library_panel(LibraryUiState& state, float width_px, float height_px,
                                 float scale) {
    LibraryAction action = LibraryAction::None;
    if (state.needs_refresh) {
        state.refresh();
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(mn, ImVec2(mn.x + width_px, mn.y + height_px), kColPanelBg);
    dl->AddRect(mn, ImVec2(mn.x + width_px, mn.y + height_px), kColBorder);

    // --- Cabecera: título + contador + toggle de lado ---------------------
    const float header_h = 28.0f * scale;
    dl->AddText(ImVec2(mn.x + 10.0f * scale, mn.y + 6.0f * scale), kColText, "BIBLIOTECA");
    const int total = (state.db != nullptr) ? state.db->count() : 0;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "(%d)", total);
    const ImVec2 cs = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(mn.x + width_px - 52.0f * scale - cs.x, mn.y + 6.0f * scale), kColTextDim,
                buf);

    const ImVec2 t0 = {mn.x + width_px - 44.0f * scale, mn.y + 4.0f * scale};
    const ImVec2 t1 = {mn.x + width_px - 8.0f * scale, mn.y + header_h - 4.0f * scale};
    dl->AddRectFilled(t0, t1, IM_COL32(50, 56, 68, 255), 3.0f);
    const char* toggle = state.on_left ? ">>" : "<<";
    const ImVec2 ts = ImGui::CalcTextSize(toggle);
    dl->AddText(ImVec2((t0.x + t1.x - ts.x) * 0.5f, (t0.y + t1.y - ts.y) * 0.5f), kColTextHot,
                toggle);
    if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(t0, t1)) {
        state.on_left = !state.on_left;
        action = LibraryAction::ToggleSide;
    }
    ImGui::Dummy(ImVec2(0.0f, header_h));

    // --- Buscador ----------------------------------------------------------
    const float pad = 8.0f * scale;
    const float inner_w = width_px - pad * 2.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
    ImGui::SetNextItemWidth(inner_w);
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
    // Widgets ImGui reales (Selectable + botones): visibles, con hover y
    // drag & drop. El bug F3.5 anterior eran rects manuales tapados por el
    // fondo opaco del hijo.
    const float footer_h = 26.0f * scale;
    const float table_h = mn.y + height_px - footer_h - ImGui::GetCursorScreenPos().y;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
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
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 22.0f * scale);

            // Cols 0-3: fila seleccionable (clic = seleccionar, doble = cargar A).
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            const bool sel = (i == state.selected);
            if (ImGui::Selectable(t.title.c_str(), sel,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0.0f, 18.0f * scale))) {
                state.selected = i;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    action = LibraryAction::LoadDeckA;
                }
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
                state.selected = i;
            }
            // Arrastrar la fila a un deck (payload con el índice).
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
            ImGui::TextDisabled("%s", cell);

            ImGui::TableSetColumnIndex(2);
            if (t.camelot > 0) {
                std::snprintf(cell, sizeof(cell), "%d%c", t.camelot, t.camelot_minor ? 'A' : 'B');
            } else {
                std::snprintf(cell, sizeof(cell), "--");
            }
            ImGui::TextColored(ImVec4(0.55f, 0.78f, 0.60f, 1.0f), "%s", cell);

            ImGui::TableSetColumnIndex(3);
            const int secs = static_cast<int>(t.duration_ms / 1000);
            std::snprintf(cell, sizeof(cell), "%d:%02d", secs / 60, secs % 60);
            ImGui::TextDisabled("%s", cell);

            // Col 4: cargar a A/B.
            ImGui::TableSetColumnIndex(4);
            ImGui::PushStyleColor(ImGuiCol_Button, kColBtnA);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(60, 105, 175, 255));
            if (ImGui::SmallButton("A")) {
                state.selected = i;
                action = LibraryAction::LoadDeckA;
            }
            ImGui::SameLine(0.0f, 3.0f * scale);
            ImGui::PopStyleColor(2);
            ImGui::PushStyleColor(ImGuiCol_Button, kColBtnB);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(165, 105, 70, 255));
            if (ImGui::SmallButton("B")) {
                state.selected = i;
                action = LibraryAction::LoadDeckB;
            }
            ImGui::PopStyleColor(2);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // --- Footer: [+ carpeta] + estado --------------------------------------
    const ImVec2 foot = {mn.x + pad, mn.y + height_px - footer_h + 2.0f * scale};
    ImGui::SetCursorScreenPos(foot);
    const ImVec2 f0 = ImGui::GetCursorScreenPos();
    ImGui::TextColored(state.status[0] != '\0' ? ImVec4(0.65f, 0.72f, 0.82f, 1.0f)
                                               : ImVec4(0.55f, 0.70f, 0.90f, 1.0f),
                       "[+ carpeta]");
    const ImVec2 f1 = ImGui::GetItemRectMax();
    if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(f0, f1)) {
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
