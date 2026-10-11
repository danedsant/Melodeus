#include "library_panel.hpp"

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
    dl->AddRectFilled(mn, ImVec2(mn.x + width_px, mn.y + height_px), IM_COL32(20, 22, 28, 255));
    dl->AddRect(mn, ImVec2(mn.x + width_px, mn.y + height_px), IM_COL32(45, 50, 60, 255));

    // Cabecera: título + contador + toggle de lado.
    const float header_h = 28.0f * scale;
    dl->AddText(ImVec2(mn.x + 10.0f * scale, mn.y + 6.0f * scale), IM_COL32(200, 208, 220, 255),
                "BIBLIOTECA");
    const int total = (state.db != nullptr) ? state.db->count() : 0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "(%d)", total);
    const ImVec2 cs = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(mn.x + width_px - 52.0f * scale - cs.x, mn.y + 6.0f * scale),
                IM_COL32(130, 140, 155, 255), buf);

    // Botón toggle (↔).
    const ImVec2 t0 = {mn.x + width_px - 44.0f * scale, mn.y + 4.0f * scale};
    const ImVec2 t1 = {mn.x + width_px - 8.0f * scale, mn.y + header_h - 4.0f * scale};
    dl->AddRectFilled(t0, t1, IM_COL32(50, 56, 68, 255), 3.0f);
    const char* toggle = state.on_left ? ">>" : "<<";
    const ImVec2 ts = ImGui::CalcTextSize(toggle);
    dl->AddText(ImVec2((t0.x + t1.x - ts.x) * 0.5f, (t0.y + t1.y - ts.y) * 0.5f),
                IM_COL32(210, 216, 226, 255), toggle);
    if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(t0, t1)) {
        state.on_left = !state.on_left;
        action = LibraryAction::ToggleSide;
    }

    ImGui::Dummy(ImVec2(0.0f, header_h));

    // Buscador.
    const ImVec2 search_mn = ImGui::GetCursorScreenPos();
    const float search_h = 26.0f * scale;
    dl->AddRectFilled(search_mn,
                      ImVec2(search_mn.x + width_px - 16.0f * scale, search_mn.y + search_h),
                      IM_COL32(30, 33, 40, 255), 3.0f);
    ImGui::SetCursorScreenPos(ImVec2(search_mn.x + 8.0f * scale, search_mn.y + 3.0f * scale));
    ImGui::SetNextItemWidth(width_px - 32.0f * scale);
    if (ImGui::InputTextWithHint("##search", "buscar (titulo, artista, album)...", state.search,
                                 sizeof(state.search), ImGuiInputTextFlags_EnterReturnsTrue)) {
        state.needs_refresh = true;
    }
    if (ImGui::IsItemEdited()) {
        state.needs_refresh = true;
    }
    ImGui::SetCursorScreenPos(ImVec2(search_mn.x, search_mn.y + search_h + 4.0f * scale));

    // Tabla: Title | BPM | Key | Time | decks.
    const float table_top = ImGui::GetCursorScreenPos().y;
    const float footer_h = 26.0f * scale;
    const float table_h = mn.y + height_px - footer_h - table_top;
    const float row_h = 22.0f * scale;
    const float col_bpm = 56.0f * scale;
    const float col_key = 44.0f * scale;
    const float col_time = 52.0f * scale;
    const float col_decks = 40.0f * scale;
    const float col_title = width_px - 16.0f * scale - col_bpm - col_key - col_time - col_decks;

    ImGui::SetCursorScreenPos(ImVec2(mn.x + 8.0f * scale, table_top));
    ImGui::BeginChild("##libtable", ImVec2(width_px - 16.0f * scale, table_h), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    {
        const int visible = static_cast<int>(table_h / row_h) + 2;
        for (int i = 0; i < static_cast<int>(state.rows.size()); ++i) {
            const library::TrackMeta& t = state.rows[static_cast<std::size_t>(i)];
            const bool sel = (i == state.selected);
            const ImVec2 r0 = ImGui::GetCursorScreenPos();
            const ImVec2 r1 = {r0.x + width_px - 24.0f * scale, r0.y + row_h};
            if (sel) {
                dl->AddRectFilled(r0, r1, IM_COL32(45, 70, 110, 255));
            } else if (i % 2 == 0) {
                dl->AddRectFilled(r0, r1, IM_COL32(26, 29, 36, 255));
            }

            // Nombre (clip a una línea).
            dl->PushClipRect(r0, ImVec2(r0.x + col_title - 4.0f, r1.y), true);
            dl->AddText(ImVec2(r0.x + 6.0f, r0.y + 3.0f), IM_COL32(220, 226, 236, 255),
                        t.title.c_str());
            dl->PopClipRect();

            char rowbuf[64];
            if (t.bpm > 0.0) {
                std::snprintf(rowbuf, sizeof(rowbuf), "%.1f", t.bpm);
            } else {
                std::snprintf(rowbuf, sizeof(rowbuf), "--");
            }
            dl->AddText(ImVec2(r0.x + col_title, r0.y + 3.0f), IM_COL32(170, 195, 225, 255),
                        rowbuf);

            if (t.camelot > 0) {
                std::snprintf(rowbuf, sizeof(rowbuf), "%d%c", t.camelot,
                              t.camelot_minor ? 'A' : 'B');
            } else {
                std::snprintf(rowbuf, sizeof(rowbuf), "--");
            }
            dl->AddText(ImVec2(r0.x + col_title + col_bpm, r0.y + 3.0f),
                        IM_COL32(170, 210, 175, 255), rowbuf);

            const int secs = static_cast<int>(t.duration_ms / 1000);
            std::snprintf(rowbuf, sizeof(rowbuf), "%d:%02d", secs / 60, secs % 60);
            dl->AddText(ImVec2(r0.x + col_title + col_bpm + col_key, r0.y + 3.0f),
                        IM_COL32(150, 158, 170, 255), rowbuf);

            // Botones A/B.
            const ImVec2 ba0 = {r1.x - col_decks + 2.0f, r0.y + 2.0f};
            const ImVec2 ba1 = {ba0.x + 17.0f * scale, r1.y - 2.0f};
            const ImVec2 bb0 = {ba1.x + 2.0f, ba0.y};
            const ImVec2 bb1 = {bb0.x + 17.0f * scale, ba1.y};
            dl->AddRectFilled(ba0, ba1, IM_COL32(55, 95, 150, 255), 2.0f);
            dl->AddRectFilled(bb0, bb1, IM_COL32(130, 80, 60, 255), 2.0f);
            const ImVec2 asz = ImGui::CalcTextSize("A");
            const ImVec2 bsz = ImGui::CalcTextSize("B");
            dl->AddText(ImVec2((ba0.x + ba1.x - asz.x) * 0.5f, (ba0.y + ba1.y - asz.y) * 0.5f),
                        IM_COL32(235, 240, 248, 255), "A");
            dl->AddText(ImVec2((bb0.x + bb1.x - bsz.x) * 0.5f, (bb0.y + bb1.y - bsz.y) * 0.5f),
                        IM_COL32(235, 240, 248, 255), "B");

            // Hit-testing (la tabla es decorativa; capturamos el ratón aquí).
            ImGui::SetCursorScreenPos(r0);
            ImGui::Dummy(ImVec2(width_px - 24.0f * scale, row_h));
            if (ImGui::IsItemHovered()) {
                if (ImGui::IsMouseClicked(1)) {
                    state.selected = i;
                } else if (ImGui::IsMouseDoubleClicked(0)) {
                    state.selected = i;
                    action = LibraryAction::LoadDeckA;
                }
            }
            if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(ba0, ba1)) {
                state.selected = i;
                action = LibraryAction::LoadDeckA;
            }
            if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(bb0, bb1)) {
                state.selected = i;
                action = LibraryAction::LoadDeckB;
            }
        }
        (void)visible;
    }
    ImGui::EndChild();

    // Footer: "+ carpeta" (clic real) + progreso/filas.
    const ImVec2 foot = {mn.x + 8.0f * scale, mn.y + height_px - footer_h + 2.0f * scale};
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
