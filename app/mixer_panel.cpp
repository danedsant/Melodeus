#include "mixer_panel.hpp"

#include "imgui.h"

#include <cmath>
#include <cstdio>

namespace melodeus::app {

namespace {

constexpr float kKnobMinDb = -26.0f;
constexpr float kKnobMaxDb = 6.0f;

void draw_vu(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float level) {
    dl->AddRectFilled(mn, mx, IM_COL32(28, 31, 38, 255), 2.0f);
    if (level <= 0.0f) {
        return;
    }
    const float h = (mx.y - mn.y) * (level > 1.0f ? 1.0f : level);
    const ImU32 col =
        level > 0.95f ? IM_COL32(220, 70, 60, 255)
                      : (level > 0.7f ? IM_COL32(220, 180, 60, 255) : IM_COL32(70, 190, 90, 255));
    dl->AddRectFilled(ImVec2(mn.x, mx.y - h), mx, col, 2.0f);
}

/// Knob circular: arrastre vertical con el ratón; doble-click = reset.
/// `v` queda en [v_min, v_max]. Devuelve true si el valor cambió.
bool knob(const char* id, float* v, float v_min, float v_max, float reset_v, float radius) {
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(radius * 2.0f, radius * 2.0f));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    bool changed = false;
    if (active) {
        const float speed = (v_max - v_min) / 200.0f;
        const float nv = *v - ImGui::GetIO().MouseDelta.y * speed;
        if (nv != *v) {
            *v = nv < v_min ? v_min : (nv > v_max ? v_max : nv);
            changed = true;
        }
        if (ImGui::IsMouseDoubleClicked(0)) {
            *v = reset_v;
            changed = true;
        }
    }
    const float frac = (v_max > v_min) ? (*v - v_min) / (v_max - v_min) : 0.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c = {mn.x + radius, mn.y + radius};
    const ImU32 bg = IM_COL32(32, 36, 44, 255);
    const ImU32 edge = active ? IM_COL32(120, 170, 230, 255)
                              : (hovered ? IM_COL32(90, 100, 120, 255) : IM_COL32(55, 60, 72, 255));
    dl->AddCircleFilled(c, radius, bg, 24);
    dl->AddCircle(c, radius, edge, 24, 1.5f);
    // Arco de valor: -135° .. +135° (mitad inferior abierta).
    constexpr float kPi = 3.14159265358979323846f;
    const float a0 = 0.75f * kPi;      // inferior-izquierda
    const float sweep = 1.5f * kPi;    // hacia la derecha pasando por arriba
    const float a = a0 - sweep * frac; // sentido horario en pantalla (y hacia abajo)
    if (frac > 0.01f) {
        dl->PathClear();
        const int segs = 20;
        for (int i = 0; i <= segs; ++i) {
            const float t = a0 - sweep * frac * (static_cast<float>(i) / segs);
            dl->PathLineTo(
                ImVec2(c.x + std::cos(t) * (radius - 2.0f), c.y - std::sin(t) * (radius - 2.0f)));
        }
        dl->PathStroke(IM_COL32(90, 150, 220, 255), 0, 2.0f);
    }
    // Indicador.
    dl->AddLine(ImVec2(c.x + std::cos(a) * (radius * 0.25f), c.y - std::sin(a) * (radius * 0.25f)),
                ImVec2(c.x + std::cos(a) * (radius - 4.0f), c.y - std::sin(a) * (radius - 4.0f)),
                IM_COL32(220, 228, 240, 255), 2.0f);
    return changed;
}

void knob_labeled(const char* id, const char* label, float* v, float v_min, float v_max,
                  float reset_v, float radius, float scale) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    knob(id, v, v_min, v_max, reset_v, radius);
    const char* lb = label;
    const ImVec2 ls = ImGui::CalcTextSize(lb);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(pos.x + radius - ls.x * 0.5f, pos.y + radius * 2.0f + 2.0f * scale),
        IM_COL32(150, 158, 170, 255), lb);
    ImGui::Dummy(ImVec2(0.0f, ls.y + 4.0f * scale));
}

} // namespace

