#include "library_panel.hpp"

#include "i18n.hpp"
#include "render/imgui_layer.hpp"
#include "theme.hpp"

#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace melodeus::app {

namespace {

/// Recorta `text` para que quepa en `max_w` px añadiendo "…" al final.
/// Búsqueda binaria sobre el índice de corte (O(log n) CalcTextSize).
std::string ellipsize(const char* text, float max_w, ImFont* font = nullptr, float font_size = 0.0f) {
    if (text == nullptr || max_w <= 0.0f) {
        return {};
    }
    const auto calc_w = [&](const char* s) -> float {
        if (font != nullptr && font_size > 0.0f) {
            return font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, s).x;
        }
        return ImGui::CalcTextSize(s).x;
    };
    if (calc_w(text) <= max_w) {
        return text;
    }
    const std::size_t len = std::strlen(text);
    std::size_t lo = 0;
    std::size_t hi = len;
    while (lo < hi) {
        const std::size_t mid = (lo + hi + 1) / 2;
        std::string tmp(text, mid);
        tmp += "\xE2\x80\xA6";
        if (calc_w(tmp.c_str()) <= max_w) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    std::string out(text, lo);
    out += "\xE2\x80\xA6";
    return out;
}

} // namespace

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

    // --- Cabecera: título teñido + pestañas (Local / YouTube / SoundCloud) + contador ---
    const float pad = 8.0f * scale;
    const float top_pad = 7.0f * scale;
    const float tab_h = 20.0f * scale;
    const ImU32 title_col = state.accent_a ? theme::accent_a() : theme::accent_b();
    float title_w = 0.0f;
    if (ImFont* title = imgui_font_title()) {
        const float title_y = mn.y + top_pad + (tab_h - title->LegacySize) * 0.5f;
        dl->AddText(title, title->LegacySize, ImVec2(mn.x + 10.0f * scale, title_y),
                    title_col, tr(Str::Library));
        title_w = title->CalcTextSizeA(title->LegacySize, FLT_MAX, 0.0f, tr(Str::Library)).x;
    } else {
        const float title_y = mn.y + top_pad + (tab_h - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(mn.x + 10.0f * scale, title_y), title_col, tr(Str::Library));
        title_w = ImGui::CalcTextSize(tr(Str::Library)).x;
    }

    // Pestañas integradas en la cabecera
    struct TabInfo {
        LibraryTab tab;
        const char* label;
        int source_id;
    };
    const TabInfo kTabs[] = {
        {LibraryTab::Local, "Local", 0},
        {LibraryTab::YouTube, "YouTube", 1},
        {LibraryTab::SoundCloud, "SoundCloud", 2},
    };

    float tab_x = mn.x + 10.0f * scale + title_w + 12.0f * scale;
    const float tab_y = mn.y + top_pad;

    for (int ti = 0; ti < 3; ++ti) {
        const auto& tinfo = kTabs[ti];
        const bool is_active = (state.active_tab == tinfo.tab);
        const int cnt = (state.db != nullptr) ? state.db->count_source(tinfo.source_id) : 0;
        char tab_text[48];
        if (cnt > 0) {
            std::snprintf(tab_text, sizeof(tab_text), "%s %d", tinfo.label, cnt);
        } else {
            std::snprintf(tab_text, sizeof(tab_text), "%s", tinfo.label);
        }
        const ImVec2 txt_sz = ImGui::CalcTextSize(tab_text);
        const float tab_w = txt_sz.x + 14.0f * scale;

        ImGui::SetCursorScreenPos(ImVec2(tab_x, tab_y));
        ImGui::PushID(ti + 100);
        if (ImGui::InvisibleButton("##lib_tab", ImVec2(tab_w, tab_h))) {
            if (state.active_tab != tinfo.tab) {
                state.active_tab = tinfo.tab;
                state.selected = -1;
                state.needs_refresh = true;
            }
        }
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();

        // Estilo visual de la pestaña
        ImU32 tab_bg, tab_bd, tab_fg;
        if (is_active) {
            if (tinfo.tab == LibraryTab::YouTube) {
                tab_bg = IM_COL32(180, 25, 25, 55);
                tab_bd = IM_COL32(255, 65, 55, 220);
                tab_fg = IM_COL32(255, 140, 130, 255);
            } else if (tinfo.tab == LibraryTab::SoundCloud) {
                tab_bg = IM_COL32(210, 75, 0, 55);
                tab_bd = IM_COL32(255, 120, 25, 220);
                tab_fg = IM_COL32(255, 180, 110, 255);
            } else {
                tab_bg = state.accent_a ? theme::accent_a_dim() : theme::accent_b_dim();
                tab_bd = state.accent_a ? theme::accent_a() : theme::accent_b();
                tab_fg = state.accent_a ? theme::accent_a() : theme::accent_b();
            }
        } else {
            tab_bg = hov ? IM_COL32(45, 54, 70, 150) : IM_COL32(25, 30, 42, 90);
            tab_bd = hov ? IM_COL32(90, 105, 135, 180) : theme::kGlassBorder;
            tab_fg = hov ? IM_COL32(210, 218, 235, 255) : theme::kTextDim;
        }

        dl->AddRectFilled(ImVec2(tab_x, tab_y), ImVec2(tab_x + tab_w, tab_y + tab_h), tab_bg, theme::kRadiusGrab);
        dl->AddRect(ImVec2(tab_x, tab_y), ImVec2(tab_x + tab_w, tab_y + tab_h), tab_bd, theme::kRadiusGrab, 0, is_active ? 1.5f : 1.0f);
        dl->AddText(ImVec2(tab_x + (tab_w - txt_sz.x) * 0.5f, tab_y + (tab_h - txt_sz.y) * 0.5f), tab_fg, tab_text);

        tab_x += tab_w + 4.0f * scale;
    }

    // Contador de la pestaña activa en el extremo derecho
    const int tab_total = (state.db != nullptr) ? state.db->count_source(static_cast<int>(state.active_tab)) : 0;
    char buf[96];
    if (state.search[0] != '\0' || state.selected_folder > 0) {
        std::snprintf(buf, sizeof(buf), "(%d/%d)", static_cast<int>(state.rows.size()), tab_total);
    } else {
        std::snprintf(buf, sizeof(buf), "(%d)", tab_total);
    }
    const ImVec2 cs = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(mx.x - pad - cs.x, mn.y + top_pad + (tab_h - cs.y) * 0.5f), theme::kTextDim, buf);

    // --- Herramientas según pestaña activa (espaciado limpio e integrado) ---
    const float inner_w = width_px - pad * 2.0f;
    const float toolbar_y = mn.y + top_pad + tab_h + 7.0f * scale;
    ImGui::SetCursorScreenPos(ImVec2(mn.x + pad, toolbar_y));

    if (state.active_tab == LibraryTab::Local) {
        // --- Pestaña Local: Buscador FTS5 + Selector de carpetas en UNA SOLA FILA compacta ---
        ImGui::SetCursorPosX(pad);
        const float gap = 5.0f * scale;
        const float add_btn_w = state.show_folder ? ImGui::GetFrameHeight() : 0.0f;
        const float combo_w = 135.0f * scale;
        const float search_w = inner_w - combo_w - (state.show_folder ? (add_btn_w + gap) : 0.0f) - gap;

        ImGui::SetNextItemWidth(search_w);
        if (state.focus_search) {
            ImGui::SetKeyboardFocusHere();
            state.focus_search = false;
        }
        if (ImGui::InputTextWithHint("##search", tr(Str::SearchHint), state.search,
                                     sizeof(state.search), ImGuiInputTextFlags_EnterReturnsTrue)) {
            state.needs_refresh = true;
        }
        if (ImGui::IsItemEdited()) {
            state.needs_refresh = true;
        }

        // Selector de Carpeta
        ImGui::SameLine(0.0f, gap);
        ImGui::SetNextItemWidth(combo_w);

        const char* current_f_label =
            (state.selected_folder == 0 ||
             state.selected_folder > static_cast<int>(state.folders.size()))
                ? tr(Str::AllFolders)
                : state.folders[static_cast<std::size_t>(state.selected_folder - 1)].c_str();

        if (ImGui::BeginCombo("##folder_combo", current_f_label)) {
            const bool is_all = (state.selected_folder == 0);
            if (ImGui::Selectable(tr(Str::AllFolders), is_all)) {
                state.selected_folder = 0;
                state.needs_refresh = true;
            }
            for (int fi = 0; fi < static_cast<int>(state.folders.size()); ++fi) {
                const bool is_sel = (state.selected_folder == fi + 1);
                if (ImGui::Selectable(state.folders[static_cast<std::size_t>(fi)].c_str(), is_sel)) {
                    state.selected_folder = fi + 1;
                    state.needs_refresh = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(Str::FilterFolderTooltip));
        }

        if (state.show_folder) {
            ImGui::SameLine(0.0f, gap);
            const float btn_size = ImGui::GetFrameHeight();
            if (ImGui::InvisibleButton("##add_folder", ImVec2(btn_size, btn_size))) {
                action = LibraryAction::AddFolder;
            }
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            if (hovered) {
                ImGui::SetTooltip("%s", tr(Str::AddFolderTooltip));
            }
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImU32 bg_col = active ? ImGui::GetColorU32(ImGuiCol_ButtonActive)
                                        : (hovered ? ImGui::GetColorU32(ImGuiCol_ButtonHovered)
                                                   : ImGui::GetColorU32(ImGuiCol_Button));
            dl->AddRectFilled(bmin, bmax, bg_col, theme::kRadiusGrab);
            dl->AddRect(bmin, bmax, theme::kGlassBorder, theme::kRadiusGrab, 0, 1.0f);

            // Icono vectorial nítido de carpeta con signo +
            const float cx = (bmin.x + bmax.x) * 0.5f;
            const float cy = (bmin.y + bmax.y) * 0.5f;
            const ImU32 icon_col = hovered ? IM_COL32(245, 248, 255, 255) : IM_COL32(180, 190, 210, 255);
            dl->AddLine(ImVec2(cx - 7.0f * scale, cy - 5.0f * scale),
                        ImVec2(cx - 2.0f * scale, cy - 5.0f * scale), icon_col, 1.5f * scale);
            dl->AddRect(ImVec2(cx - 7.0f * scale, cy - 3.5f * scale),
                        ImVec2(cx + 7.0f * scale, cy + 5.5f * scale), icon_col, 2.0f * scale, 0, 1.3f * scale);
            dl->AddLine(ImVec2(cx, cy - 1.0f * scale), ImVec2(cx, cy + 3.0f * scale), icon_col, 1.4f * scale);
            dl->AddLine(ImVec2(cx - 2.0f * scale, cy + 1.0f * scale), ImVec2(cx + 2.0f * scale, cy + 1.0f * scale),
                        icon_col, 1.4f * scale);
        }
    } else if (state.active_tab == LibraryTab::YouTube) {
        // --- Pestaña YouTube: Mini-buscador / Descarga directa + Sub-pills ---
        ImGui::SetCursorPosX(pad);
        const bool is_url = (std::string(state.search_yt).rfind("http://", 0) == 0 ||
                             std::string(state.search_yt).rfind("https://", 0) == 0);
        const float action_btn_w = 64.0f * scale;
        const float input_w = inner_w - action_btn_w - 6.0f * scale;
        ImGui::SetNextItemWidth(input_w);
        const bool enter_pressed = ImGui::InputTextWithHint(
            "##yt_search", tr(Str::SearchYtHint), state.search_yt, sizeof(state.search_yt),
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemEdited()) {
            if (state.subview_yt == StreamSubView::Downloaded) {
                state.needs_refresh = true;
            }
        }
        ImGui::SameLine(0.0f, 6.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(190, 35, 35, 180));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(230, 45, 45, 230));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 60, 60, 255));
        const char* btn_label = is_url ? tr(Str::Load) : tr(Str::SearchButton);
        if ((ImGui::Button(btn_label, ImVec2(action_btn_w, 0.0f)) || enter_pressed) && state.search_yt[0] != '\0') {
            if (is_url) {
                if (std::string(state.search_yt).find("soundcloud.com") != std::string::npos) {
                    std::snprintf(state.status, sizeof(state.status), "%s", "Enlace de SoundCloud \xE2\x80\x94 usa la pesta\xC3\xB1" "a SoundCloud");
                } else {
                    std::snprintf(state.url, sizeof(state.url), "%s", state.search_yt);
                    action = LibraryAction::LoadUrl;
                }
            } else {
                state.subview_yt = StreamSubView::OnlineResults;
                action = LibraryAction::StartOnlineSearch;
            }
        }
        ImGui::PopStyleColor(3);

        // Sub-selector (En Biblioteca vs Resultados en Línea)
        ImGui::SetCursorPosX(pad);
        const int yt_count = (state.db != nullptr) ? state.db->count_source(1) : 0;
        char pill_local_buf[64], pill_online_buf[64];
        std::snprintf(pill_local_buf, sizeof(pill_local_buf), "%s (%d)", tr(Str::InLibrary), yt_count);
        if (state.is_searching_online) {
            std::snprintf(pill_online_buf, sizeof(pill_online_buf), "%s", tr(Str::SearchingOnline));
        } else {
            std::snprintf(pill_online_buf, sizeof(pill_online_buf), "%s (%d)", tr(Str::OnlineResults),
                          static_cast<int>(state.online_results_yt.size()));
        }

        const bool is_sub_down = (state.subview_yt == StreamSubView::Downloaded);
        ImGui::PushStyleColor(ImGuiCol_Button, is_sub_down ? IM_COL32(190, 35, 35, 140) : IM_COL32(35, 40, 52, 100));
        if (ImGui::SmallButton(pill_local_buf)) {
            state.subview_yt = StreamSubView::Downloaded;
            state.needs_refresh = true;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine(0.0f, 6.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_Button, !is_sub_down ? IM_COL32(190, 35, 35, 140) : IM_COL32(35, 40, 52, 100));
        if (ImGui::SmallButton(pill_online_buf)) {
            state.subview_yt = StreamSubView::OnlineResults;
        }
        ImGui::PopStyleColor();
    } else if (state.active_tab == LibraryTab::SoundCloud) {
        // --- Pestaña SoundCloud: Mini-buscador / Descarga directa + Sub-pills ---
        ImGui::SetCursorPosX(pad);
        const bool is_url = (std::string(state.search_sc).rfind("http://", 0) == 0 ||
                             std::string(state.search_sc).rfind("https://", 0) == 0);
        const float action_btn_w = 64.0f * scale;
        const float input_w = inner_w - action_btn_w - 6.0f * scale;
        ImGui::SetNextItemWidth(input_w);
        const bool enter_pressed = ImGui::InputTextWithHint(
            "##sc_search", tr(Str::SearchScHint), state.search_sc, sizeof(state.search_sc),
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemEdited()) {
            if (state.subview_sc == StreamSubView::Downloaded) {
                state.needs_refresh = true;
            }
        }
        ImGui::SameLine(0.0f, 6.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(215, 75, 10, 180));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(245, 95, 20, 230));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 115, 30, 255));
        const char* btn_label = is_url ? tr(Str::Load) : tr(Str::SearchButton);
        if ((ImGui::Button(btn_label, ImVec2(action_btn_w, 0.0f)) || enter_pressed) && state.search_sc[0] != '\0') {
            if (is_url) {
                if (std::string(state.search_sc).find("youtube.com") != std::string::npos ||
                    std::string(state.search_sc).find("youtu.be") != std::string::npos) {
                    std::snprintf(state.status, sizeof(state.status), "%s", "Enlace de YouTube \xE2\x80\x94 usa la pesta\xC3\xB1" "a YouTube");
                } else {
                    std::snprintf(state.url, sizeof(state.url), "%s", state.search_sc);
                    action = LibraryAction::LoadUrl;
                }
            } else {
                state.subview_sc = StreamSubView::OnlineResults;
                action = LibraryAction::StartOnlineSearch;
            }
        }
        ImGui::PopStyleColor(3);

        // Sub-selector (En Biblioteca vs Resultados en Línea)
        ImGui::SetCursorPosX(pad);
        const int sc_count = (state.db != nullptr) ? state.db->count_source(2) : 0;
        char pill_local_buf[64], pill_online_buf[64];
        std::snprintf(pill_local_buf, sizeof(pill_local_buf), "%s (%d)", tr(Str::InLibrary), sc_count);
        if (state.is_searching_online) {
            std::snprintf(pill_online_buf, sizeof(pill_online_buf), "%s", tr(Str::SearchingOnline));
        } else {
            std::snprintf(pill_online_buf, sizeof(pill_online_buf), "%s (%d)", tr(Str::OnlineResults),
                          static_cast<int>(state.online_results_sc.size()));
        }

        const bool is_sub_down = (state.subview_sc == StreamSubView::Downloaded);
        ImGui::PushStyleColor(ImGuiCol_Button, is_sub_down ? IM_COL32(215, 75, 10, 140) : IM_COL32(35, 40, 52, 100));
        if (ImGui::SmallButton(pill_local_buf)) {
            state.subview_sc = StreamSubView::Downloaded;
            state.needs_refresh = true;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine(0.0f, 6.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_Button, !is_sub_down ? IM_COL32(215, 75, 10, 140) : IM_COL32(35, 40, 52, 100));
        if (ImGui::SmallButton(pill_online_buf)) {
            state.subview_sc = StreamSubView::OnlineResults;
        }
        ImGui::PopStyleColor();
    }

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f * scale);

    // --- Tabla de pistas (sortable) -----------------------------------------
    const float footer_h = 44.0f * scale;
    const float table_h = (mn.y + height_px - footer_h - 4.0f * scale) - ImGui::GetCursorScreenPos().y;
    ImGui::SetCursorPosX(pad);
    ImFont* mono = imgui_font_mono();

    // Limpieza perezosa del indicador de carga.
    if (state.loading_row >= 0 && ImGui::GetTime() >= state.loading_until) {
        state.loading_row = -1;
    }

    // Coordena X de la columna Título (para elipsis y contexto).
    const float table_top = ImGui::GetCursorScreenPos().y;

    // Hover más marcado para filas no seleccionadas (sólo dentro de la tabla).
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.659f, 0.333f, 0.969f, 0.50f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.659f, 0.333f, 0.969f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, ImVec4(50.0f / 255.0f, 58.0f / 255.0f, 75.0f / 255.0f, 0.85f));

    const bool is_online_view =
        (state.active_tab == LibraryTab::YouTube && state.subview_yt == StreamSubView::OnlineResults) ||
        (state.active_tab == LibraryTab::SoundCloud && state.subview_sc == StreamSubView::OnlineResults);

    if (is_online_view) {
        const auto& online_results = state.current_online_results();
        if (ImGui::BeginTable("##lib_online", 4,
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_BordersInnerV,
                              ImVec2(inner_w, table_h))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(tr(Str::Title), ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn(tr(Str::ChannelArtist), ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn(tr(Str::Time), ImGuiTableColumnFlags_WidthFixed, 48.0f * scale);
            ImGui::TableSetupColumn(tr(Str::Status), ImGuiTableColumnFlags_WidthFixed, 85.0f * scale);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.48f, 0.55f, 1.0f));
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor();

            if (state.is_searching_online) {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 32.0f * scale);
                ImGui::TableSetColumnIndex(0);
                const char* msg = (state.active_tab == LibraryTab::SoundCloud)
                                      ? "SoundCloud..."
                                      : "YouTube...";
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "%s %s", tr(Str::SearchingOnline), msg);
            } else if (online_results.empty()) {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 32.0f * scale);
                ImGui::TableSetColumnIndex(0);
                if (state.online_status_msg[0] != '\0') {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", state.online_status_msg);
                } else {
                    ImGui::TextDisabled("%s", tr(Str::EmptyOnlineHint));
                }
            } else {
                for (int i = 0; i < static_cast<int>(online_results.size()); ++i) {
                    const auto& item = online_results[static_cast<std::size_t>(i)];
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, 23.0f * scale);
                    const bool sel = (state.selected_online == i);

                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(i);

                    if (sel) {
                        ImDrawList* dlist = ImGui::GetWindowDrawList();
                        const ImVec2 row_min = ImGui::GetCursorScreenPos();
                        const float row_w = ImGui::GetContentRegionAvail().x;
                        const ImU32 row_acc = state.accent_a ? theme::accent_a() : theme::accent_b();
                        dlist->AddRectFilled(
                            row_min, ImVec2(row_min.x + row_w, row_min.y + 21.0f * scale),
                            (row_acc & 0x00FFFFFF) | (35 << 24));
                    }

                    if (ImGui::Selectable(item.title.c_str(), sel,
                                          ImGuiSelectableFlags_SpanAllColumns |
                                              ImGuiSelectableFlags_AllowDoubleClick,
                                          ImVec2(0.0f, 18.0f * scale))) {
                        state.selected_online = i;
                    }

                    // Drag & Drop
                    if (ImGui::BeginDragDropSource()) {
                        OnlineTrackDragPayload payload{i};
                        ImGui::SetDragDropPayload(kOnlineTrackDragType, &payload, sizeof(payload));
                        ImGui::Text("%s", item.title.c_str());
                        ImGui::EndDragDropSource();
                    }

                    // Doble clic: carga en el deck del panel activo
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        state.selected_online = i;
                        state.online_action_idx = i;
                        action = state.accent_a ? LibraryAction::LoadOnlineDeckA : LibraryAction::LoadOnlineDeckB;
                    }

                    // Menú contextual (clic derecho)
                    if (ImGui::BeginPopupContextItem()) {
                        state.selected_online = i;
                        state.online_action_idx = i;
                        if (ImGui::MenuItem(tr(Str::LoadDeckA))) {
                            action = LibraryAction::LoadOnlineDeckA;
                        }
                        if (ImGui::MenuItem(tr(Str::LoadDeckB))) {
                            action = LibraryAction::LoadOnlineDeckB;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem(tr(Str::DownloadTrack))) {
                            action = LibraryAction::DownloadOnlineOnly;
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(item.uploader.c_str());

                    ImGui::TableSetColumnIndex(2);
                    char tbuf[32];
                    const int secs = static_cast<int>(item.duration);
                    std::snprintf(tbuf, sizeof(tbuf), "%d:%02d", secs / 60, secs % 60);
                    const float col2_w = ImGui::GetContentRegionAvail().x;
                    const float txt2_w = mono != nullptr ? mono->CalcTextSizeA(mono->LegacySize, FLT_MAX, 0.0f, tbuf).x
                                                         : ImGui::CalcTextSize(tbuf).x;
                    if (col2_w > txt2_w) {
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col2_w - txt2_w) * 0.5f);
                    }
                    if (mono != nullptr) {
                        ImGui::PushFont(mono);
                        ImGui::TextDisabled("%s", tbuf);
                        ImGui::PopFont();
                    } else {
                        ImGui::TextDisabled("%s", tbuf);
                    }

                    ImGui::TableSetColumnIndex(3);
                    const char* st_txt = item.already_downloaded ? "[v] Guardado" : "[*] En nube";
                    const float col3_w = ImGui::GetContentRegionAvail().x;
                    const float txt3_w = ImGui::CalcTextSize(st_txt).x;
                    if (col3_w > txt3_w) {
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col3_w - txt3_w) * 0.5f);
                    }
                    if (item.already_downloaded) {
                        ImGui::TextColored(ImVec4(0.2f, 0.85f, 0.45f, 1.0f), "%s", st_txt);
                    } else {
                        ImGui::TextDisabled("%s", st_txt);
                    }

                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    } else if (ImGui::BeginTable("##lib", 6,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
                              ImGuiTableFlags_Sortable | ImGuiTableFlags_BordersInnerV,
                          ImVec2(inner_w, table_h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(tr(Str::Title),
                                ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide,
                                2.0f, ColTitle);
        ImGui::TableSetupColumn(tr(Str::Artist), ImGuiTableColumnFlags_WidthStretch, 1.2f, ColArtist);
        ImGui::TableSetupColumn(tr(Str::Album),
                                ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultHide,
                                1.0f, ColAlbum);
        ImGui::TableSetupColumn("BPM", ImGuiTableColumnFlags_WidthFixed, 45.0f * scale, ColBpm);
        ImGui::TableSetupColumn(tr(Str::Key), ImGuiTableColumnFlags_WidthFixed, 40.0f * scale, ColKey);
        ImGui::TableSetupColumn(tr(Str::Time), ImGuiTableColumnFlags_WidthFixed, 45.0f * scale, ColTime);

        // Procesar specs de ordenación según el ColumnUserID configurado.
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
            if (specs->SpecsDirty && specs->SpecsCount > 0) {
                const ImGuiTableColumnSortSpecs& s = specs->Specs[0];
                state.sort_col = static_cast<int>(s.ColumnUserID);
                state.sort_asc = s.SortDirection != ImGuiSortDirection_Descending;
                state.apply_sort();
                specs->SpecsDirty = false;
            }
        }

        // Cabeceras uppercase + dim (como la maqueta Tailwind).
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.48f, 0.55f, 1.0f));
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();

        for (int i = 0; i < static_cast<int>(state.rows.size()); ++i) {
            const library::TrackMeta& t = state.rows[static_cast<std::size_t>(i)];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 23.0f * scale);

            if (ImGui::TableSetColumnIndex(0)) {
                ImGui::PushID(i);
                const bool sel = (i == state.selected);

                if (sel) {
                    const ImU32 row_acc = state.accent_a ? theme::accent_a() : theme::accent_b();
                    const ImVec4 acc_v = ImGui::ColorConvertU32ToFloat4(row_acc);
                    ImGui::PushStyleColor(ImGuiCol_Header,
                                          ImVec4(acc_v.x, acc_v.y, acc_v.z, 0.28f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                          ImVec4(acc_v.x, acc_v.y, acc_v.z, 0.35f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                                          ImVec4(acc_v.x, acc_v.y, acc_v.z, 0.42f));
                }

                const float title_avail = std::max(60.0f, ImGui::GetContentRegionAvail().x - 12.0f * scale);
                const std::string title_disp = ellipsize(t.title.c_str(), title_avail);

                if (ImGui::Selectable(title_disp.c_str(), sel,
                                      ImGuiSelectableFlags_SpanAllColumns |
                                          ImGuiSelectableFlags_AllowDoubleClick,
                                      ImVec2(0.0f, 19.0f * scale))) {
                    state.selected = i;
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        action = state.accent_a ? LibraryAction::LoadDeckA : LibraryAction::LoadDeckB;
                    }
                }
                if (sel) {
                    ImGui::PopStyleColor(3);
                }

                // Capturar hover ANTES del popup (cambia el last item).
                const bool row_hover = ImGui::IsItemHovered();

                // Contexto (click derecho): cargar en A o B, o eliminar.
                if (ImGui::BeginPopupContextItem("##row_ctx")) {
                    if (ImGui::MenuItem(tr(Str::LoadDeckA), "1")) {
                        state.selected = i;
                        action = LibraryAction::LoadDeckA;
                    }
                    if (ImGui::MenuItem(tr(Str::LoadDeckB), "2")) {
                        state.selected = i;
                        action = LibraryAction::LoadDeckB;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(tr(Str::DeleteTrack), "Supr")) {
                        state.selected = i;
                        action = LibraryAction::DeleteTrack;
                    }
                    ImGui::EndPopup();
                }

                // Tooltip: artista / álbum / carpeta / formato.
                if (row_hover && (!t.artist.empty() || !t.album.empty() || t.bitrate > 0 || !t.uri.empty())) {
                    ImGui::BeginTooltip();
                    if (!t.title.empty()) {
                        ImGui::TextUnformatted(t.title.c_str());
                    }
                    if (!t.artist.empty()) {
                        ImGui::TextDisabled("Artista: %s", t.artist.c_str());
                    }
                    if (!t.album.empty()) {
                        ImGui::TextDisabled("Álbum: %s", t.album.c_str());
                    }
                    if (!t.uri.empty()) {
                        std::filesystem::path p(t.uri);
                        const std::string parent_name = p.parent_path().filename().string();
                        if (!parent_name.empty()) {
                            ImGui::TextDisabled("Carpeta: %s", parent_name.c_str());
                        }
                        ImGui::TextDisabled("Ruta: %s", t.uri.c_str());
                    }
                    if (t.bitrate > 0) {
                        ImGui::TextDisabled("%s \xC2\xB7 %d kbps",
                                            t.format.empty() ? "?" : t.format.c_str(), t.bitrate);
                    }
                    ImGui::EndTooltip();
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
                ImGui::PopID();
            }

            // Columna 1: Artista
            if (ImGui::TableSetColumnIndex(1)) {
                const float art_avail = std::max(40.0f, ImGui::GetContentRegionAvail().x - 6.0f * scale);
                const std::string art = ellipsize(t.artist.c_str(), art_avail);
                ImGui::TextDisabled("%s", art.empty() ? "--" : art.c_str());
            }

            // Columna 2: Álbum
            if (ImGui::TableSetColumnIndex(2)) {
                const float alb_avail = std::max(40.0f, ImGui::GetContentRegionAvail().x - 6.0f * scale);
                const std::string alb = ellipsize(t.album.c_str(), alb_avail);
                ImGui::TextDisabled("%s", alb.empty() ? "--" : alb.c_str());
            }

            char cell[64];
            // Columna 3: BPM (centrado)
            if (ImGui::TableSetColumnIndex(3)) {
                if (t.bpm > 0.0) {
                    std::snprintf(cell, sizeof(cell), "%.1f", t.bpm);
                } else {
                    std::snprintf(cell, sizeof(cell), "--");
                }
                const float col3_w = ImGui::GetContentRegionAvail().x;
                const float txt3_w = mono != nullptr ? mono->CalcTextSizeA(mono->LegacySize, FLT_MAX, 0.0f, cell).x
                                                     : ImGui::CalcTextSize(cell).x;
                if (col3_w > txt3_w) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col3_w - txt3_w) * 0.5f);
                }
                if (mono != nullptr) {
                    ImGui::PushFont(mono);
                    ImGui::TextDisabled("%s", cell);
                    ImGui::PopFont();
                } else {
                    ImGui::TextDisabled("%s", cell);
                }
            }

            // Columna 4: Key Camelot (centrado)
            if (ImGui::TableSetColumnIndex(4)) {
                const bool sel = (i == state.selected);
                if (t.camelot > 0) {
                    std::snprintf(cell, sizeof(cell), "%d%c", t.camelot, t.camelot_minor ? 'A' : 'B');
                } else {
                    std::snprintf(cell, sizeof(cell), "--");
                }
                const float col4_w = ImGui::GetContentRegionAvail().x;
                const float txt4_w = ImGui::CalcTextSize(cell).x;
                if (col4_w > txt4_w) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col4_w - txt4_w) * 0.5f);
                }
                if (sel) {
                    ImGui::TextColored(ImVec4(0.659f, 0.333f, 0.969f, 1.0f), "%s", cell);
                } else {
                    ImGui::TextColored(ImVec4(0.42f, 0.72f, 0.55f, 1.0f), "%s", cell);
                }
            }

            // Columna 5: Duración / Loading (centrado)
            if (ImGui::TableSetColumnIndex(5)) {
                const bool loading = (i == state.loading_row && ImGui::GetTime() < state.loading_until);
                if (loading) {
                    // Tres puntos animados en acento (staging / decodificación).
                    const int phase = static_cast<int>(ImGui::GetTime() * 3.0) % 3;
                    char dots[4] = "...";
                    dots[phase] = ' ';
                    const float col5_w = ImGui::GetContentRegionAvail().x;
                    const float txt5_w = ImGui::CalcTextSize(dots).x;
                    if (col5_w > txt5_w) {
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col5_w - txt5_w) * 0.5f);
                    }
                    ImGui::TextColored(ImVec4(0.659f, 0.333f, 0.969f, 1.0f), "%s", dots);
                } else {
                    const int secs = static_cast<int>(t.duration_ms / 1000);
                    std::snprintf(cell, sizeof(cell), "%d:%02d", secs / 60, secs % 60);
                    const float col5_w = ImGui::GetContentRegionAvail().x;
                    const float txt5_w = mono != nullptr ? mono->CalcTextSizeA(mono->LegacySize, FLT_MAX, 0.0f, cell).x
                                                         : ImGui::CalcTextSize(cell).x;
                    if (col5_w > txt5_w) {
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (col5_w - txt5_w) * 0.5f);
                    }
                    if (mono != nullptr) {
                        ImGui::PushFont(mono);
                        ImGui::TextDisabled("%s", cell);
                        ImGui::PopFont();
                    } else {
                        ImGui::TextDisabled("%s", cell);
                    }
                }
            }
        }
        ImGui::EndTable();
    }

    // Atajo Supr / Delete para eliminar la pista seleccionada de la biblioteca
    if (!is_online_view && state.selected >= 0 && state.selected < static_cast<int>(state.rows.size()) &&
        !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        action = LibraryAction::DeleteTrack;
    }

    ImGui::PopStyleColor(3); // hover fuerte + TableBorderLight

    // --- Estado vacío (sin resultados o biblioteca vacía) -------------------
    if (!is_online_view && state.rows.empty()) {
        const char* msg = nullptr;
        char qbuf[160];
        const int tab_cnt = (state.db != nullptr) ? state.db->count_source(static_cast<int>(state.active_tab)) : 0;
        if (tab_cnt == 0) {
            if (state.active_tab == LibraryTab::YouTube) {
                msg = "sin pistas de YouTube \xE2\x80\x94 busca o pega un enlace arriba";
            } else if (state.active_tab == LibraryTab::SoundCloud) {
                msg = "sin pistas de SoundCloud \xE2\x80\x94 busca o pega un enlace arriba";
            } else {
                msg = tr(Str::EmptyLibrary);
            }
        } else {
            const char* sq = (state.active_tab == LibraryTab::Local) ? state.search :
                             ((state.active_tab == LibraryTab::YouTube) ? state.search_yt : state.search_sc);
            if (sq[0] != '\0') {
                std::snprintf(qbuf, sizeof(qbuf), tr(Str::NoResults), sq);
                msg = qbuf;
            } else {
                msg = tr(Str::NoTracks);
            }
        }
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        const float cx = mn.x + width_px * 0.5f;
        const float cy = table_top + table_h * 0.45f;
        dl->AddText(ImVec2(cx - ts.x * 0.5f, cy), theme::kTextDim, msg);
    }

    // --- Footer de Dos Filas: Metadatos/Keycaps (Fila 1) + Estado del Sistema/Carga (Fila 2) ---
    const float footer_y = mn.y + height_px - footer_h;
    dl->AddLine(ImVec2(mn.x + pad, footer_y - 2.0f * scale), ImVec2(mx.x - pad, footer_y - 2.0f * scale),
                theme::kGlassBorder, 1.0f);

    ImFont* ui = imgui_font_ui();
    ImFont* bold = imgui_font_bold();
    ImFont* mono_font = imgui_font_mono();

    // =======================================================================
    // FILA 1: Contador de pistas (izq) | Micro-keycaps de navegación (der)
    // =======================================================================
    const float row1_y = footer_y + 2.0f * scale;
    const float row1_h = 18.0f * scale;

    // --- Izquierda Fila 1: Contador de pistas en badge sutil ---
    char count_buf[64];
    if (state.search[0] != '\0' || state.search_yt[0] != '\0' || state.search_sc[0] != '\0') {
        std::snprintf(count_buf, sizeof(count_buf), tr(Str::OfTracks),
                      static_cast<int>(state.rows.size()), tab_total);
    } else {
        std::snprintf(count_buf, sizeof(count_buf), tr(Str::Tracks),
                      static_cast<int>(state.rows.size()));
    }
    char full_count_str[96];
    std::snprintf(full_count_str, sizeof(full_count_str), "\xE2\x99\xAB  %s", count_buf);
    const ImVec2 count_sz = (ui != nullptr) ? ui->CalcTextSizeA(11.0f * scale, FLT_MAX, 0.0f, full_count_str)
                                            : ImGui::CalcTextSize(full_count_str);
    const float count_badge_w = count_sz.x + 12.0f * scale;
    const ImVec2 c_p0(mn.x + pad, row1_y + (row1_h - 18.0f * scale) * 0.5f);
    const ImVec2 c_p1(c_p0.x + count_badge_w, c_p0.y + 18.0f * scale);
    dl->AddRectFilled(c_p0, c_p1, IM_COL32(255, 255, 255, 8), 4.0f * scale);
    dl->AddRect(c_p0, c_p1, IM_COL32(255, 255, 255, 20), 4.0f * scale, 0, 1.0f);
    const ImVec2 c_tpos(c_p0.x + 6.0f * scale, c_p0.y + (18.0f * scale - count_sz.y) * 0.5f);
    if (ui != nullptr) {
        dl->AddText(ui, 11.0f * scale, c_tpos, theme::kTextDim, full_count_str);
    } else {
        dl->AddText(c_tpos, theme::kTextDim, full_count_str);
    }

    // --- Derecha Fila 1: Micro-keycaps navegación (sólo en biblioteca izquierda navegable) ---
    if (state.show_folder && width_px > 300.0f * scale) {
        struct KeycapItem {
            const char* key;
            const char* label;
        };
        const KeycapItem kKeycaps[] = {
            {"\xE2\x86\x91\xE2\x86\x93", tr(Str::NavMove)},
            {"\xE2\x86\xB5 A", tr(Str::NavDeckA)},
            {"\xE2\x87\xA7\xE2\x86\xB5 B", tr(Str::NavDeckB)},
            {"/", tr(Str::NavSearch)},
        };

        float cap_rx = mx.x - pad;
        const float cap_h = 16.0f * scale;
        const float cap_y = row1_y + (row1_h - cap_h) * 0.5f;

        for (int ki = 3; ki >= 0; --ki) {
            const auto& kc = kKeycaps[ki];
            const ImVec2 lbl_sz = (ui != nullptr) ? ui->CalcTextSizeA(10.5f * scale, FLT_MAX, 0.0f, kc.label)
                                                  : ImGui::CalcTextSize(kc.label);
            const ImVec2 key_sz = (mono_font != nullptr) ? mono_font->CalcTextSizeA(10.0f * scale, FLT_MAX, 0.0f, kc.key)
                                                         : ImGui::CalcTextSize(kc.key);
            const float key_pill_w = key_sz.x + 8.0f * scale;
            const float item_total_w = key_pill_w + 4.0f * scale + lbl_sz.x;

            if (cap_rx - item_total_w < c_p1.x + 10.0f * scale) {
                break;
            }

            cap_rx -= lbl_sz.x;
            const ImVec2 lpos(cap_rx, cap_y + (cap_h - lbl_sz.y) * 0.5f);
            if (ui != nullptr) {
                dl->AddText(ui, 10.5f * scale, lpos, IM_COL32(130, 138, 152, 255), kc.label);
            } else {
                dl->AddText(lpos, IM_COL32(130, 138, 152, 255), kc.label);
            }

            cap_rx -= (4.0f * scale + key_pill_w);
            const ImVec2 kp0(cap_rx, cap_y);
            const ImVec2 kp1(cap_rx + key_pill_w, cap_y + cap_h);
            dl->AddRectFilled(kp0, kp1, IM_COL32(255, 255, 255, 14), 3.0f * scale);
            dl->AddRect(kp0, kp1, IM_COL32(255, 255, 255, 32), 3.0f * scale, 0, 1.0f);

            const ImVec2 ktpos(kp0.x + 4.0f * scale, kp0.y + (cap_h - key_sz.y) * 0.5f);
            if (mono_font != nullptr) {
                dl->AddText(mono_font, 10.0f * scale, ktpos, theme::kTextHot, kc.key);
            } else {
                dl->AddText(ktpos, theme::kTextHot, kc.key);
            }

            cap_rx -= 10.0f * scale;
        }
    }

    // --- Separador interno entre Fila 1 y Fila 2 ---
    const float mid_sep_y = footer_y + 21.0f * scale;
    dl->AddLine(ImVec2(mn.x + pad, mid_sep_y), ImVec2(mx.x - pad, mid_sep_y),
                IM_COL32(255, 255, 255, 12), 1.0f);

    // =======================================================================
    // FILA 2: Estado del Sistema / Carga Activa a todo el ancho (Dedicated Row)
    // =======================================================================
    const float row2_y = footer_y + 24.0f * scale;
    const float row2_h = 18.0f * scale;

    float r2_cur_x = mn.x + pad;
    const float r2_avail_w = (mx.x - pad) - r2_cur_x;

    if (state.status[0] != '\0') {
        const char* s_str = state.status;
        const bool is_load_a = (std::strncmp(s_str, "cargando deck A <- ", 19) == 0);
        const bool is_load_b = (std::strncmp(s_str, "cargando deck B <- ", 19) == 0);
        const bool is_down = (std::strncmp(s_str, "descargando ", 12) == 0);
        const bool is_import = (std::strncmp(s_str, "importando ", 11) == 0);
        const bool is_error = (std::strstr(s_str, "fall\xC3\xB3") != nullptr || std::strstr(s_str, "error") != nullptr);

        if (is_load_a || is_load_b) {
            const char* badge_text = is_load_a ? tr(Str::StatusLoadingDeckA) : tr(Str::StatusLoadingDeckB);
            const ImU32 badge_accent = is_load_a ? theme::accent_a() : theme::accent_b();
            const ImU32 badge_accent_dim = is_load_a ? theme::accent_a_dim() : theme::accent_b_dim();
            const char* track_title = s_str + 19;

            const ImVec2 b_ts = (bold != nullptr) ? bold->CalcTextSizeA(10.0f * scale, FLT_MAX, 0.0f, badge_text)
                                                  : ImGui::CalcTextSize(badge_text);
            const float b_w = b_ts.x + 18.0f * scale;
            const ImVec2 bp0(r2_cur_x, row2_y + (row2_h - 18.0f * scale) * 0.5f);
            const ImVec2 bp1(bp0.x + b_w, bp0.y + 18.0f * scale);
            dl->AddRectFilled(bp0, bp1, badge_accent_dim, 4.0f * scale);
            dl->AddRect(bp0, bp1, badge_accent, 4.0f * scale, 0, 1.0f);
            dl->AddCircleFilled(ImVec2(bp0.x + 7.0f * scale, bp0.y + 9.0f * scale), 2.5f * scale, badge_accent);
            const ImVec2 btpos(bp0.x + 13.0f * scale, bp0.y + (18.0f * scale - b_ts.y) * 0.5f);
            if (bold != nullptr) {
                dl->AddText(bold, 10.0f * scale, btpos, badge_accent, badge_text);
            } else {
                dl->AddText(btpos, badge_accent, badge_text);
            }
            r2_cur_x += b_w + 8.0f * scale;

            const float title_avail = (mx.x - pad) - r2_cur_x;
            if (title_avail > 30.0f * scale) {
                const std::string disp_title = ellipsize(track_title, title_avail, ui, 12.0f * scale);
                const ImVec2 tt_sz = (ui != nullptr) ? ui->CalcTextSizeA(12.0f * scale, FLT_MAX, 0.0f, disp_title.c_str())
                                                     : ImGui::CalcTextSize(disp_title.c_str());
                const ImVec2 ttpos(r2_cur_x, row2_y + (row2_h - tt_sz.y) * 0.5f);
                if (ui != nullptr) {
                    dl->AddText(ui, 12.0f * scale, ttpos, theme::kTextHot, disp_title.c_str());
                } else {
                    dl->AddText(ttpos, theme::kTextHot, disp_title.c_str());
                }
            }
        } else if (is_down || is_import) {
            const char* badge_text = is_down ? tr(Str::StatusDownloading) : tr(Str::StatusImporting);
            const ImU32 badge_color = IM_COL32(0, 190, 255, 255);
            const ImU32 badge_dim = IM_COL32(0, 190, 255, 45);
            const char* rest_text = is_down ? (s_str + 12) : (s_str + 11);

            const ImVec2 b_ts = (bold != nullptr) ? bold->CalcTextSizeA(10.0f * scale, FLT_MAX, 0.0f, badge_text)
                                                  : ImGui::CalcTextSize(badge_text);
            const float b_w = b_ts.x + 18.0f * scale;
            const ImVec2 bp0(r2_cur_x, row2_y + (row2_h - 18.0f * scale) * 0.5f);
            const ImVec2 bp1(bp0.x + b_w, bp0.y + 18.0f * scale);
            dl->AddRectFilled(bp0, bp1, badge_dim, 4.0f * scale);
            dl->AddRect(bp0, bp1, badge_color, 4.0f * scale, 0, 1.0f);
            dl->AddCircleFilled(ImVec2(bp0.x + 7.0f * scale, bp0.y + 9.0f * scale), 2.5f * scale, badge_color);
            const ImVec2 btpos(bp0.x + 13.0f * scale, bp0.y + (18.0f * scale - b_ts.y) * 0.5f);
            if (bold != nullptr) {
                dl->AddText(bold, 10.0f * scale, btpos, badge_color, badge_text);
            } else {
                dl->AddText(btpos, badge_color, badge_text);
            }
            r2_cur_x += b_w + 8.0f * scale;

            const float text_avail = (mx.x - pad) - r2_cur_x;
            if (text_avail > 30.0f * scale) {
                const std::string disp_rest = ellipsize(rest_text, text_avail, ui, 12.0f * scale);
                const ImVec2 rt_sz = (ui != nullptr) ? ui->CalcTextSizeA(12.0f * scale, FLT_MAX, 0.0f, disp_rest.c_str())
                                                     : ImGui::CalcTextSize(disp_rest.c_str());
                const ImVec2 rtpos(r2_cur_x, row2_y + (row2_h - rt_sz.y) * 0.5f);
                if (ui != nullptr) {
                    dl->AddText(ui, 12.0f * scale, rtpos, theme::kTextHot, disp_rest.c_str());
                } else {
                    dl->AddText(rtpos, theme::kTextHot, disp_rest.c_str());
                }
            }
        } else if (is_error) {
            const ImVec2 e_ts = (bold != nullptr) ? bold->CalcTextSizeA(10.0f * scale, FLT_MAX, 0.0f, "ERROR")
                                                  : ImGui::CalcTextSize("ERROR");
            const float b_w = e_ts.x + 14.0f * scale;
            const ImVec2 bp0(r2_cur_x, row2_y + (row2_h - 18.0f * scale) * 0.5f);
            const ImVec2 bp1(bp0.x + b_w, bp0.y + 18.0f * scale);
            dl->AddRectFilled(bp0, bp1, IM_COL32(235, 60, 60, 45), 4.0f * scale);
            dl->AddRect(bp0, bp1, IM_COL32(235, 60, 60, 255), 4.0f * scale, 0, 1.0f);
            const ImVec2 btpos(bp0.x + 7.0f * scale, bp0.y + (18.0f * scale - e_ts.y) * 0.5f);
            if (bold != nullptr) {
                dl->AddText(bold, 10.0f * scale, btpos, IM_COL32(255, 100, 100, 255), "ERROR");
            } else {
                dl->AddText(btpos, IM_COL32(255, 100, 100, 255), "ERROR");
            }
            r2_cur_x += b_w + 8.0f * scale;

            const float text_avail = (mx.x - pad) - r2_cur_x;
            if (text_avail > 30.0f * scale) {
                const std::string disp_err = ellipsize(s_str, text_avail, ui, 12.0f * scale);
                const ImVec2 et_sz = (ui != nullptr) ? ui->CalcTextSizeA(12.0f * scale, FLT_MAX, 0.0f, disp_err.c_str())
                                                     : ImGui::CalcTextSize(disp_err.c_str());
                const ImVec2 etpos(r2_cur_x, row2_y + (row2_h - et_sz.y) * 0.5f);
                if (ui != nullptr) {
                    dl->AddText(ui, 12.0f * scale, etpos, IM_COL32(255, 140, 140, 255), disp_err.c_str());
                } else {
                    dl->AddText(etpos, IM_COL32(255, 140, 140, 255), disp_err.c_str());
                }
            }
        } else {
            const std::string disp_gen = ellipsize(s_str, r2_avail_w, ui, 12.0f * scale);
            const ImVec2 gt_sz = (ui != nullptr) ? ui->CalcTextSizeA(12.0f * scale, FLT_MAX, 0.0f, disp_gen.c_str())
                                                 : ImGui::CalcTextSize(disp_gen.c_str());
            const ImVec2 gtpos(r2_cur_x, row2_y + (row2_h - gt_sz.y) * 0.5f);
            if (ui != nullptr) {
                dl->AddText(ui, 12.0f * scale, gtpos, theme::kTextHot, disp_gen.c_str());
            } else {
                dl->AddText(gtpos, theme::kTextHot, disp_gen.c_str());
            }
        }
    } else {
        const char* ready_txt = tr(Str::StatusReady);
        const ImVec2 rt_sz = (ui != nullptr) ? ui->CalcTextSizeA(11.5f * scale, FLT_MAX, 0.0f, ready_txt)
                                             : ImGui::CalcTextSize(ready_txt);
        const ImVec2 rtpos(r2_cur_x, row2_y + (row2_h - rt_sz.y) * 0.5f);
        if (ui != nullptr) {
            dl->AddText(ui, 11.5f * scale, rtpos, theme::kTextDim, ready_txt);
        } else {
            dl->AddText(rtpos, theme::kTextDim, ready_txt);
        }

        const char* audio_txt = tr(Str::AudioOk);
        const ImVec2 at_sz = (ui != nullptr) ? ui->CalcTextSizeA(11.0f * scale, FLT_MAX, 0.0f, audio_txt)
                                             : ImGui::CalcTextSize(audio_txt);
        const float at_rx = mx.x - pad;
        const float at_w = at_sz.x + 14.0f * scale;
        const float at_x = at_rx - at_w;
        if (at_x > r2_cur_x + rt_sz.x + 16.0f * scale) {
            dl->AddCircleFilled(ImVec2(at_x + 4.0f * scale, row2_y + row2_h * 0.5f), 2.5f * scale,
                                IM_COL32(72, 200, 120, 255));
            const ImVec2 atpos(at_x + 10.0f * scale, row2_y + (row2_h - at_sz.y) * 0.5f);
            if (ui != nullptr) {
                dl->AddText(ui, 11.0f * scale, atpos, IM_COL32(140, 148, 162, 255), audio_txt);
            } else {
                dl->AddText(atpos, IM_COL32(140, 148, 162, 255), audio_txt);
            }
        }
    }

    // Fija el cursor al pie exacto del panel para no desbordar el contenedor child
    ImGui::SetCursorScreenPos(ImVec2(mn.x, mx.y - 1.0f));
    ImGui::Dummy(ImVec2(0.0f, 1.0f));

    return action;
}

} // namespace melodeus::app
