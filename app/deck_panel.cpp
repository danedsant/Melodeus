#include "deck_panel.hpp"

#include "library_panel.hpp"
#include "render/imgui_layer.hpp"
#include "theme.hpp"

#include "imgui.h"

#include <cstdio>

namespace melodeus::app {

namespace {

void format_time(char* buf, std::size_t cap, double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int total = static_cast<int>(seconds);
    std::snprintf(buf, cap, "%d:%02d", total / 60, total % 60);
}

/// Drop target genérico sobre el último ítem: acepta un payload de la
/// biblioteca y devuelve la fila (o -1).
int accept_track_drop() {
    int drop_row = -1;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kTrackDragType)) {
            IM_ASSERT(payload->DataSize == sizeof(TrackDragPayload));
            drop_row = static_cast<const TrackDragPayload*>(payload->Data)->row;
        }
        ImGui::EndDragDropTarget();
    }
    return drop_row;
}

/// Franja de acento inferior de un panel glass (1px), centrada con margen.
void accent_stripe(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float scale, ImU32 accent) {
    dl->AddRectFilled(ImVec2(mn.x + 12.0f * scale, mx.y - 2.0f * scale),
                      ImVec2(mx.x - 12.0f * scale, mx.y), accent, 1.0f);
}

} // namespace

