#include "deck_panel.hpp"

#include "library_panel.hpp"

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
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float header_h = 64.0f * scale;
    const float pitch_w = 44.0f * scale;
    const ImU32 accent =
        (deck_idx == 0) ? IM_COL32(70, 130, 210, 255) : IM_COL32(210, 130, 70, 255);
    const ImU32 accent_dim =
        (deck_idx == 0) ? IM_COL32(40, 70, 120, 255) : IM_COL32(120, 70, 40, 255);

    // --- Header -----------------------------------------------------------
    const ImVec2 header_min = ImGui::GetCursorScreenPos();
    const ImVec2 header_max = {header_min.x + width_px, header_min.y + header_h};
    dl->AddRectFilled(header_min, header_max, IM_COL32(24, 27, 34, 255));
    dl->AddRectFilled(ImVec2(header_min.x, header_max.y - 2.0f * scale), header_max, accent);

    // PLAY grande.
    const ImVec2 play_min = {header_min.x + 10.0f * scale, header_min.y + 8.0f * scale};
    const ImVec2 play_max = {play_min.x + 48.0f * scale, header_max.y - 10.0f * scale};
    const bool play_hover = ImGui::IsMouseHoveringRect(play_min, play_max);
    const ImU32 play_col =
        state.playing ? (play_hover ? IM_COL32(60, 170, 90, 255) : IM_COL32(45, 145, 75, 255))
                      : (play_hover ? IM_COL32(65, 72, 86, 255) : IM_COL32(48, 53, 64, 255));
    dl->AddRectFilled(play_min, play_max, play_col, 5.0f * scale);
    dl->AddRect(play_min, play_max, accent, 5.0f * scale);
    const char* play_glyph = state.playing ? "||" : ">";
    const ImVec2 gs = ImGui::CalcTextSize(play_glyph);
    dl->AddText(
        ImVec2((play_min.x + play_max.x - gs.x) * 0.5f, (play_min.y + play_max.y - gs.y) * 0.5f),
        IM_COL32(240, 244, 250, 255), play_glyph);
    ImGui::SetCursorScreenPos(play_min);
    ImGui::InvisibleButton("##play", ImVec2(play_max.x - play_min.x, play_max.y - play_min.y));
    if (ImGui::IsItemClicked()) {
        out.play_toggled = true;
    }

    // CUE (vuelve a 0).
    const ImVec2 cue_min = {play_max.x + 6.0f * scale, play_min.y};
    const ImVec2 cue_max = {cue_min.x + 36.0f * scale, play_max.y};
    const bool cue_hover = ImGui::IsMouseHoveringRect(cue_min, cue_max);
    dl->AddRectFilled(cue_min, cue_max,
                      cue_hover ? IM_COL32(150, 120, 50, 255) : IM_COL32(90, 75, 40, 255),
                      5.0f * scale);
    const char* cue_lbl = "CUE";
    const ImVec2 cs = ImGui::CalcTextSize(cue_lbl);
    dl->AddText(
        ImVec2((cue_min.x + cue_max.x - cs.x) * 0.5f, (cue_min.y + cue_max.y - cs.y) * 0.5f),
        IM_COL32(240, 235, 220, 255), cue_lbl);
    ImGui::SetCursorScreenPos(cue_min);
    ImGui::InvisibleButton("##cue", ImVec2(cue_max.x - cue_min.x, cue_max.y - cue_min.y));
    if (ImGui::IsItemClicked()) {
        out.cue_pressed = true;
    }

    // Pitch fader vertical ±8% (doble-click = reset a 0).
    const ImVec2 pf_min = {header_max.x - pitch_w, header_min.y + 4.0f * scale};
    const ImVec2 pf_max = {header_max.x - 6.0f * scale, header_max.y - 6.0f * scale};
    ImGui::SetCursorScreenPos(pf_min);
    float pitch = static_cast<float>(state.pitch_pct);
    ImGui::VSliderFloat("##pitch", ImVec2(pf_max.x - pf_min.x, pf_max.y - pf_min.y), &pitch, -8.0f,
                        8.0f, "%.1f%%", ImGuiSliderFlags_AlwaysClamp);
    if (pitch != static_cast<float>(state.pitch_pct)) {
        out.pitch_changed = true;
        out.pitch_new = pitch;
    }

    // BPM gigante + pitched.
    float x = cue_max.x + 14.0f * scale;
    const float pitch_right = pf_min.x - 8.0f * scale;
    char buf[96];
    if (state.bpm > 0.0) {
        std::snprintf(buf, sizeof(buf), "%.1f", state.bpm);
    } else {
        std::snprintf(buf, sizeof(buf), "--.-");
    }
    ImFont* font = ImGui::GetFont();
    const float big = 24.0f * scale;
    dl->AddText(font, big, ImVec2(x, header_min.y + 6.0f * scale), IM_COL32(240, 244, 250, 255),
                buf);
    dl->AddText(ImVec2(x, header_min.y + 38.0f * scale), IM_COL32(130, 140, 155, 255),
                (state.pitch_pct > 0.01 || state.pitch_pct < -0.01) ? "BPM pitched" : "BPM");
    if (state.pitch_pct > 0.01 || state.pitch_pct < -0.01) {
        std::snprintf(buf, sizeof(buf), "%.1f", state.bpm_pitched);
        dl->AddText(ImVec2(x + 56.0f * scale, header_min.y + 14.0f * scale),
                    IM_COL32(160, 190, 230, 255), buf);
    }

    // Key badge.
    x += 108.0f * scale;
    const ImVec2 key0 = {x, header_min.y + 8.0f * scale};
    const ImVec2 key1 = {x + 56.0f * scale, header_min.y + 40.0f * scale};
    if (state.camelot > 0) {
        std::snprintf(buf, sizeof(buf), "%d%c", state.camelot, state.camelot_minor ? 'A' : 'B');
        dl->AddRectFilled(key0, key1, accent_dim, 4.0f * scale);
        dl->AddRect(key0, key1, accent, 4.0f * scale);
        const ImVec2 ks = ImGui::CalcTextSize(buf);
        dl->AddText(font, 18.0f * scale,
                    ImVec2((key0.x + key1.x - ks.x) * 0.5f, key0.y + 4.0f * scale),
                    IM_COL32(240, 244, 250, 255), buf);
        if (state.key_name != nullptr && state.key_name[0] != '\0') {
            dl->AddText(ImVec2(x, key1.y + 2.0f * scale), IM_COL32(130, 140, 155, 255),
                        state.key_name);
        }
    } else {
        dl->AddText(ImVec2(x, header_min.y + 14.0f * scale), IM_COL32(100, 108, 120, 255), "--");
    }

    // Tiempo transcurrido / restante.
    x += 72.0f * scale;
    char t0[16];
    char t1[16];
    format_time(t0, sizeof(t0), state.position_sec);
    format_time(t1, sizeof(t1), state.duration_sec - state.position_sec);
    std::snprintf(buf, sizeof(buf), "%s", t0);
    dl->AddText(font, 18.0f * scale, ImVec2(x, header_min.y + 8.0f * scale),
                IM_COL32(220, 226, 235, 255), buf);
    std::snprintf(buf, sizeof(buf), "-%s", t1);
    dl->AddText(ImVec2(x, header_min.y + 38.0f * scale), IM_COL32(130, 140, 155, 255), buf);

    // Título/artista (una línea, recortado).
    std::snprintf(buf, sizeof(buf), "%s%s%s", (state.title != nullptr) ? state.title : "",
                  (state.artist != nullptr && state.artist[0] != '\0') ? " — " : "",
                  (state.artist != nullptr) ? state.artist : "");
    const float title_x = x + 100.0f * scale;
    if (title_x < pitch_right - 12.0f * scale) {
        dl->PushClipRect(ImVec2(title_x, header_min.y),
                         ImVec2(pitch_right, header_min.y + 34.0f * scale), true);
        dl->AddText(ImVec2(title_x, header_min.y + 14.0f * scale), IM_COL32(210, 216, 226, 255),
                    buf);
        dl->PopClipRect();
    }

    ImGui::Dummy(ImVec2(0.0f, header_h));

    // --- Carriles de waveform (drop targets) -------------------------------
    const auto lane = [&](const char* id, float h, ImU32 bg) {
        const ImVec2 mn2 = ImGui::GetCursorScreenPos();
        const bool hover = ImGui::IsMouseHoveringRect(mn2, ImVec2(mn2.x + width_px, mn2.y + h));
        dl->AddRectFilled(mn2, ImVec2(mn2.x + width_px, mn2.y + h), bg);
        dl->AddRect(mn2, ImVec2(mn2.x + width_px, mn2.y + h),
                    hover ? accent : IM_COL32(40, 45, 55, 255), 0.0f, 0,
                    hover ? 2.0f * scale : 1.0f);
        ImGui::SetCursorScreenPos(mn2);
        ImGui::InvisibleButton(id, ImVec2(width_px, h));
        const int drop = accept_track_drop();
        if (drop >= 0 && out.drop_row < 0) {
            out.drop_row = drop;
        }
        return RECT{static_cast<LONG>(mn2.x), static_cast<LONG>(mn2.y),
                    static_cast<LONG>(mn2.x + width_px), static_cast<LONG>(mn2.y + h)};
    };
    ImGui::PushID(deck_idx);
    out.rects.overview = lane("##ov", 40.0f * scale, IM_COL32(16, 18, 24, 255));
    out.rects.detail = lane("##dt", 110.0f * scale, IM_COL32(14, 16, 22, 255));
    ImGui::PopID();
    return out;
}

} // namespace melodeus::app
