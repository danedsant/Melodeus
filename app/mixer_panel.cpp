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

void draw_mixer_panel(MixerState& state, float width_px, float height_px, float scale) {
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

    // Columna central vertical: [EQ + FILTRO por canal] -> [crossfader] ->
    // [master]. Sin VUs (el peak vive en la top bar).
    const float top = mn.y + 32.0f * scale;
    const float inner_x = mn.x + 12.0f * scale;
    const float inner_w = width_px - 24.0f * scale;
    const float knob_r = 13.0f * scale;
    const float col_w = inner_w * 0.5f;

    // --- Canales A y B: FILTRO + EQ (HI/MID/LOW) en rejilla 2x4 -----------
    for (int ch = 0; ch < 2; ++ch) {
        const float x0 = inner_x + col_w * static_cast<float>(ch);
        ImGui::PushID(ch);
        ImGui::SetCursorScreenPos(ImVec2(x0 + (col_w - knob_r * 2.0f) * 0.5f, top));
        knob_labeled("##flt", "FILTRO", &state.filter[ch], -1.0f, 1.0f, 0.0f, knob_r, scale, false);
        for (int b = 0; b < 3; ++b) {
            const char* lbl = (b == 0) ? "LOW" : ((b == 1) ? "MID" : "HI");
            char id[16];
            std::snprintf(id, sizeof(id), "##eq%d", b);
            knob_labeled(id, lbl, &state.eq_db[ch][b], kKnobMinDb, kKnobMaxDb, 0.0f, knob_r, scale,
                         true);
        }
        ImGui::PopID();
    }
    const float xf_top = ImGui::GetCursorScreenPos().y + 8.0f * scale;

    // --- Gains de canal (faders cortos) + crossfader ----------------------
    // Fila: [gain A] [crossfader] [gain B], alineada y centrada verticalmente.
    const float fader_h = 56.0f * scale;
    const float gain_w = 26.0f * scale;
    const float xf_y = xf_top;
    ImGui::SetCursorScreenPos(ImVec2(inner_x, xf_y));
    ImGui::VSliderFloat("##gain_a", ImVec2(gain_w, fader_h), &state.ch_gain_a, 0.0f, 1.0f, "");
    const float xf_x = inner_x + gain_w + 8.0f * scale;
    const float xf_w = inner_w - gain_w * 2.0f - 16.0f * scale;
    ImGui::SetCursorScreenPos(ImVec2(xf_x, xf_y + (fader_h - 20.0f) * 0.5f));
    ImGui::SetNextItemWidth(xf_w);
    ImGui::SliderFloat("##xf", &state.crossfader, 0.0f, 1.0f, "");
    // Detente central del crossfader.
    {
        const ImVec2 x0 = ImGui::GetItemRectMin();
        const ImVec2 x1 = ImGui::GetItemRectMax();
        const float cx = (x0.x + x1.x) * 0.5f;
        dl->AddLine(ImVec2(cx, x0.y - 2.0f * scale), ImVec2(cx, x0.y + 1.0f * scale),
                    theme::kTextDim, 1.0f);
    }
    // Etiquetas A / CROSS / B.
    const float lbl_y = xf_y + (fader_h - 20.0f) * 0.5f + 22.0f * scale;
    const ImVec2 asz = ImGui::CalcTextSize("A");
    const ImVec2 bsz = ImGui::CalcTextSize("B");
    dl->AddText(ImVec2(inner_x + (gain_w - bsz.x) * 0.5f, lbl_y), theme::kAccentA, "B");
    const char* xf_label = "CROSS";
    const ImVec2 xfs = ImGui::CalcTextSize(xf_label);
    dl->AddText(ImVec2(xf_x + (xf_w - xfs.x) * 0.5f, lbl_y), theme::kTextDim, xf_label);
    const float gain_b_x = inner_x + inner_w - gain_w;
    dl->AddText(ImVec2(gain_b_x + (gain_w - asz.x) * 0.5f, lbl_y), theme::kAccentB, "A");
    ImGui::SetCursorScreenPos(ImVec2(gain_b_x, xf_y));
    ImGui::VSliderFloat("##gain_b", ImVec2(gain_w, fader_h), &state.ch_gain_b, 0.0f, 1.0f, "");

    // --- Master (parte inferior, ancho completo) --------------------------
    const float master_y = xf_y + fader_h + 28.0f * scale;
    ImGui::SetCursorScreenPos(ImVec2(inner_x, master_y));
    ImGui::SetNextItemWidth(inner_w);
    ImGui::SliderFloat("##master", &state.master, 0.0f, 1.0f, "master %.2f");
}

} // namespace melodeus::app
