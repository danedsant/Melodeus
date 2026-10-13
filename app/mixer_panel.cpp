#include "mixer_panel.hpp"

#include "i18n.hpp"
#include "render/imgui_layer.hpp"
#include "theme.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace melodeus::app {

namespace {

constexpr float kKnobMinDb = -26.0f;
constexpr float kKnobMaxDb = 6.0f;
constexpr float kPi = 3.14159265358979323846f;

/// Modo de lectura numérica dentro del knob.
enum class KnobDisp : unsigned char { None, Db, Pct };

/// Knob circular glass: arrastre vertical; doble-click = reset. Devuelve
/// true si el valor cambió. `centered` = detente en `center` a las 12:00
/// (EQ/filtro: 0 dB/bypass arriba, corte a la izquierda, boost a la derecha);
/// sin center, mapeo lineal de inferior-izquierda a inferior-derecha.
bool knob(const char* id, float* v, float v_min, float v_max, float reset_v, float radius,
          KnobDisp disp, bool centered = false, float center = 0.0f, ImU32 accent_col = 0) {
    const ImU32 effective_accent = (accent_col != 0) ? accent_col : theme::accent_a();
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
        active ? effective_accent : (hovered ? IM_COL32(110, 122, 148, 255) : theme::kGlassBorder);
    dl->AddCircle(c, radius, edge, 24, active || hovered ? 1.5f : 1.0f);
    // Ángulo del indicador. Convención y-flip: y = c.y - sin(t)*r, así que
    // t=π/2 es arriba. Centrado: centro→12:00, ±135° hacia los lados.
    // Lineal: 0→inferior-izq (1.25π), 0.5→arriba, 1→inferior-der (-0.25π).
    constexpr float kTop = 0.5f * kPi;
    constexpr float kHalf = 0.75f * kPi; // 135° por lado
    float a;                             // ángulo actual del indicador
    if (centered) {
        if (*v <= center) {
            const float t = (center - *v) / (center - v_min);
            a = kTop + t * kHalf;
        } else {
            const float t = (*v - center) / (v_max - center);
            a = kTop - t * kHalf;
        }
    } else {
        const float a0 = 1.25f * kPi;   // inferior-izquierda (225°)
        const float sweep = 1.5f * kPi; // hacia la derecha pasando por arriba
        a = a0 - sweep * frac;
    }
    // Arco de valor: desde el detente (o el mínimo) hasta el valor actual.
    {
        float arc_from;
        float arc_to;
        bool draw_arc;
        if (centered) {
            arc_from = kTop;
            arc_to = a;
            draw_arc = std::fabs(*v - center) > (v_max - v_min) * 0.004f;
        } else {
            arc_from = 1.25f * kPi;
            arc_to = a;
            draw_arc = frac > 0.01f;
        }
        if (draw_arc) {
            dl->PathClear();
            const int segs = 20;
            for (int i = 0; i <= segs; ++i) {
                const float t = arc_from + (arc_to - arc_from) * (static_cast<float>(i) / segs);
                dl->PathLineTo(ImVec2(c.x + std::cos(t) * (radius - 2.0f),
                                      c.y - std::sin(t) * (radius - 2.0f)));
            }
            dl->PathStroke(effective_accent, 0, 2.0f);
        }
    }
    // Indicador.
    dl->AddLine(ImVec2(c.x + std::cos(a) * (radius * 0.25f), c.y - std::sin(a) * (radius * 0.25f)),
                ImVec2(c.x + std::cos(a) * (radius - 4.0f), c.y - std::sin(a) * (radius - 4.0f)),
                theme::kTextHot, 2.0f);
    // Valor numérico dentro del knob (dB para EQ, % para gain).
    if (disp != KnobDisp::None) {
        char b[16];
        if (disp == KnobDisp::Db) {
            if (*v > -0.05f && *v < 0.05f) {
                std::snprintf(b, sizeof(b), "0");
            } else {
                std::snprintf(b, sizeof(b), "%+.0f", static_cast<double>(*v));
            }
        } else {
            std::snprintf(b, sizeof(b), "%d", static_cast<int>(std::lround(*v * 100.0f)));
        }
        if (ImFont* mono = imgui_font_mono()) {
            const ImVec2 ts = mono->CalcTextSizeA(mono->LegacySize, FLT_MAX, 0.0f, b);
            const float cy = c.y + radius * 0.55f - ts.y * 0.5f;
            dl->PushClipRect(ImVec2(c.x - radius, cy - 1.0f),
                             ImVec2(c.x + radius, cy + ts.y + 1.0f), true);
            const bool neg = (disp == KnobDisp::Db) && (*v < -0.05f);
            dl->AddText(mono, mono->LegacySize, ImVec2(c.x - ts.x * 0.5f, cy),
                        neg ? theme::kDanger : theme::kTextDim, b);
            dl->PopClipRect();
        }
    }
    return changed;
}

} // namespace

