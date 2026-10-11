#include "deck_panel.hpp"

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

} // namespace

DeckPanelRects draw_deck_panel(const DeckUiState& state, float width_px, float scale,
                               bool* play_toggled) {
    DeckPanelRects rects{};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float header_h = 52.0f * scale;

    // --- Header -----------------------------------------------------------
    const ImVec2 header_min = ImGui::GetCursorScreenPos();
    const ImVec2 header_max = {header_min.x + width_px, header_min.y + header_h};
    dl->AddRectFilled(header_min, header_max, IM_COL32(24, 27, 34, 255));
    dl->AddRect(header_min, header_max, IM_COL32(45, 50, 60, 255));

    // Play/stop.
    const ImVec2 play_min = {header_min.x + 8.0f * scale, header_min.y + 8.0f * scale};
    const ImVec2 play_max = {play_min.x + 36.0f * scale, header_max.y - 8.0f * scale};
    const ImU32 play_col = state.playing ? IM_COL32(70, 160, 90, 255) : IM_COL32(50, 55, 65, 255);
    dl->AddRectFilled(play_min, play_max, play_col, 4.0f * scale);
    dl->AddRect(play_min, play_max, IM_COL32(90, 100, 115, 255), 4.0f * scale);
    const char* play_glyph = state.playing ? "||" : ">";
    ImVec2 gs = ImGui::CalcTextSize(play_glyph);
    dl->AddText(
        ImVec2((play_min.x + play_max.x - gs.x) * 0.5f, (play_min.y + play_max.y - gs.y) * 0.5f),
        IM_COL32(230, 235, 240, 255), play_glyph);
    if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(play_min, play_max) &&
        play_toggled != nullptr) {
        *play_toggled = true;
    }

    // BPM grande + pitched.
    float x = play_max.x + 14.0f * scale;
    char buf[64];
    if (state.bpm > 0.0) {
        std::snprintf(buf, sizeof(buf), "%.1f", state.bpm);
    } else {
        std::snprintf(buf, sizeof(buf), "--.-");
    }
    ImFont* font = ImGui::GetFont();
    const float big = 22.0f * scale;
    dl->AddText(font, big, ImVec2(x, header_min.y + 6.0f * scale), IM_COL32(240, 244, 250, 255),
                buf, nullptr, width_px);
    std::snprintf(buf, sizeof(buf), "BPM");
    dl->AddText(ImVec2(x, header_min.y + 34.0f * scale), IM_COL32(130, 140, 155, 255), buf);
    if (state.pitch_pct > 0.01 || state.pitch_pct < -0.01) {
        std::snprintf(buf, sizeof(buf), "%.1f", state.bpm_pitched);
        dl->AddText(ImVec2(x + 42.0f * scale, header_min.y + 10.0f * scale),
                    IM_COL32(160, 190, 230, 255), buf);
    }

    // Key: Camelot grande + tradicional.
    x += 96.0f * scale;
    if (state.camelot > 0) {
        std::snprintf(buf, sizeof(buf), "%d%c", state.camelot, state.camelot_minor ? 'A' : 'B');
    } else {
        std::snprintf(buf, sizeof(buf), "--");
    }
    dl->AddText(font, big, ImVec2(x, header_min.y + 6.0f * scale), IM_COL32(240, 244, 250, 255),
                buf, nullptr, width_px);
    if (state.key_name != nullptr && state.key_name[0] != '\0') {
        std::snprintf(buf, sizeof(buf), "(%s)", state.key_name);
        dl->AddText(ImVec2(x, header_min.y + 34.0f * scale), IM_COL32(130, 140, 155, 255), buf);
    }

    // Tiempo transcurrido / restante.
    x += 80.0f * scale;
    char t0[16];
    char t1[16];
    format_time(t0, sizeof(t0), state.position_sec);
    format_time(t1, sizeof(t1), state.duration_sec - state.position_sec);
    std::snprintf(buf, sizeof(buf), "%s", t0);
    dl->AddText(font, 16.0f * scale, ImVec2(x, header_min.y + 8.0f * scale),
                IM_COL32(220, 226, 235, 255), buf);
    std::snprintf(buf, sizeof(buf), "-%s", t1);
    dl->AddText(ImVec2(x, header_min.y + 34.0f * scale), IM_COL32(130, 140, 155, 255), buf);

    // Título/artista a la derecha (una línea).
    std::snprintf(buf, sizeof(buf), "%s%s%s", (state.title != nullptr) ? state.title : "",
                  (state.artist != nullptr && state.artist[0] != '\0') ? " — " : "",
                  (state.artist != nullptr) ? state.artist : "");
    const float title_x = x + 90.0f * scale;
    if (title_x < header_max.x - 12.0f * scale) {
        dl->PushClipRect(ImVec2(title_x, header_min.y),
                         ImVec2(header_max.x - 8.0f * scale, header_max.y), true);
        dl->AddText(ImVec2(title_x, header_min.y + 18.0f * scale), IM_COL32(200, 206, 216, 255),
                    buf);
        dl->PopClipRect();
    }

    ImGui::Dummy(ImVec2(0.0f, header_h));

    // --- Carriles de waveform ---------------------------------------------
    const auto lane = [&](float h, ImU32 bg) {
        const ImVec2 mn = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(mn, ImVec2(mn.x + width_px, mn.y + h), bg);
        dl->AddRect(mn, ImVec2(mn.x + width_px, mn.y + h), IM_COL32(40, 45, 55, 255));
        ImGui::Dummy(ImVec2(0.0f, h));
        return RECT{static_cast<LONG>(mn.x), static_cast<LONG>(mn.y),
                    static_cast<LONG>(mn.x + width_px), static_cast<LONG>(mn.y + h)};
    };
    rects.overview = lane(44.0f * scale, IM_COL32(16, 18, 24, 255));
    rects.detail = lane(110.0f * scale, IM_COL32(14, 16, 22, 255));
    return rects;
}

} // namespace melodeus::app
