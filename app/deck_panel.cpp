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

} // namespace

DeckPanelOut draw_deck_panel(const DeckUiState& state, int deck_idx, float width_px, float scale) {
    DeckPanelOut out{};
    ImGui::PushID(deck_idx); // ##play/##cue/##pitch únicos por deck
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* mono = imgui_font_mono();
    const float header_h = 66.0f * scale;
    const float pitch_w = 40.0f * scale;
    const ImU32 accent = (deck_idx == 0) ? theme::kAccentA : theme::kAccentB;
    const ImU32 accent_dim = (deck_idx == 0) ? theme::kAccentADim : theme::kAccentBDim;

    // --- Header glass ------------------------------------------------------
    const ImVec2 header_min = ImGui::GetCursorScreenPos();
    const ImVec2 header_max = {header_min.x + width_px, header_min.y + header_h};
    theme::glass(dl, header_min, header_max);
    // Franja de acento inferior.
    dl->AddRectFilled(ImVec2(header_min.x + 12.0f * scale, header_max.y - 2.0f * scale),
                      ImVec2(header_max.x - 12.0f * scale, header_max.y), accent, 1.0f);

    // PLAY (botón real grande; verde cuando suena).
    const float btn_h = header_h - 18.0f * scale;
    ImGui::SetCursorScreenPos({header_min.x + 10.0f * scale, header_min.y + 9.0f * scale});
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

    // CUE (vuelve a 0; mantiene presionado = seek).
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

    // Pitch fader vertical ±8% (doble-click = reset a 0).
    const ImVec2 pf_min = {header_max.x - pitch_w, header_min.y + 6.0f * scale};
    const ImVec2 pf_max = {header_max.x - 8.0f * scale, header_max.y - 8.0f * scale};
    ImGui::SetCursorScreenPos(pf_min);
    float pitch = static_cast<float>(state.pitch_pct);
    ImGui::VSliderFloat("##pitch", ImVec2(pf_max.x - pf_min.x, pf_max.y - pf_min.y), &pitch, -8.0f,
                        8.0f, "%.1f%%", ImGuiSliderFlags_AlwaysClamp);
    if (pitch != static_cast<float>(state.pitch_pct)) {
        out.pitch_changed = true;
        out.pitch_new = pitch;
    }

    // BPM gigante (mono) + pitched.
    float x = ImGui::GetCursorScreenPos().x + 16.0f * scale;
    const float pitch_right = pf_min.x - 8.0f * scale;
    char buf[96];
    const float big = (mono != nullptr ? mono->LegacySize : 15.0f) * 1.85f;
    if (state.bpm > 0.0) {
        std::snprintf(buf, sizeof(buf), "%.1f", state.bpm);
    } else {
        std::snprintf(buf, sizeof(buf), "--.-");
    }
    const ImVec2 bpm_pos = {x, header_min.y + 8.0f * scale};
    if (mono != nullptr) {
        dl->AddText(mono, big, bpm_pos, theme::kTextHot, buf);
    } else {
        dl->AddText(bpm_pos, theme::kTextHot, buf);
    }
    dl->AddText(ImVec2(x, header_min.y + 42.0f * scale), theme::kTextDim,
                (state.pitch_pct > 0.01 || state.pitch_pct < -0.01) ? "BPM pitched" : "BPM");
    if (state.pitch_pct > 0.01 || state.pitch_pct < -0.01) {
        std::snprintf(buf, sizeof(buf), "%.1f", state.bpm_pitched);
        dl->AddText(ImVec2(x + 64.0f * scale, header_min.y + 18.0f * scale), accent, buf);
    }

    // Key badge (glass mini).
    x += 116.0f * scale;
    const ImVec2 key0 = {x, header_min.y + 10.0f * scale};
    const ImVec2 key1 = {x + 54.0f * scale, header_min.y + 42.0f * scale};
    if (state.camelot > 0) {
        std::snprintf(buf, sizeof(buf), "%d%c", state.camelot, state.camelot_minor ? 'A' : 'B');
        dl->AddRectFilled(key0, key1, accent_dim, theme::kRadiusControl);
        dl->AddRect(key0, key1, accent, theme::kRadiusControl, 0, 1.0f);
        const ImVec2 ks = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2((key0.x + key1.x - ks.x) * 0.5f, key0.y + 6.0f * scale), theme::kTextHot,
                    buf);
        if (state.key_name != nullptr && state.key_name[0] != '\0') {
            dl->AddText(ImVec2(x, key1.y + 1.0f * scale), theme::kTextDim, state.key_name);
        }
    } else {
        dl->AddText(ImVec2(x, header_min.y + 20.0f * scale), theme::kTextDim, "--");
    }

    // Tiempo transcurrido / restante (mono).
    x += 74.0f * scale;
    char t0[16];
    char t1[16];
    format_time(t0, sizeof(t0), state.position_sec);
    format_time(t1, sizeof(t1), state.duration_sec - state.position_sec);
    std::snprintf(buf, sizeof(buf), "%s", t0);
    const float time_size = (mono != nullptr ? mono->LegacySize : 15.0f) * 1.2f;
    if (mono != nullptr) {
        dl->AddText(mono, time_size, ImVec2(x, header_min.y + 12.0f * scale), theme::kText, buf);
    } else {
        dl->AddText(ImVec2(x, header_min.y + 12.0f * scale), theme::kText, buf);
    }
    std::snprintf(buf, sizeof(buf), "-%s", t1);
    dl->AddText(ImVec2(x, header_min.y + 42.0f * scale), theme::kTextDim, buf);

    // Título/artista (una línea, recortado).
    std::snprintf(buf, sizeof(buf), "%s%s%s", (state.title != nullptr) ? state.title : "",
                  (state.artist != nullptr && state.artist[0] != '\0') ? " — " : "",
                  (state.artist != nullptr) ? state.artist : "");
    const float title_x = x + 96.0f * scale;
    if (title_x < pitch_right - 12.0f * scale) {
        dl->PushClipRect(ImVec2(title_x, header_min.y),
                         ImVec2(pitch_right, header_min.y + 36.0f * scale), true);
        dl->AddText(ImVec2(title_x, header_min.y + 18.0f * scale), theme::kText, buf);
        dl->PopClipRect();
    }

    ImGui::Dummy(ImVec2(0.0f, header_h));

    // --- Carriles de waveform (drop targets glass) --------------------------
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
    out.rects.overview = lane("##ov", 40.0f * scale, IM_COL32(18, 20, 27, 160));
    out.rects.detail = lane("##dt", 110.0f * scale, IM_COL32(16, 18, 25, 190));
    ImGui::PopID();
    return out;
}

} // namespace melodeus::app
