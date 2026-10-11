#include "mixer_panel.hpp"

#include "render/imgui_layer.hpp"
#include "theme.hpp"

#include "imgui.h"

#include <cmath>
#include <cstdio>

namespace melodeus::app {

namespace {

constexpr float kKnobMinDb = -26.0f;
constexpr float kKnobMaxDb = 6.0f;

void draw_vu(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float level) {
    dl->AddRectFilled(mn, mx, IM_COL32(28, 31, 38, 120), 3.0f);
    if (level <= 0.0f) {
        return;
    }
    const float h = (mx.y - mn.y) * (level > 1.0f ? 1.0f : level);
    const ImU32 col =
        level > 0.95f ? theme::kDanger : (level > 0.7f ? theme::kWarn : theme::kSuccess);
    dl->AddRectFilled(ImVec2(mn.x, mx.y - h), mx, col, 3.0f);
}

/// Knob circular glass: arrastre vertical; doble-click = reset. Devuelve
/// true si el valor cambió.
bool knob(const char* id, float* v, float v_min, float v_max, float reset_v, float radius,
          bool show_db) {
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
    // Cuerpo glass del knob.
    dl->AddCircleFilled(c, radius, theme::kGlassFillStrong, 24);
    const ImU32 edge =
        active ? theme::kAccentA : (hovered ? IM_COL32(110, 122, 148, 255) : theme::kGlassBorder);
    dl->AddCircle(c, radius, edge, 24, active || hovered ? 1.5f : 1.0f);
    // Arco de valor: -135° .. +135° (mitad inferior abierta).
    constexpr float kPi = 3.14159265358979323846f;
    const float a0 = 0.75f * kPi;   // inferior-izquierda
    const float sweep = 1.5f * kPi; // hacia la derecha pasando por arriba
    const float a = a0 - sweep * frac;
    if (frac > 0.01f) {
        dl->PathClear();
        const int segs = 20;
        for (int i = 0; i <= segs; ++i) {
            const float t = a0 - sweep * frac * (static_cast<float>(i) / segs);
            dl->PathLineTo(
                ImVec2(c.x + std::cos(t) * (radius - 2.0f), c.y - std::sin(t) * (radius - 2.0f)));
        }
        dl->PathStroke(theme::kAccentA, 0, 2.0f);
    }
    // Indicador.
    dl->AddLine(ImVec2(c.x + std::cos(a) * (radius * 0.25f), c.y - std::sin(a) * (radius * 0.25f)),
                ImVec2(c.x + std::cos(a) * (radius - 4.0f), c.y - std::sin(a) * (radius - 4.0f)),
                theme::kTextHot, 2.0f);
    // Valor dB dentro del knob (sólo knobs de EQ).
    if (show_db) {
        char b[16];
        if (*v > -0.05f && *v < 0.05f) {
            std::snprintf(b, sizeof(b), "0");
        } else {
            std::snprintf(b, sizeof(b), "%+.0f", static_cast<double>(*v));
        }
        if (ImFont* mono = imgui_font_mono()) {
            const ImVec2 ts = ImGui::CalcTextSize(b);
            const float cy = c.y + radius * 0.55f - ts.y * 0.5f;
            dl->PushClipRect(ImVec2(c.x - radius, cy - 1.0f),
                             ImVec2(c.x + radius, cy + ts.y + 1.0f), true);
            dl->AddText(mono, mono->LegacySize, ImVec2(c.x - ts.x * 0.5f, cy),
                        (*v < -0.05f) ? theme::kDanger : theme::kTextDim, b);
            dl->PopClipRect();
        }
    }
    return changed;
}

void knob_labeled(const char* id, const char* label, float* v, float v_min, float v_max,
                  float reset_v, float radius, float scale, bool show_db) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    knob(id, v, v_min, v_max, reset_v, radius, show_db);
    const char* lb = label;
    const ImVec2 ls = ImGui::CalcTextSize(lb);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(pos.x + radius - ls.x * 0.5f, pos.y + radius * 2.0f + 2.0f * scale), theme::kTextDim,
        lb);
    ImGui::Dummy(ImVec2(0.0f, ls.y + 4.0f * scale));
}

} // namespace