void draw_mixer_panel(MixerState& state, float width_px, float height_px, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx = {mn.x + width_px, mn.y + height_px};
    theme::glass(dl, mn, mx);

    // --- Título centrado bold tracking-widest (mb-6 = 24) -------------------
    const float pad = 12.0f * scale; // p-3
    const char* title = tr(Str::Mixer);
    ImFont* bold = imgui_font_bold();
    const float title_sz = 14.0f * scale; // text-sm
    ImVec2 ts = bold != nullptr ? bold->CalcTextSizeA(title_sz, FLT_MAX, 0.0f, title)
                                : ImGui::CalcTextSize(title);
    const float title_y = mn.y + 8.0f * scale;
    const float title_cx = mn.x + width_px * 0.5f;
    if (bold != nullptr) {
        // Tracking-widest: dibujamos letra a letra con espaciado extra.
        float x = title_cx - (ts.x + title_sz * 0.35f * 4.0f) * 0.5f;
        for (const char* p = title; *p != '\0'; ++p) {
            char g[2] = {*p, '\0'};
            dl->AddText(bold, title_sz, ImVec2(x, title_y), theme::kText, g);
            const ImVec2 gs = bold->CalcTextSizeA(title_sz, FLT_MAX, 0.0f, g);
            x += gs.x + title_sz * 0.35f;
        }
    } else {
        dl->AddText(ImVec2(title_cx - ts.x * 0.5f, title_y), theme::kText, title);
    }

    // --- Grid de knobs: 2 columnas (A/B), filas GAIN/FILTRO/HI/MID/LOW -----
    const float knob_r = 22.0f * scale; // w-11 → 44px
    const float lbl_sz = 10.0f * scale; // text-[10px]
    const float gap_x = 8.0f * scale;   // gap-x-2
    const float gap_y = 32.0f * scale;  // gap-y-8
    const float inner_x = mn.x + pad;
    const float inner_w = width_px - pad * 2.0f;
    const float col_w = (inner_w - gap_x) * 0.5f;
    const float cell_h = knob_r * 2.0f + 4.0f * scale + lbl_sz; // knob + label

    // Bloques inferiores anclados al pie: CROSSFADER y MASTER en cajas oscuras rediseñadas.
    const float box_pad = 10.0f * scale;
    const float sld_h = 18.0f * scale;
    const float hdr_h = 16.0f * scale;
    const float box_h = box_pad + hdr_h + 8.0f * scale + sld_h + box_pad;
    const float box_gap = 14.0f * scale;
    const float boxes_top = mx.y - pad - box_h * 2.0f - box_gap;

    // Área disponible para el grid de knobs (5 filas).
    const float grid_top = title_y + ts.y + 24.0f * scale; // mb-6
    const float grid_bot = boxes_top - 8.0f * scale;       // mt-8
    const float rows = 5.0f;
    // Ranuras iguales; si no cabe, comprime el gap.
    const float slot_h = (grid_bot - grid_top) / rows;
    const float use_gap = gap_y;
    const float row_h = slot_h > (cell_h + use_gap) ? cell_h + use_gap : slot_h;

    for (int ch = 0; ch < 2; ++ch) {
        const float cx = inner_x + (col_w + gap_x) * static_cast<float>(ch) + col_w * 0.5f;
        ImGui::PushID(ch);
        struct {
            const char* lbl;
            float* v;
            float vmin;
            float vmax;
            float reset;
            KnobDisp disp;
            bool centered;
        } rows_def[5] = {
            {tr(Str::Gain), (ch == 0) ? &state.ch_gain_a : &state.ch_gain_b, 0.0f, 1.0f, 0.8f,
             KnobDisp::Pct, false},
            {tr(Str::Filter), &state.filter[ch], -1.0f, 1.0f, 0.0f, KnobDisp::None, true},
            {tr(Str::EqHi), &state.eq_db[ch][2], kKnobMinDb, kKnobMaxDb, 0.0f, KnobDisp::Db, true},
            {tr(Str::EqMid), &state.eq_db[ch][1], kKnobMinDb, kKnobMaxDb, 0.0f, KnobDisp::Db, true},
            {tr(Str::EqLow), &state.eq_db[ch][0], kKnobMinDb, kKnobMaxDb, 0.0f, KnobDisp::Db, true},
        };
        for (int r = 0; r < 5; ++r) {
            const float top = grid_top + row_h * static_cast<float>(r) + (row_h - cell_h) * 0.5f;
            ImGui::SetCursorScreenPos(ImVec2(cx - knob_r, top));
            char id[16];
            std::snprintf(id, sizeof(id), "##k%d%d", ch, r);
            knob(id, rows_def[r].v, rows_def[r].vmin, rows_def[r].vmax, rows_def[r].reset, knob_r,
                 rows_def[r].disp, rows_def[r].centered, 0.0f,
                 (ch == 0) ? theme::accent_a() : theme::accent_b());
            // Etiqueta debajo del knob (text-[10px], centered).
            const ImVec2 ls = ImGui::CalcTextSize(rows_def[r].lbl);
            dl->AddText(ImVec2(cx - ls.x * 0.5f, top + knob_r * 2.0f + 4.0f * scale),
                        theme::kTextDim, rows_def[r].lbl);
        }
        ImGui::PopID();
    }

    // --- CROSSFADER (caja oscura con cabecera y fader DJ de alta visibilidad)
    const auto dark_box = [&](const ImVec2& b0, const ImVec2& b1) {
        dl->AddRectFilled(b0, b1, IM_COL32(14, 16, 22, 230), 8.0f * scale);
        dl->AddRect(b0, b1, theme::kGlassBorder, 8.0f * scale);
    };
    const ImVec2 xf0 = {inner_x, boxes_top};
    const ImVec2 xf1 = {mn.x + width_px - pad, boxes_top + box_h};
    dark_box(xf0, xf1);
    {
        // Fila 1: Cabecera [A]  CROSSFADER  [B] (completamente separada del track)
        const float hdr_y = xf0.y + box_pad;
        const char* lbl = tr(Str::Crossfader);
        const ImVec2 ls = ImGui::CalcTextSize(lbl);

        // Badge A
        const float badge_h = hdr_h;
        const float badge_w = 18.0f * scale;
        dl->AddRectFilled(ImVec2(xf0.x + box_pad, hdr_y),
                          ImVec2(xf0.x + box_pad + badge_w, hdr_y + badge_h),
                          theme::accent_a_dim(), 4.0f * scale);
        dl->AddRect(ImVec2(xf0.x + box_pad, hdr_y),
                    ImVec2(xf0.x + box_pad + badge_w, hdr_y + badge_h),
                    theme::accent_a(), 4.0f * scale);
        const ImVec2 asz = ImGui::CalcTextSize("A");
        dl->AddText(ImVec2(xf0.x + box_pad + (badge_w - asz.x) * 0.5f,
                           hdr_y + (badge_h - asz.y) * 0.5f),
                    theme::accent_a(), "A");

        // CROSSFADER label centrado
        dl->AddText(ImVec2(xf0.x + (xf1.x - xf0.x - ls.x) * 0.5f,
                           hdr_y + (badge_h - ls.y) * 0.5f),
                    theme::kText, lbl);

        // Badge B
        dl->AddRectFilled(ImVec2(xf1.x - box_pad - badge_w, hdr_y),
                          ImVec2(xf1.x - box_pad, hdr_y + badge_h),
                          theme::accent_b_dim(), 4.0f * scale);
        dl->AddRect(ImVec2(xf1.x - box_pad - badge_w, hdr_y),
                    ImVec2(xf1.x - box_pad, hdr_y + badge_h),
                    theme::accent_b(), 4.0f * scale);
        const ImVec2 bsz = ImGui::CalcTextSize("B");
        dl->AddText(ImVec2(xf1.x - box_pad - badge_w + (badge_w - bsz.x) * 0.5f,
                           hdr_y + (badge_h - bsz.y) * 0.5f),
                    theme::accent_b(), "B");

        // Fila 2: Track de Fader DJ de Alto Contraste
        const float tr_y = hdr_y + badge_h + 8.0f * scale;
        const float tr_x0 = xf0.x + box_pad;
        const float tr_x1 = xf1.x - box_pad;
        const float tr_w = tr_x1 - tr_x0;
        const ImVec2 t0 = {tr_x0, tr_y};
        const ImVec2 t1 = {tr_x1, tr_y + sld_h};

        // Riel hundido oscuro con bisel
        dl->AddRectFilled(t0, t1, IM_COL32(8, 10, 14, 255), 4.0f * scale);
        dl->AddRect(t0, t1, IM_COL32(40, 48, 64, 255), 4.0f * scale, 0, 1.0f);
        const float slot_cy = (t0.y + t1.y) * 0.5f;
        dl->AddLine(ImVec2(tr_x0 + 4.0f * scale, slot_cy),
                    ImVec2(tr_x1 - 4.0f * scale, slot_cy),
                    IM_COL32(18, 22, 30, 255), 3.0f * scale);

        // Detente central destacado (línea vertical nítida de altura completa)
        const float cx = (t0.x + t1.x) * 0.5f;
        dl->AddLine(ImVec2(cx, t0.y - 3.0f * scale), ImVec2(cx, t1.y + 3.0f * scale),
                    IM_COL32(230, 235, 250, 255), 2.0f);

        // Ticks intermedios de graduación
        const float q1 = tr_x0 + tr_w * 0.25f;
        const float q3 = tr_x0 + tr_w * 0.75f;
        dl->AddLine(ImVec2(q1, slot_cy - 4.0f * scale), ImVec2(q1, slot_cy + 4.0f * scale),
                    IM_COL32(70, 80, 100, 200), 1.0f);
        dl->AddLine(ImVec2(q3, slot_cy - 4.0f * scale), ImVec2(q3, slot_cy + 4.0f * scale),
                    IM_COL32(70, 80, 100, 200), 1.0f);

        // Interacción táctil de fader
        ImGui::SetCursorScreenPos(ImVec2(t0.x - 4.0f * scale, t0.y - 4.0f * scale));
        ImGui::InvisibleButton("##xf_btn", ImVec2(tr_w + 8.0f * scale, sld_h + 8.0f * scale));
        const bool active = ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();
        if (active) {
            const float mx_pos = ImGui::GetIO().MousePos.x;
            float v = (mx_pos - tr_x0) / tr_w;
            v = std::clamp(v, 0.0f, 1.0f);
            if (std::fabs(v - 0.5f) < 0.025f) {
                v = 0.5f; // Snap magnético suave al centro
            }
            state.crossfader = v;
        }
        if (hovered && ImGui::IsMouseDoubleClicked(0)) {
            state.crossfader = 0.5f; // Doble clic resetea al centro exacto
        }

        // Fader Cap Táctil (Tirador Profesional DJ)
        const float cap_w = 22.0f * scale;
        const float cap_h = sld_h + 8.0f * scale;
        const float cap_cx = tr_x0 + state.crossfader * tr_w;
        const ImVec2 cp0 = {cap_cx - cap_w * 0.5f, slot_cy - cap_h * 0.5f};
        const ImVec2 cp1 = {cp0.x + cap_w, cp0.y + cap_h};

        // Sombra suave proyectada por el tirador
        dl->AddRectFilled(ImVec2(cp0.x + 1.5f * scale, cp0.y + 2.0f * scale),
                          ImVec2(cp1.x + 1.5f * scale, cp1.y + 3.0f * scale),
                          IM_COL32(0, 0, 0, 180), 4.0f * scale);
        // Cuerpo metálico oscuro con bisel superior
        dl->AddRectFilled(cp0, cp1, IM_COL32(36, 42, 56, 255), 4.0f * scale);
        dl->AddRectFilled(ImVec2(cp0.x + 1.0f, cp0.y + 1.0f),
                          ImVec2(cp1.x - 1.0f, cp0.y + cap_h * 0.45f),
                          IM_COL32(52, 60, 78, 255), 3.0f * scale);
        // Borde exterior reactivo
        const ImU32 cap_border = (active || hovered) ? IM_COL32(180, 200, 240, 255)
                                                     : IM_COL32(100, 115, 145, 255);
        dl->AddRect(cp0, cp1, cap_border, 4.0f * scale, 0, (active || hovered) ? 1.5f : 1.0f);

        // Ranuras táctiles horizontales
        dl->AddLine(ImVec2(cp0.x + 4.0f * scale, cp0.y + cap_h * 0.28f),
                    ImVec2(cp1.x - 4.0f * scale, cp0.y + cap_h * 0.28f),
                    IM_COL32(20, 24, 34, 220), 1.0f);
        dl->AddLine(ImVec2(cp0.x + 4.0f * scale, cp0.y + cap_h * 0.72f),
                    ImVec2(cp1.x - 4.0f * scale, cp0.y + cap_h * 0.72f),
                    IM_COL32(20, 24, 34, 220), 1.0f);

        // Línea central indicadora en el tirador
        ImU32 cap_indicator = IM_COL32(255, 255, 255, 255);
        if (state.crossfader < 0.46f) {
            cap_indicator = theme::accent_a();
        } else if (state.crossfader > 0.54f) {
            cap_indicator = theme::accent_b();
        }
        dl->AddLine(ImVec2(cap_cx, cp0.y + 3.0f * scale),
                    ImVec2(cap_cx, cp1.y - 3.0f * scale),
                    cap_indicator, 2.0f * scale);
    }

    // --- MASTER (caja oscura con cabecera y fader DJ de alta visibilidad)
    const ImVec2 m0 = {inner_x, boxes_top + box_h + box_gap};
    const ImVec2 m1 = {mn.x + width_px - pad, boxes_top + box_h * 2.0f + box_gap};
    dark_box(m0, m1);
    {
        // Fila 1: Cabecera MASTER + lectura numérica porcentual
        const float hdr_y = m0.y + box_pad;
        const char* lbl = tr(Str::Master);
        dl->AddText(ImVec2(m0.x + box_pad, hdr_y + 1.0f * scale), theme::kText, lbl);

        char mval_buf[32];
        const int pct = static_cast<int>(std::round(state.master * 100.0f));
        std::snprintf(mval_buf, sizeof(mval_buf), "%d%%", pct);
        if (ImFont* mono = imgui_font_mono()) {
            const ImVec2 vsz = mono->CalcTextSizeA(lbl_sz, FLT_MAX, 0.0f, mval_buf);
            dl->AddText(mono, lbl_sz, ImVec2(m1.x - box_pad - vsz.x, hdr_y + 1.0f * scale),
                        theme::accent_a(), mval_buf);
        } else {
            const ImVec2 vsz = ImGui::CalcTextSize(mval_buf);
            dl->AddText(ImVec2(m1.x - box_pad - vsz.x, hdr_y + 1.0f * scale),
                        theme::accent_a(), mval_buf);
        }

        // Fila 2: Track de Fader Master de Alto Contraste
        const float tr_y = hdr_y + hdr_h + 8.0f * scale;
        const float tr_x0 = m0.x + box_pad;
        const float tr_x1 = m1.x - box_pad;
        const float tr_w = tr_x1 - tr_x0;
        const ImVec2 t0 = {tr_x0, tr_y};
        const ImVec2 t1 = {tr_x1, tr_y + sld_h};

        // Riel hundido oscuro
        dl->AddRectFilled(t0, t1, IM_COL32(8, 10, 14, 255), 4.0f * scale);
        dl->AddRect(t0, t1, IM_COL32(40, 48, 64, 255), 4.0f * scale, 0, 1.0f);
        const float slot_cy = (t0.y + t1.y) * 0.5f;

        // Barra de nivel con brillo progresivo
        const float level_w = std::clamp(state.master, 0.0f, 1.0f) * tr_w;
        if (level_w > 2.0f) {
            dl->AddRectFilled(ImVec2(tr_x0 + 2.0f, slot_cy - 3.0f * scale),
                              ImVec2(tr_x0 + level_w, slot_cy + 3.0f * scale),
                              (theme::accent_a() & 0x00FFFFFF) | (180 << 24), 2.0f * scale);
        }
        dl->AddLine(ImVec2(tr_x0 + 4.0f * scale, slot_cy),
                    ImVec2(tr_x1 - 4.0f * scale, slot_cy),
                    IM_COL32(22, 26, 36, 160), 2.0f * scale);

        // Interacción
        ImGui::SetCursorScreenPos(ImVec2(t0.x - 4.0f * scale, t0.y - 4.0f * scale));
        ImGui::InvisibleButton("##master_btn", ImVec2(tr_w + 8.0f * scale, sld_h + 8.0f * scale));
        const bool active = ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();
        if (active) {
            const float mx_pos = ImGui::GetIO().MousePos.x;
            state.master = std::clamp((mx_pos - tr_x0) / tr_w, 0.0f, 1.0f);
        }
        if (hovered && ImGui::IsMouseDoubleClicked(0)) {
            state.master = 0.8f; // Doble clic resetea a nivel nominal
        }

        // Fader Cap Táctil
        const float cap_w = 22.0f * scale;
        const float cap_h = sld_h + 8.0f * scale;
        const float cap_cx = tr_x0 + std::clamp(state.master, 0.0f, 1.0f) * tr_w;
        const ImVec2 cp0 = {cap_cx - cap_w * 0.5f, slot_cy - cap_h * 0.5f};
        const ImVec2 cp1 = {cp0.x + cap_w, cp0.y + cap_h};

        dl->AddRectFilled(ImVec2(cp0.x + 1.5f * scale, cp0.y + 2.0f * scale),
                          ImVec2(cp1.x + 1.5f * scale, cp1.y + 3.0f * scale),
                          IM_COL32(0, 0, 0, 180), 4.0f * scale);
        dl->AddRectFilled(cp0, cp1, IM_COL32(36, 42, 56, 255), 4.0f * scale);
        dl->AddRectFilled(ImVec2(cp0.x + 1.0f, cp0.y + 1.0f),
                          ImVec2(cp1.x - 1.0f, cp0.y + cap_h * 0.45f),
                          IM_COL32(52, 60, 78, 255), 3.0f * scale);
        const ImU32 cap_border = (active || hovered) ? IM_COL32(180, 200, 240, 255)
                                                     : IM_COL32(100, 115, 145, 255);
        dl->AddRect(cp0, cp1, cap_border, 4.0f * scale, 0, (active || hovered) ? 1.5f : 1.0f);

        dl->AddLine(ImVec2(cp0.x + 4.0f * scale, cp0.y + cap_h * 0.28f),
                    ImVec2(cp1.x - 4.0f * scale, cp0.y + cap_h * 0.28f),
                    IM_COL32(20, 24, 34, 220), 1.0f);
        dl->AddLine(ImVec2(cp0.x + 4.0f * scale, cp0.y + cap_h * 0.72f),
                    ImVec2(cp1.x - 4.0f * scale, cp0.y + cap_h * 0.72f),
                    IM_COL32(20, 24, 34, 220), 1.0f);

        dl->AddLine(ImVec2(cap_cx, cp0.y + 3.0f * scale),
                    ImVec2(cap_cx, cp1.y - 3.0f * scale),
                    IM_COL32(255, 255, 255, 255), 2.0f * scale);
    }
}

} // namespace melodeus::app