void draw_mixer_panel(MixerState& state, float peak_a, float peak_b, float master_env,
                      float width_px, float height_px, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(mn, ImVec2(mn.x + width_px, mn.y + height_px), IM_COL32(20, 22, 28, 255));
    dl->AddRect(mn, ImVec2(mn.x + width_px, mn.y + height_px), IM_COL32(45, 50, 60, 255));
    dl->AddText(ImVec2(mn.x + 10.0f * scale, mn.y + 6.0f * scale), IM_COL32(200, 208, 220, 255),
                "MIXER");

    // --- Canales A y B: Filtro, EQ (HI/MID/LOW), gain + VU -----------------
    const float top = mn.y + 28.0f * scale;
    const float col_w = width_px * 0.5f;
    const float knob_r = 13.0f * scale;
    const float bottom_reserve = 118.0f * scale; ///< crossfader + master
    const float chan_h = mn.y + height_px - bottom_reserve - top;
    const float fader_h = chan_h - (knob_r * 2.0f + 14.0f * scale) * 4.0f - 24.0f * scale;

    for (int ch = 0; ch < 2; ++ch) {
        const float x0 = mn.x + col_w * static_cast<float>(ch);
        float* gain = (ch == 0) ? &state.ch_gain_a : &state.ch_gain_b;
        const float peak = (ch == 0) ? peak_a : peak_b;
        ImGui::PushID(ch);

        // Filtro DJ (LP <-> HP).
        ImGui::SetCursorScreenPos(ImVec2(x0 + 8.0f * scale, top));
        knob_labeled("##flt", "FILTRO", &state.filter[ch], -1.0f, 1.0f, 0.0f, knob_r, scale);

        // EQ: HI / MID / LOW (doble-click = 0 dB).
        for (int b = 0; b < 3; ++b) {
            const char* lbl = (b == 0) ? "LOW" : ((b == 1) ? "MID" : "HI");
            char id[16];
            std::snprintf(id, sizeof(id), "##eq%d", b);
            knob_labeled(id, lbl, &state.eq_db[ch][b], kKnobMinDb, kKnobMaxDb, 0.0f, knob_r, scale);
        }

        // Gain fader + VU.
        const float f_top = ImGui::GetCursorScreenPos().y;
        const float fh = fader_h > 40.0f * scale ? fader_h : 40.0f * scale;
        ImGui::SetCursorScreenPos(ImVec2(x0 + 8.0f * scale, f_top));
        ImGui::VSliderFloat(ch == 0 ? "##gain_a" : "##gain_b", ImVec2(28.0f * scale, fh), gain,
                            0.0f, 1.0f, "");
        const ImVec2 vu0 = {x0 + 42.0f * scale, f_top};
        const ImVec2 vu1 = {x0 + 54.0f * scale, f_top + fh};
        draw_vu(dl, vu0, vu1, peak);

        dl->AddText(ImVec2(x0 + 8.0f * scale, f_top + fh + 4.0f * scale),
                    IM_COL32(150, 158, 170, 255), ch == 0 ? "A" : "B");
        ImGui::PopID();
    }

    // --- Crossfader + master ----------------------------------------------
    const float xf_y = mn.y + height_px - bottom_reserve;
    ImGui::SetCursorScreenPos(ImVec2(mn.x + 16.0f * scale, xf_y));
    ImGui::SetNextItemWidth(width_px - 32.0f * scale);
    ImGui::SliderFloat("##xf", &state.crossfader, 0.0f, 1.0f, "");
    dl->AddText(ImVec2(mn.x + 16.0f * scale, xf_y + 22.0f * scale), IM_COL32(150, 158, 170, 255),
                "B");
    const char* xf_label = "CROSS";
    const ImVec2 xfs = ImGui::CalcTextSize(xf_label);
    dl->AddText(ImVec2(mn.x + (width_px - xfs.x) * 0.5f, xf_y + 22.0f * scale),
                IM_COL32(150, 158, 170, 255), xf_label);
    const ImVec2 asz_m = ImGui::CalcTextSize("A");
    dl->AddText(ImVec2(mn.x + width_px - 16.0f * scale - asz_m.x, xf_y + 22.0f * scale),
                IM_COL32(150, 158, 170, 255), "A");

    // Master + VU del master.
    const float master_y = xf_y + 52.0f * scale;
    ImGui::SetCursorScreenPos(ImVec2(mn.x + 16.0f * scale, master_y));
    ImGui::SetNextItemWidth(width_px - 90.0f * scale);
    ImGui::SliderFloat("##master", &state.master, 0.0f, 1.0f, "master %.2f");
    const ImVec2 mvu0 = {mn.x + width_px - 62.0f * scale, master_y};
    const ImVec2 mvu1 = {mn.x + width_px - 40.0f * scale, master_y + 22.0f * scale};
    draw_vu(dl, mvu0, mvu1, master_env);
    const ImVec2 gr0 = {mn.x + width_px - 34.0f * scale, master_y};
    const ImVec2 gr1 = {mn.x + width_px - 16.0f * scale, master_y + 22.0f * scale};
    draw_vu(dl, gr0, gr1, master_env > 0.0f ? 1.0f - master_env : 0.0f);
}

} // namespace melodeus::app