void draw_mixer_panel(MixerState& state, float peak_a, float peak_b, float master_env,
                      float width_px, float height_px, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx = {mn.x + width_px, mn.y + height_px};
    theme::glass(dl, mn, mx);
    if (ImFont* title = imgui_font_title()) {
        dl->AddText(title, title->LegacySize, ImVec2(mn.x + 12.0f * scale, mn.y + 5.0f * scale),
                    theme::kText, "MIXER");
    } else {
        dl->AddText(ImVec2(mn.x + 12.0f * scale, mn.y + 6.0f * scale), theme::kText, "MIXER");
    }

    // --- Canales A y B: Filtro, EQ (HI/MID/LOW), gain + VU -----------------
    const float top = mn.y + 30.0f * scale;
    const float col_w = width_px * 0.5f;
    const float knob_r = 14.0f * scale;
    const float bottom_reserve = 120.0f * scale;
    const float chan_h = mn.y + height_px - bottom_reserve - top;
    const float fader_h = chan_h - (knob_r * 2.0f + 16.0f * scale) * 4.0f - 24.0f * scale;

    for (int ch = 0; ch < 2; ++ch) {
        const float x0 = mn.x + col_w * static_cast<float>(ch);
        float* gain = (ch == 0) ? &state.ch_gain_a : &state.ch_gain_b;
        const float peak = (ch == 0) ? peak_a : peak_b;
        ImGui::PushID(ch);

        // Filtro DJ (LP <-> HP).
        ImGui::SetCursorScreenPos(ImVec2(x0 + 10.0f * scale, top));
        knob_labeled("##flt", "FILTRO", &state.filter[ch], -1.0f, 1.0f, 0.0f, knob_r, scale, false);

        // EQ: HI / MID / LOW (doble-click = 0 dB; valor dB dentro del knob).
        for (int b = 0; b < 3; ++b) {
            const char* lbl = (b == 0) ? "LOW" : ((b == 1) ? "MID" : "HI");
            char id[16];
            std::snprintf(id, sizeof(id), "##eq%d", b);
            knob_labeled(id, lbl, &state.eq_db[ch][b], kKnobMinDb, kKnobMaxDb, 0.0f, knob_r, scale,
                         true);
        }

        // Gain fader + VU.
        const float f_top = ImGui::GetCursorScreenPos().y;
        const float fh = fader_h > 40.0f * scale ? fader_h : 40.0f * scale;
        ImGui::SetCursorScreenPos(ImVec2(x0 + 10.0f * scale, f_top));
        ImGui::VSliderFloat(ch == 0 ? "##gain_a" : "##gain_b", ImVec2(28.0f * scale, fh), gain,
                            0.0f, 1.0f, "");
        const ImVec2 vu0 = {x0 + 44.0f * scale, f_top};
        const ImVec2 vu1 = {x0 + 56.0f * scale, f_top + fh};
        draw_vu(dl, vu0, vu1, peak);

        dl->AddText(ImVec2(x0 + 10.0f * scale, f_top + fh + 4.0f * scale), theme::kTextDim,
                    ch == 0 ? "A" : "B");
        ImGui::PopID();
    }

    // --- Crossfader + master ----------------------------------------------
    const float xf_y = mn.y + height_px - bottom_reserve;
    ImGui::SetCursorScreenPos(ImVec2(mn.x + 16.0f * scale, xf_y));
    ImGui::SetNextItemWidth(width_px - 32.0f * scale);
    ImGui::SliderFloat("##xf", &state.crossfader, 0.0f, 1.0f, "");
    // Detente central del crossfader.
    {
        const ImVec2 x0 = ImGui::GetItemRectMin();
        const ImVec2 x1 = ImGui::GetItemRectMax();
        const float cx = (x0.x + x1.x) * 0.5f;
        dl->AddLine(ImVec2(cx, x0.y - 2.0f * scale), ImVec2(cx, x0.y + 1.0f * scale),
                    theme::kTextDim, 1.0f);
    }
    dl->AddText(ImVec2(mn.x + 16.0f * scale, xf_y + 24.0f * scale), theme::kTextDim, "B");
    const char* xf_label = "CROSS";
    const ImVec2 xfs = ImGui::CalcTextSize(xf_label);
    dl->AddText(ImVec2(mn.x + (width_px - xfs.x) * 0.5f, xf_y + 24.0f * scale), theme::kTextDim,
                xf_label);
    const ImVec2 asz_m = ImGui::CalcTextSize("A");
    dl->AddText(ImVec2(mn.x + width_px - 16.0f * scale - asz_m.x, xf_y + 24.0f * scale),
                theme::kTextDim, "A");

    // Master + VU del master.
    const float master_y = xf_y + 54.0f * scale;
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