DeckPanelOut draw_deck_panel(const DeckUiState& state, int deck_idx, float width_px, float scale) {
    DeckPanelOut out{};
    ImGui::PushID(deck_idx); // ##play/##cue/##pitch únicos por deck
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* mono = imgui_font_mono();
    const ImU32 accent = (deck_idx == 0) ? theme::kAccentA : theme::kAccentB;
    const ImU32 accent_dim = (deck_idx == 0) ? theme::kAccentADim : theme::kAccentBDim;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    char buf[96];

    // =======================================================================
    // 1. DISPLAY (glass): título, BPM gigante, key, tiempo. Sin botones.
    // =======================================================================
    const float display_h = 58.0f * scale;
    const ImVec2 disp_min = origin;
    const ImVec2 disp_max = {origin.x + width_px, origin.y + display_h};
    theme::glass(dl, disp_min, disp_max);
    accent_stripe(dl, disp_min, disp_max, scale, accent);

    float x = disp_min.x + 14.0f * scale;
    const float big = (mono != nullptr ? mono->LegacySize : 15.0f) * 1.7f;
    if (state.bpm > 0.0) {
        std::snprintf(buf, sizeof(buf), "%.1f", state.bpm);
    } else {
        std::snprintf(buf, sizeof(buf), "--.-");
    }
    const ImVec2 bpm_pos = {x, disp_min.y + 8.0f * scale};
    if (mono != nullptr) {
        dl->AddText(mono, big, bpm_pos, theme::kTextHot, buf);
    } else {
        dl->AddText(bpm_pos, theme::kTextHot, buf);
    }
    dl->AddText(ImVec2(x, disp_min.y + 40.0f * scale), theme::kTextDim,
                (state.pitch_pct > 0.01 || state.pitch_pct < -0.01) ? "BPM pitched" : "BPM");
    if (state.pitch_pct > 0.01 || state.pitch_pct < -0.01) {
        std::snprintf(buf, sizeof(buf), "%.1f", state.bpm_pitched);
        dl->AddText(ImVec2(x + 58.0f * scale, disp_min.y + 18.0f * scale), accent, buf);
    }

    // Key badge (glass mini).
    x += 104.0f * scale;
    const ImVec2 key0 = {x, disp_min.y + 10.0f * scale};
    const ImVec2 key1 = {x + 50.0f * scale, disp_min.y + 38.0f * scale};
    if (state.camelot > 0) {
        std::snprintf(buf, sizeof(buf), "%d%c", state.camelot, state.camelot_minor ? 'A' : 'B');
        dl->AddRectFilled(key0, key1, accent_dim, theme::kRadiusControl);
        dl->AddRect(key0, key1, accent, theme::kRadiusControl, 0, 1.0f);
        const ImVec2 ks = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2((key0.x + key1.x - ks.x) * 0.5f, key0.y + 5.0f * scale), theme::kTextHot,
                    buf);
        if (state.key_name != nullptr && state.key_name[0] != '\0') {
            dl->AddText(ImVec2(x, key1.y + 1.0f * scale), theme::kTextDim, state.key_name);
        }
    } else {
        dl->AddText(ImVec2(x, disp_min.y + 18.0f * scale), theme::kTextDim, "--");
    }

    // Tiempo transcurrido / restante (mono).
    x += 66.0f * scale;
    char t0[16];
    char t1[16];
    format_time(t0, sizeof(t0), state.position_sec);
    format_time(t1, sizeof(t1), state.duration_sec - state.position_sec);
    const float time_size = (mono != nullptr ? mono->LegacySize : 15.0f) * 1.15f;
    if (mono != nullptr) {
        dl->AddText(mono, time_size, ImVec2(x, disp_min.y + 10.0f * scale), theme::kText, t0);
    } else {
        dl->AddText(ImVec2(x, disp_min.y + 10.0f * scale), theme::kText, t0);
    }
    std::snprintf(buf, sizeof(buf), "-%s", t1);
    dl->AddText(ImVec2(x, disp_min.y + 36.0f * scale), theme::kTextDim, buf);

    // Título/artista (una línea, recortado al resto del ancho).
    std::snprintf(buf, sizeof(buf), "%s%s%s", (state.title != nullptr) ? state.title : "",
                  (state.artist != nullptr && state.artist[0] != '\0') ? " — " : "",
                  (state.artist != nullptr) ? state.artist : "");
    const float title_x = x + 84.0f * scale;
    const float title_right = disp_max.x - 14.0f * scale;
    if (title_x < title_right) {
        dl->PushClipRect(ImVec2(title_x, disp_min.y), ImVec2(title_right, disp_min.y + display_h),
                         true);
        dl->AddText(ImVec2(title_x, disp_min.y + 16.0f * scale), theme::kText, buf);
        dl->PopClipRect();
    }
    ImGui::Dummy(ImVec2(0.0f, display_h));

    // =======================================================================
    // 2. WAVEFORM (2 carriles glass, drop targets de la biblioteca).
    // =======================================================================
    const float gap = 6.0f * scale;
    ImGui::Dummy(ImVec2(0.0f, gap));
    const auto lane = [&](const char* id, float h, ImU32 bg) {
        const ImVec2 mn2 = ImGui::GetCursorScreenPos();
        const ImVec2 mx2 = {mn2.x + width_px, mn2.y + h};
        const bool hover = ImGui::IsMouseHoveringRect(mn2, mx2);
        dl->AddRectFilled(mn2, mx2, bg, theme::kRadiusControl);
        dl->AddRect(mn2, mx2, hover ? accent : theme::kGlassBorder, theme::kRadiusControl, 0,
                    hover ? 2.0f : 1.0f);
        ImGui::SetCursorScreenPos(mn2);
        ImGui::InvisibleButton(id, ImVec2(width_px, h));
        const int drop = accept_track_drop();
        if (drop >= 0 && out.drop_row < 0) {
            out.drop_row = drop;
        }
        return RECT{static_cast<LONG>(mn2.x), static_cast<LONG>(mn2.y), static_cast<LONG>(mx2.x),
                    static_cast<LONG>(mx2.y)};
    };
    out.rects.overview = lane("##ov", 34.0f * scale, IM_COL32(18, 20, 27, 160));
    ImGui::Dummy(ImVec2(0.0f, gap));
    out.rects.detail = lane("##dt", 96.0f * scale, IM_COL32(16, 18, 25, 190));

    // =======================================================================
    // 3. CONTROLES (glass): PLAY, CUE, pitch fader.
    // =======================================================================
    ImGui::Dummy(ImVec2(0.0f, gap));
    const float ctrl_h = 56.0f * scale;
    const ImVec2 ctrl_min = ImGui::GetCursorScreenPos();
    const ImVec2 ctrl_max = {ctrl_min.x + width_px, ctrl_min.y + ctrl_h};
    theme::glass(dl, ctrl_min, ctrl_max);

    // PLAY (botón real grande; verde cuando suena).
    const float btn_h = ctrl_h - 16.0f * scale;
    ImGui::SetCursorScreenPos({ctrl_min.x + 10.0f * scale, ctrl_min.y + 8.0f * scale});
    const std::string play_icon =
        theme::icons::utf8(state.playing ? theme::icons::pause : theme::icons::play);
    if (state.playing) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(45, 145, 75, 220));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(60, 170, 90, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70, 190, 105, 255));
    }
    if (ImGui::Button(play_icon.c_str(), ImVec2(46.0f * scale, btn_h))) {
        out.play_toggled = true;
    }
    if (state.playing) {
        ImGui::PopStyleColor(3);
    }

    // CUE (vuelve a 0; ámbar).
    ImGui::SameLine(0.0f, 5.0f * scale);
    const std::string cue_icon = theme::icons::utf8(theme::icons::stop_circle);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(110, 88, 48, 160));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(150, 120, 60, 220));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(190, 155, 80, 255));
    if (ImGui::Button(cue_icon.c_str(), ImVec2(46.0f * scale, btn_h))) {
        out.cue_pressed = true;
    }
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("CUE — volver al inicio");
    }

    // Etiqueta del deck junto a los botones.
    ImGui::SameLine(0.0f, 8.0f * scale);
    dl->AddText(ImVec2(ImGui::GetCursorScreenPos().x, ctrl_min.y + ctrl_h * 0.5f - 8.0f * scale),
                accent, (deck_idx == 0) ? "DECK A" : "DECK B");

    // Pitch fader vertical ±8% a la derecha (doble-click = reset).
    const float pitch_w = 34.0f * scale;
    const ImVec2 pf_min = {ctrl_max.x - pitch_w - 8.0f * scale, ctrl_min.y + 6.0f * scale};
    const ImVec2 pf_max = {ctrl_max.x - 8.0f * scale, ctrl_max.y - 6.0f * scale};
    ImGui::SetCursorScreenPos(pf_min);
    float pitch = static_cast<float>(state.pitch_pct);
    ImGui::VSliderFloat("##pitch", ImVec2(pf_max.x - pf_min.x, pf_max.y - pf_min.y), &pitch, -8.0f,
                        8.0f, "%.1f%%", ImGuiSliderFlags_AlwaysClamp);
    if (pitch != static_cast<float>(state.pitch_pct)) {
        out.pitch_changed = true;
        out.pitch_new = pitch;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("PITCH — arrastrar (doble-click = 0)");
    }

    ImGui::Dummy(ImVec2(0.0f, ctrl_h));
    ImGui::PopID();
    return out;
}

} // namespace melodeus::app
