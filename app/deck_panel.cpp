#include "deck_panel.hpp"

#include "i18n.hpp"
#include "library_panel.hpp"
#include "render/imgui_layer.hpp"
#include "theme.hpp"

#include "imgui.h"

#include <cmath>
#include <cstdio>

namespace melodeus::app {

namespace {

constexpr float kPi = 3.14159265358979323846f;

void format_time(char* buf, std::size_t cap, double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int total = static_cast<int>(seconds);
    std::snprintf(buf, cap, "%d:%02d", total / 60, total % 60);
}

/// Drop target genérico sobre el último ítem: acepta un payload de la
/// biblioteca (local u online) y devuelve las filas (o -1).
void accept_track_drop(int* out_local, int* out_online) {
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kTrackDragType)) {
            IM_ASSERT(payload->DataSize == sizeof(TrackDragPayload));
            if (out_local != nullptr) {
                *out_local = static_cast<const TrackDragPayload*>(payload->Data)->row;
            }
        } else if (const ImGuiPayload* payload_onl = ImGui::AcceptDragDropPayload(kOnlineTrackDragType)) {
            IM_ASSERT(payload_onl->DataSize == sizeof(OnlineTrackDragPayload));
            if (out_online != nullptr) {
                *out_online = static_cast<const OnlineTrackDragPayload*>(payload_onl->Data)->index;
            }
        }
        ImGui::EndDragDropTarget();
    }
}

/// Knob circular de pitch: arrastre vertical; doble-click = 0. Rango configurable
/// (±8%, ±16%, ±50%, ±100%).
bool pitch_knob(float* v, float radius, float range_max, ImU32 accent_col) {
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##pitchknob", ImVec2(radius * 2.0f, radius * 2.0f));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    bool changed = false;
    const float rmax = range_max > 0.0f ? range_max : 16.0f;
    if (active) {
        const float speed = rmax / 200.0f;
        const float nv = *v - ImGui::GetIO().MouseDelta.y * speed;
        if (nv != *v) {
            *v = nv < -rmax ? -rmax : (nv > rmax ? rmax : nv);
            changed = true;
        }
        if (ImGui::IsMouseDoubleClicked(0)) {
            *v = 0.0f;
            changed = true;
        }
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c = {mn.x + radius, mn.y + radius};
    dl->AddCircleFilled(c, radius, theme::kGlassFillStrong, 24);
    const ImU32 edge =
        active ? accent_col : (hovered ? IM_COL32(110, 122, 148, 255) : theme::kGlassBorder);
    dl->AddCircle(c, radius, edge, 24, active || hovered ? 1.5f : 1.0f);
    // Centrado: 0% a las 12:00, ±135° hacia los lados (igual que el mixer).
    constexpr float kTop = 0.5f * kPi;
    constexpr float kHalf = 0.75f * kPi;
    float a;
    if (*v <= 0.0f) {
        a = kTop + (-*v / rmax) * kHalf;
    } else {
        a = kTop - (*v / rmax) * kHalf;
    }
    if (std::fabs(*v) > 0.05f) {
        dl->PathClear();
        const int segs = 20;
        for (int i = 0; i <= segs; ++i) {
            const float t = kTop + (a - kTop) * (static_cast<float>(i) / segs);
            dl->PathLineTo(
                ImVec2(c.x + std::cos(t) * (radius - 2.0f), c.y - std::sin(t) * (radius - 2.0f)));
        }
        dl->PathStroke(accent_col, 0, 2.0f);
    }
    dl->AddLine(ImVec2(c.x + std::cos(a) * (radius * 0.25f), c.y - std::sin(a) * (radius * 0.25f)),
                ImVec2(c.x + std::cos(a) * (radius - 4.0f), c.y - std::sin(a) * (radius - 4.0f)),
                theme::kTextHot, 2.0f);
    char b[16];
    std::snprintf(b, sizeof(b), "%.1f", static_cast<double>(*v));
    if (ImFont* mono = imgui_font_mono()) {
        const ImVec2 ts = ImGui::CalcTextSize(b);
        dl->AddText(mono, mono->LegacySize, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f),
                    theme::kText, b);
    }
    return changed;
}

/// Trunca una cadena con elipsis ("…") para que quepa en max_w píxeles.
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

/// Badge moderno con estilo glass hairline y redondeo consistente (20px altura estándar).
float draw_badge(ImDrawList* dl, ImFont* font, const ImVec2& pos, const char* text, ImU32 border,
                 ImU32 fill, ImU32 text_col, float font_size, float scale, float h_px = 20.0f,
                 float radius_px = 5.0f, float border_th = 1.0f) {
    const ImVec2 ts = (font != nullptr) ? font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text)
                                        : ImGui::CalcTextSize(text);
    const float pad_x = 7.0f * scale;
    const float h = h_px * scale;
    const float r = radius_px * scale;
    const ImVec2 p0 = pos;
    const ImVec2 p1 = {pos.x + ts.x + pad_x * 2.0f, pos.y + h};
    dl->AddRectFilled(p0, p1, fill, r);
    dl->AddRect(p0, p1, border, r, 0, border_th);
    const ImVec2 tpos = {p0.x + pad_x, p0.y + (h - ts.y) * 0.5f};
    if (font != nullptr) {
        dl->AddText(font, font_size, tpos, text_col, text);
    } else {
        dl->AddText(tpos, text_col, text);
    }
    return ts.x + pad_x * 2.0f;
}

} // namespace

DeckPanelOut draw_deck_panel(const DeckUiState& state, int deck_idx, float width_px, float scale) {
    DeckPanelOut out{};
    ImGui::PushID(deck_idx);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* ui = imgui_font_ui();
    ImFont* bold = imgui_font_bold();
    ImFont* mono = imgui_font_mono();
    const ImU32 accent = (deck_idx == 0) ? theme::accent_a() : theme::accent_b();
    const ImU32 accent_dim = (deck_idx == 0) ? theme::accent_a_dim() : theme::accent_b_dim();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    char buf[96];

    // Métricas escaladas para consistencia en el display (PANEL 1).
    const float pad = 12.0f * scale;        // p-3
    const float gap = 12.0f * scale;        // gap-3
    const float bpm_sz = 30.0f * scale;     // text-3xl
    const float lbl_sz = 11.5f * scale;     // text-xs
    const float title_sz = 14.5f * scale;   // text-sm (bold)
    const float artist_sz = 12.0f * scale;  // text-xs (dim)
    const float badge_font_sz = 11.0f * scale; // font para badges
    const float pill_sz = 10.5f * scale;    // text-[10.5px] para badges pequeños en barra
    const float badge_h = 20.0f * scale;    // Altura universal para todos los badges
    const float badge_r = 5.0f * scale;     // Radio universal para badges
    const float ov_h = 24.0f * scale;       // h-6
    const float dt_h = 64.0f * scale;       // h-16
    const float ts_sz = 12.5f * scale;      // tamaño para timestamps
    const float ts_h = 20.0f * scale;       // altura para timestamps
    const float pb3 = 12.0f * scale;        // pb-3 (info)

    // Altura del bloque BPM: número + mt-1 + label.
    const float bpm_block_h = bpm_sz + 4.0f * scale + lbl_sz + 2.0f * scale;
    const float info_h = bpm_block_h + pb3; // content + pb-3
    const float panel_h =
        pad + info_h + gap + ov_h + gap + dt_h + 4.0f * scale + ts_h + pad;

    // =======================================================================
    // PANEL 1 (glass): info + waveforms + timestamps.
    // =======================================================================
    const ImVec2 p_min = origin;
    const ImVec2 p_max = {origin.x + width_px, origin.y + panel_h};
    theme::glass(dl, p_min, p_max);

    // --- Fila de info: BPM (izq) | divider | título + badges (der) ----------
    const float info_top = p_min.y + pad;
    float x = p_min.x + pad;
    // BPM grande: efectivo (con pitch). Es lo que suena de verdad.
    const double effective_bpm = (state.bpm > 0.0) ? state.bpm_pitched : 0.0;
    if (effective_bpm > 0.0) {
        std::snprintf(buf, sizeof(buf), "%.1f", effective_bpm);
    } else {
        std::snprintf(buf, sizeof(buf), "--.-");
    }
    if (bold != nullptr) {
        dl->AddText(bold, bpm_sz, ImVec2(x, info_top), theme::kTextHot, buf);
    } else {
        dl->AddText(ImVec2(x, info_top), theme::kTextHot, buf);
    }
    const ImVec2 bpm_ts =
        bold != nullptr ? bold->CalcTextSizeA(bpm_sz, FLT_MAX, 0.0f, buf) : ImGui::CalcTextSize(buf);
    // Label "BPM"; si hay pitch, muestra el BPM base debajo del efectivo.
    char bpm_lbl_buf[32];
    if (state.bpm > 0.0 && std::fabs(state.pitch_pct) > 0.05) {
        std::snprintf(bpm_lbl_buf, sizeof(bpm_lbl_buf), "%s  %.1f", tr(Str::Bpm), state.bpm);
    } else {
        std::snprintf(bpm_lbl_buf, sizeof(bpm_lbl_buf), "%s", tr(Str::Bpm));
    }
    const ImVec2 bpm_lbl_pos = ImVec2(x, info_top + bpm_sz + 3.0f * scale);
    if (ui != nullptr) {
        dl->AddText(ui, lbl_sz, bpm_lbl_pos, theme::kTextDim, bpm_lbl_buf);
    } else {
        dl->AddText(bpm_lbl_pos, theme::kTextDim, bpm_lbl_buf);
    }
    // Divider vertical tras el bloque BPM
    const float bpm_w = (bpm_ts.x > lbl_sz * 4.0f ? bpm_ts.x : lbl_sz * 4.0f);
    const float div_x = x + bpm_w + 14.0f * scale;
    dl->AddLine(ImVec2(div_x, info_top),
                ImVec2(div_x, info_top + bpm_block_h), theme::kGlassBorder, 1.0f);

    // Lado derecho del header: Fila 1 (Título y Badges de Estado) | Fila 2 (Artista y Módulo Key)
    const float rx = div_x + 14.0f * scale;
    const float rr = p_max.x - pad;

    // --- FILA 1: Título (izq) y Badges de Estado [DECK], [SYNC], [CARGANDO] (der) ---
    const float row1_y = info_top + 1.0f * scale;
    float cur_r1_x = rr;

    const char* deck_lbl = (deck_idx == 0) ? tr(Str::DeckA) : tr(Str::DeckB);
    const ImVec2 deck_ts = bold ? bold->CalcTextSizeA(badge_font_sz, FLT_MAX, 0.0f, deck_lbl)
                                : ImGui::CalcTextSize(deck_lbl);
    const float deck_w = deck_ts.x + 14.0f * scale;
    cur_r1_x -= deck_w;
    draw_badge(dl, bold, ImVec2(cur_r1_x, row1_y), deck_lbl, accent, accent_dim, accent,
               badge_font_sz, scale, 20.0f, 5.0f, 1.5f);

    if (state.sync_on) {
        const ImVec2 sync_ts = bold ? bold->CalcTextSizeA(badge_font_sz, FLT_MAX, 0.0f, tr(Str::Sync))
                                    : ImGui::CalcTextSize(tr(Str::Sync));
        const float sync_w = sync_ts.x + 14.0f * scale;
        cur_r1_x -= (sync_w + 5.0f * scale);
        draw_badge(dl, bold, ImVec2(cur_r1_x, row1_y), tr(Str::Sync), accent, accent_dim, accent,
                   badge_font_sz, scale, 20.0f, 5.0f, 1.5f);
    }

    if (state.is_loading) {
        const char* load_lbl = tr(Str::LoadingBadge);
        const ImVec2 load_ts = bold ? bold->CalcTextSizeA(badge_font_sz, FLT_MAX, 0.0f, load_lbl)
                                    : ImGui::CalcTextSize(load_lbl);
        const float load_w = load_ts.x + 14.0f * scale;
        cur_r1_x -= (load_w + 5.0f * scale);
        draw_badge(dl, bold, ImVec2(cur_r1_x, row1_y), load_lbl, IM_COL32(255, 217, 102, 255),
                   IM_COL32(255, 217, 102, 45), IM_COL32(255, 217, 102, 255), badge_font_sz,
                   scale, 20.0f, 5.0f, 1.0f);
    }

    // Título de la pista recortado con elipsis para no chocar con los badges de estado
    const float title_max_w = (cur_r1_x - 8.0f * scale) - rx;
    const char* raw_title = (state.title != nullptr && state.title[0] != '\0') ? state.title : "---";
    const std::string title_disp = ellipsize(raw_title, title_max_w, bold, title_sz);
    const float title_y = row1_y + (badge_h - title_sz) * 0.5f;
    if (bold != nullptr) {
        dl->AddText(bold, title_sz, ImVec2(rx, title_y), theme::kTextHot, title_disp.c_str());
    } else {
        dl->AddText(ImVec2(rx, title_y), theme::kTextHot, title_disp.c_str());
    }

    // --- FILA 2: Artista / Álbum (izq) y Módulo de Tonalidad (der) ---
    const float row2_y = info_top + 26.0f * scale;

    // Calcular ancho total del módulo de tonalidad
    const bool has_key = (state.camelot > 0 || std::fabs(state.key_shift) > 0.05);
    char key_buf[32];
    if (has_key) {
        if (state.camelot > 0) {
            if (std::fabs(state.key_shift) > 0.05) {
                std::snprintf(key_buf, sizeof(key_buf), "%d%c (%+.0f)", state.camelot,
                              state.camelot_minor ? 'A' : 'B', state.key_shift);
            } else {
                std::snprintf(key_buf, sizeof(key_buf), "%d%c", state.camelot,
                              state.camelot_minor ? 'A' : 'B');
            }
        } else {
            std::snprintf(key_buf, sizeof(key_buf), "KEY %+.0f", state.key_shift);
        }
    } else {
        std::snprintf(key_buf, sizeof(key_buf), "--");
    }

    const ImVec2 kb_ts = (mono != nullptr) ? mono->CalcTextSizeA(badge_font_sz, FLT_MAX, 0.0f, key_buf)
                                           : ImGui::CalcTextSize(key_buf);
    const float kb_w = kb_ts.x + 14.0f * scale;

    const float kstep_total_w = 36.0f * scale;
    const bool has_kn = (state.key_name != nullptr && state.key_name[0] != '\0');
    float kn_w = 0.0f;
    if (has_kn) {
        const ImVec2 kn_ts = (mono != nullptr) ? mono->CalcTextSizeA(badge_font_sz, FLT_MAX, 0.0f, state.key_name)
                                               : ImGui::CalcTextSize(state.key_name);
        kn_w = kn_ts.x + 14.0f * scale;
    }

    float key_module_total_w = kb_w;
    if (has_key) {
        key_module_total_w += 4.0f * scale + kstep_total_w;
    }
    if (has_kn) {
        key_module_total_w += 4.0f * scale + kn_w;
    }

    // Dibujar módulo de tonalidad alineado a la derecha
    float kx = rr - key_module_total_w;

    // 1) Badge Camelot / Key
    draw_badge(dl, mono, ImVec2(kx, row2_y), key_buf, has_key ? accent : theme::kGlassBorder,
               has_key ? accent_dim : IM_COL32(22, 26, 36, 180), has_key ? accent : theme::kTextDim,
               badge_font_sz, scale, 20.0f, 5.0f, has_key ? 1.5f : 1.0f);
    ImGui::SetCursorScreenPos(ImVec2(kx, row2_y));
    if (ImGui::InvisibleButton("##key_badge_btn", ImVec2(kb_w, badge_h))) {
        out.key_shift_reset = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::KeyResetTooltip));
    }
    kx += kb_w + 4.0f * scale;

    // 2) Mini Stepper [-] [+]
    if (has_key) {
        const ImVec2 p0(kx, row2_y);
        const ImVec2 p1(kx + kstep_total_w, row2_y + badge_h);
        dl->AddRectFilled(p0, p1, IM_COL32(20, 24, 34, 220), badge_r);
        dl->AddRect(p0, p1, theme::kGlassBorder, badge_r, 0, 1.0f);
        dl->AddLine(ImVec2(kx + 18.0f * scale, row2_y + 3.0f * scale),
                    ImVec2(kx + 18.0f * scale, row2_y + badge_h - 3.0f * scale),
                    theme::kGlassBorder, 1.0f);

        ImGui::SetCursorScreenPos(p0);
        if (ImGui::InvisibleButton("##kdown", ImVec2(18.0f * scale, badge_h))) {
            out.key_shift_delta = -1;
        }
        const bool kd_hov = ImGui::IsItemHovered();
        if (kd_hov) {
            ImGui::SetTooltip("%s", tr(Str::KeySemiDownTooltip));
            dl->AddRectFilled(p0, ImVec2(kx + 18.0f * scale, row2_y + badge_h),
                              IM_COL32(255, 255, 255, 25), badge_r, ImDrawFlags_RoundCornersLeft);
        }
        const ImVec2 dm_ts = ImGui::CalcTextSize("-");
        dl->AddText(ImVec2(kx + (18.0f * scale - dm_ts.x) * 0.5f,
                           row2_y + (badge_h - dm_ts.y) * 0.5f),
                    kd_hov ? theme::kTextHot : theme::kTextDim, "-");

        const ImVec2 kp_pos(kx + 18.0f * scale, row2_y);
        ImGui::SetCursorScreenPos(kp_pos);
        if (ImGui::InvisibleButton("##kup", ImVec2(18.0f * scale, badge_h))) {
            out.key_shift_delta = +1;
        }
        const bool kp_hov = ImGui::IsItemHovered();
        if (kp_hov) {
            ImGui::SetTooltip("%s", tr(Str::KeySemiUpTooltip));
            dl->AddRectFilled(kp_pos, p1, IM_COL32(255, 255, 255, 25), badge_r,
                              ImDrawFlags_RoundCornersRight);
        }
        const ImVec2 dp_ts = ImGui::CalcTextSize("+");
        dl->AddText(ImVec2(kx + 18.0f * scale + (18.0f * scale - dp_ts.x) * 0.5f,
                           row2_y + (badge_h - dp_ts.y) * 0.5f),
                    kp_hov ? theme::kTextHot : theme::kTextDim, "+");
        kx += kstep_total_w + 4.0f * scale;
    }

    // 3) Badge tonalidad musical (ej. "Am", "C#m")
    if (has_kn) {
        draw_badge(dl, mono, ImVec2(kx, row2_y), state.key_name, theme::kGlassBorder,
                   IM_COL32(24, 28, 38, 200), theme::kTextHot, badge_font_sz, scale,
                   20.0f, 5.0f, 1.0f);
    }

    // Artista / Álbum a la izquierda de la fila 2
    char sub_buf[160] = {};
    if (state.artist != nullptr && state.artist[0] != '\0') {
        if (state.album != nullptr && state.album[0] != '\0') {
            std::snprintf(sub_buf, sizeof(sub_buf), "%s  \xC2\xB7  %s", state.artist, state.album);
        } else {
            std::snprintf(sub_buf, sizeof(sub_buf), "%s", state.artist);
        }
    }
    if (sub_buf[0] != '\0') {
        const float artist_max_w = (rr - key_module_total_w - 8.0f * scale) - rx;
        const std::string artist_disp = ellipsize(sub_buf, artist_max_w, ui, artist_sz);
        const float artist_y = row2_y + (badge_h - artist_sz) * 0.5f;
        if (ui != nullptr) {
            dl->AddText(ui, artist_sz, ImVec2(rx, artist_y), theme::kTextDim, artist_disp.c_str());
        } else {
            dl->AddText(ImVec2(rx, artist_y), theme::kTextDim, artist_disp.c_str());
        }
    }

    // Divider horizontal bajo la info (border-b tras pb-3).
    const float hdiv_y = info_top + bpm_block_h + pb3 - 2.0f * scale;
    dl->AddLine(ImVec2(p_min.x + pad, hdiv_y), ImVec2(p_max.x - pad, hdiv_y), theme::kGlassBorder,
                1.0f);

    // --- Carriles de onda ---------------------------------------------------
    const auto lane = [&](const char* id, float h, ImU32 bg, bool is_overview) {
        const ImVec2 mn2 = ImGui::GetCursorScreenPos();
        const ImVec2 mx2 = {mn2.x + width_px - pad * 2.0f, mn2.y + h};
        ImGui::SetCursorScreenPos(mn2);
        ImGui::InvisibleButton(id, ImVec2(mx2.x - mn2.x, h));
        const bool hover = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const float wave_r = 6.0f * scale;
        dl->AddRectFilled(mn2, mx2, bg, wave_r);
        dl->AddRect(mn2, mx2, hover ? accent : theme::kGlassBorder, wave_r, 0,
                    hover ? 1.5f : 1.0f);
        if ((hover && ImGui::IsMouseClicked(0)) || (held && ImGui::IsMouseDown(0))) {
            const float mx = ImGui::GetIO().MousePos.x;
            float frac = (mx - mn2.x) / (mx2.x - mn2.x);
            frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
            double t = 0.0;
            if (is_overview) {
                t = frac * state.duration_sec;
            } else {
                t = state.position_sec + (frac - 0.5) * state.detail_span_sec;
            }
            if (t < 0.0) {
                t = 0.0;
            }
            if (state.duration_sec > 0.0 && t > state.duration_sec) {
                t = state.duration_sec;
            }
            out.seek_requested = true;
            out.seek_seconds = t;
        }
        int drop_loc = -1, drop_onl = -1;
        accept_track_drop(&drop_loc, &drop_onl);
        if (drop_loc >= 0 && out.drop_row < 0) {
            out.drop_row = drop_loc;
        }
        if (drop_onl >= 0 && out.drop_online_row < 0) {
            out.drop_online_row = drop_onl;
        }
        return RECT{static_cast<LONG>(mn2.x), static_cast<LONG>(mn2.y), static_cast<LONG>(mx2.x),
                    static_cast<LONG>(mx2.y)};
    };
    const float ov_y = origin.y + pad + info_h + gap;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, ov_y));
    out.rects.overview = lane("##ov", ov_h, IM_COL32(18, 20, 27, 160), true);

    const float dt_y = ov_y + ov_h + gap;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, dt_y));
    out.rects.detail = lane("##dt", dt_h, IM_COL32(16, 18, 25, 190), false);

    // --- Timestamps debajo del detail (mt-1, mono) --------------------------
    char t0[16];
    char t1[16];
    char t_total[16];
    format_time(t0, sizeof(t0), state.position_sec);
    const double rem_sec = state.duration_sec - state.position_sec;
    format_time(t1, sizeof(t1), rem_sec);
    format_time(t_total, sizeof(t_total), state.duration_sec);

    const float ts_y = dt_y + dt_h + 4.0f * scale;

    // Transcurrido (izquierda):
    const ImVec2 ts_left = {p_min.x + pad, ts_y};
    if (mono != nullptr) {
        dl->AddText(mono, ts_sz, ts_left, theme::kTextHot, t0);
    } else {
        dl->AddText(ts_left, theme::kTextHot, t0);
    }

    // Duración Total (centro):
    if (state.duration_sec > 0.0) {
        char total_buf[32];
        std::snprintf(total_buf, sizeof(total_buf), "TOTAL %s", t_total);
        const ImVec2 tot_sz = (mono != nullptr)
                                  ? mono->CalcTextSizeA(11.0f * scale, FLT_MAX, 0.0f, total_buf)
                                  : ImGui::CalcTextSize(total_buf);
        const float center_x = (p_min.x + p_max.x - tot_sz.x) * 0.5f;
        const ImVec2 tot_pos = {center_x, ts_y + (ts_sz - 11.0f * scale) * 0.5f};
        if (mono != nullptr) {
            dl->AddText(mono, 11.0f * scale, tot_pos, theme::kTextDim, total_buf);
        } else {
            dl->AddText(tot_pos, theme::kTextDim, total_buf);
        }
    }

    // Restante con Alerta DJ de final de pista (< 30s) (derecha):
    std::snprintf(buf, sizeof(buf), "-%s", t1);
    const ImVec2 tsz =
        mono != nullptr ? mono->CalcTextSizeA(ts_sz, FLT_MAX, 0.0f, buf) : ImGui::CalcTextSize(buf);
    const ImVec2 ts_right = {p_max.x - pad - tsz.x, ts_y};

    ImU32 rem_col = theme::kTextDim;
    if (state.playing && rem_sec > 0.0 && rem_sec < 30.0) {
        const bool blink = (static_cast<int>(ImGui::GetTime() * 3.0) % 2 == 0);
        rem_col = blink ? IM_COL32(255, 140, 50, 255) : IM_COL32(255, 65, 65, 255);
    }

    if (mono != nullptr) {
        dl->AddText(mono, ts_sz, ts_right, rem_col, buf);
    } else {
        dl->AddText(ts_right, rem_col, buf);
    }

    // =======================================================================
    // PANEL 2 (glass): PLAY, CUE | divider | hot cues 1/2/3 || PITCH knob.
    // =======================================================================
    // Posición absoluta desde origin (igual que arriba): el cursor acumula
    // ItemSpacing y no refleja la geometría del panel.
    const float ctrl_h = 48.0f * scale; // p-2 + btn h-8
    const ImVec2 ctrl_min = {origin.x, origin.y + panel_h + 8.0f * scale};
    const ImVec2 ctrl_max = {origin.x + width_px, ctrl_min.y + ctrl_h};
    theme::glass(dl, ctrl_min, ctrl_max);

    const float bar_btn_h = 30.0f * scale; // Altura universal para todos los botones
    const float bar_btn_y = ctrl_min.y + (ctrl_h - bar_btn_h) * 0.5f;
    const float bar_r = 6.0f * scale;      // Radio universal de esquinas
    const float div_h = 22.0f * scale;     // Altura universal de divisores verticales
    const float div_y = ctrl_min.y + (ctrl_h - div_h) * 0.5f;

    struct ToolbarBtnResult {
        bool clicked = false;
        bool right_clicked = false;
        bool hovered = false;
    };

    const auto draw_bar_btn = [&](const char* id, const char* label, ImFont* font, float font_sz,
                                  float x_pos, float y_pos, float w, float h,
                                  bool active, ImU32 active_bg, ImU32 active_border, ImU32 active_text,
                                  const char* tooltip = nullptr, bool check_right_click = false) -> ToolbarBtnResult {
        ToolbarBtnResult res;
        ImGui::SetCursorScreenPos(ImVec2(x_pos, y_pos));
        res.clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
        res.hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        if (check_right_click && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            res.right_clicked = true;
        }

        if (res.hovered && tooltip != nullptr) {
            ImGui::SetTooltip("%s", tooltip);
        }

        const ImVec2 p0(x_pos, y_pos);
        const ImVec2 p1(x_pos + w, y_pos + h);

        // Fondo glass unificado:
        ImU32 bg_col;
        if (active) {
            bg_col = held ? active_border : active_bg;
        } else if (res.hovered) {
            bg_col = held ? IM_COL32(50, 60, 80, 240) : IM_COL32(38, 46, 62, 220);
        } else {
            bg_col = IM_COL32(24, 28, 38, 200);
        }
        dl->AddRectFilled(p0, p1, bg_col, bar_r);

        // Borde fino glass hairline:
        ImU32 border_col;
        float border_th = 1.0f;
        if (active) {
            border_col = active_border;
            border_th = 1.5f;
        } else if (res.hovered) {
            border_col = IM_COL32(140, 155, 185, 255);
        } else {
            border_col = theme::kGlassBorder;
        }
        dl->AddRect(p0, p1, border_col, bar_r, 0, border_th);

        // Texto o Icono centrado geométricamente:
        if (label != nullptr && label[0] != '\0') {
            const ImVec2 ts = (font != nullptr)
                                  ? font->CalcTextSizeA(font_sz, FLT_MAX, 0.0f, label)
                                  : ImGui::CalcTextSize(label);
            const ImVec2 tpos(x_pos + (w - ts.x) * 0.5f, y_pos + (h - ts.y) * 0.5f);
            ImU32 text_col;
            if (active) {
                text_col = active_text;
            } else if (res.hovered) {
                text_col = theme::kTextHot;
            } else {
                text_col = (active_text != 0) ? active_text : theme::kText;
            }
            if (font != nullptr) {
                dl->AddText(font, font_sz, tpos, text_col, label);
            } else {
                dl->AddText(tpos, text_col, label);
            }
        }
        return res;
    };

    float cur_x = ctrl_min.x + 8.0f * scale;

    // --- 1. TRANSPORTE: PLAY & CUE -----------------------------------------
    const float play_w = 40.0f * scale;
    const std::string play_icon =
        theme::icons::utf8(state.playing ? theme::icons::pause : theme::icons::play);
    const auto res_play = draw_bar_btn(
        "##bar_play", play_icon.c_str(), ui, 14.0f * scale, cur_x, bar_btn_y, play_w, bar_btn_h,
        state.playing, IM_COL32(34, 142, 68, 220), IM_COL32(48, 205, 96, 255), theme::kTextHot,
        tr(Str::PlayTooltip));
    if (res_play.clicked) {
        out.play_toggled = true;
    }
    cur_x += play_w + 5.0f * scale;

    const float cue_w = 40.0f * scale;
    const std::string cue_icon = theme::icons::utf8(theme::icons::stop_circle);
    const bool cue_at_start = (!state.playing && state.position_sec <= 0.05);
    const auto res_cue = draw_bar_btn(
        "##bar_cue", cue_icon.c_str(), ui, 14.0f * scale, cur_x, bar_btn_y, cue_w, bar_btn_h,
        cue_at_start, IM_COL32(190, 115, 20, 220), IM_COL32(245, 160, 30, 255),
        cue_at_start ? theme::kTextHot : IM_COL32(240, 165, 45, 220), tr(Str::CueTooltip));
    if (res_cue.clicked) {
        out.cue_pressed = true;
    }
    cur_x += cue_w;

    // --- DIVIDER 1: Transporte | Hot Cues -----------------------------------
    cur_x += 7.0f * scale;
    dl->AddLine(ImVec2(cur_x, div_y), ImVec2(cur_x, div_y + div_h), theme::kGlassBorder, 1.0f);
    cur_x += 7.0f * scale;

    // --- 2. HOT CUES (1, 2, 3) ---------------------------------------------
    const float hc_sz = bar_btn_h; // 30x30px: botones cuadrados perfectamente alineados
    for (int s = 0; s < 3; ++s) {
        const bool has = state.hot_cues[s] >= 0.0;
        char lbl[8];
        std::snprintf(lbl, sizeof(lbl), "%d", s + 1);
        char btn_id[16];
        std::snprintf(btn_id, sizeof(btn_id), "##hc_%d", s);
        char tip[128];
        std::snprintf(tip, sizeof(tip), has ? tr(Str::HotCueJump) : tr(Str::HotCueSet), s + 1);

        const auto res_hc = draw_bar_btn(
            btn_id, lbl, bold, 12.0f * scale, cur_x, bar_btn_y, hc_sz, hc_sz,
            has, accent_dim, accent, has ? theme::kTextHot : theme::kTextDim,
            tip, true);

        if (res_hc.clicked) {
            out.hot_cue_slot = s;
            out.hot_cue_set = !has;
        }
        if (has && res_hc.right_clicked) {
            out.hot_cue_clear = s;
        }
        cur_x += hc_sz + (s < 2 ? 4.0f * scale : 0.0f);
    }

    // --- DIVIDER 2: Hot Cues | Loop -----------------------------------------
    cur_x += 7.0f * scale;
    dl->AddLine(ImVec2(cur_x, div_y), ImVec2(cur_x, div_y + div_h), theme::kGlassBorder, 1.0f);
    cur_x += 7.0f * scale;

    // --- 3. LOOP SECTION: Botón [ LOOP ] + Stepper [ ‹  4  › ] --------------
    const float loop_btn_w = 42.0f * scale;
    const auto res_loop = draw_bar_btn(
        "##loop_toggle", "LOOP", bold, 11.0f * scale, cur_x, bar_btn_y, loop_btn_w, bar_btn_h,
        state.loop_on, accent_dim, accent, theme::kTextHot, tr(Str::LoopTooltip));
    if (res_loop.clicked) {
        out.loop_toggled = true;
    }
    cur_x += loop_btn_w + 4.0f * scale;

    // Control segmentado integrado para selección de compases:
    const float stepper_w = 68.0f * scale;
    const float arr_w = 20.0f * scale;
    const float val_w = stepper_w - arr_w * 2.0f; // 28px para el valor central

    const ImVec2 st0(cur_x, bar_btn_y);
    const ImVec2 st1(cur_x + stepper_w, bar_btn_y + bar_btn_h);
    dl->AddRectFilled(st0, st1, IM_COL32(18, 22, 30, 220), bar_r);
    dl->AddRect(st0, st1, state.loop_on ? accent : theme::kGlassBorder, bar_r, 0,
                state.loop_on ? 1.5f : 1.0f);

    // Divisores sutiles internos del stepper:
    dl->AddLine(ImVec2(cur_x + arr_w, bar_btn_y + 4.0f * scale),
                ImVec2(cur_x + arr_w, bar_btn_y + bar_btn_h - 4.0f * scale),
                theme::kGlassBorder, 1.0f);
    dl->AddLine(ImVec2(cur_x + arr_w + val_w, bar_btn_y + 4.0f * scale),
                ImVec2(cur_x + arr_w + val_w, bar_btn_y + bar_btn_h - 4.0f * scale),
                theme::kGlassBorder, 1.0f);

    // Segmento decremento ‹:
    ImGui::SetCursorScreenPos(st0);
    if (ImGui::InvisibleButton("##loopdec", ImVec2(arr_w, bar_btn_h))) {
        out.loop_len_delta = -1;
    }
    const bool dec_hov = ImGui::IsItemHovered();
    if (dec_hov) {
        ImGui::SetTooltip("%s", tr(Str::LoopShorten));
        dl->AddRectFilled(st0, ImVec2(cur_x + arr_w, bar_btn_y + bar_btn_h),
                          IM_COL32(255, 255, 255, 25), bar_r, ImDrawFlags_RoundCornersLeft);
    }
    const ImVec2 dec_ts = ImGui::CalcTextSize("<");
    dl->AddText(ImVec2(cur_x + (arr_w - dec_ts.x) * 0.5f,
                       bar_btn_y + (bar_btn_h - dec_ts.y) * 0.5f),
                dec_hov ? theme::kTextHot : theme::kTextDim, "<");

    // Segmento valor:
    char lbuf[16];
    if (state.bpm > 0.0) {
        const double beats = state.loop_len_sec * state.bpm / 60.0;
        if (beats < 0.1875) {
            std::snprintf(lbuf, sizeof(lbuf), "1/8");
        } else if (beats < 0.375) {
            std::snprintf(lbuf, sizeof(lbuf), "1/4");
        } else if (beats < 0.75) {
            std::snprintf(lbuf, sizeof(lbuf), "1/2");
        } else {
            std::snprintf(lbuf, sizeof(lbuf), "%.0f", beats);
        }
    } else {
        std::snprintf(lbuf, sizeof(lbuf), "%.1f", state.loop_len_sec);
    }
    const ImVec2 val_ts = (mono != nullptr)
                              ? mono->CalcTextSizeA(pill_sz, FLT_MAX, 0.0f, lbuf)
                              : ImGui::CalcTextSize(lbuf);
    const ImVec2 val_pos(cur_x + arr_w + (val_w - val_ts.x) * 0.5f,
                         bar_btn_y + (bar_btn_h - val_ts.y) * 0.5f);
    if (mono != nullptr) {
        dl->AddText(mono, pill_sz, val_pos, state.loop_on ? accent : theme::kTextHot, lbuf);
    } else {
        dl->AddText(val_pos, state.loop_on ? accent : theme::kTextHot, lbuf);
    }

    // Segmento incremento ›:
    const float inc_x = cur_x + arr_w + val_w;
    ImGui::SetCursorScreenPos(ImVec2(inc_x, bar_btn_y));
    if (ImGui::InvisibleButton("##loopinc", ImVec2(arr_w, bar_btn_h))) {
        out.loop_len_delta = 1;
    }
    const bool inc_hov = ImGui::IsItemHovered();
    if (inc_hov) {
        ImGui::SetTooltip("%s", tr(Str::LoopLengthen));
        dl->AddRectFilled(ImVec2(inc_x, bar_btn_y), st1,
                          IM_COL32(255, 255, 255, 25), bar_r, ImDrawFlags_RoundCornersRight);
    }
    const ImVec2 inc_ts = ImGui::CalcTextSize(">");
    dl->AddText(ImVec2(inc_x + (arr_w - inc_ts.x) * 0.5f,
                       bar_btn_y + (bar_btn_h - inc_ts.y) * 0.5f),
                inc_hov ? theme::kTextHot : theme::kTextDim, ">");
    cur_x += stepper_w;

    // --- DIVIDER 3: Loop | Performance (Sync / MT) --------------------------
    cur_x += 7.0f * scale;
    dl->AddLine(ImVec2(cur_x, div_y), ImVec2(cur_x, div_y + div_h), theme::kGlassBorder, 1.0f);
    cur_x += 7.0f * scale;

    // --- 4. PERFORMANCE: SYNC & MASTER TEMPO --------------------------------
    const float sync_w = 44.0f * scale;
    const auto res_sync = draw_bar_btn(
        "##bar_sync", tr(Str::Sync), bold, 11.0f * scale, cur_x, bar_btn_y, sync_w, bar_btn_h,
        state.sync_on, accent_dim, accent, theme::kTextHot, tr(Str::SyncTooltip));
    if (res_sync.clicked) {
        out.sync_pressed = true;
    }
    cur_x += sync_w + 4.0f * scale;

    const float mt_w = 32.0f * scale;
    const auto res_mt = draw_bar_btn(
        "##bar_mt", "MT", bold, 11.0f * scale, cur_x, bar_btn_y, mt_w, bar_btn_h,
        state.master_tempo, accent_dim, accent, theme::kTextHot,
        tr(Str::MasterTempoTooltip));
    if (res_mt.clicked) {
        out.master_tempo_toggled = true;
    }
    cur_x += mt_w;

    // --- 5. PITCH SECTION (Knob + Rango + Etiqueta a la derecha) -----------
    const float knob_r = 15.0f * scale; // 30px diámetro, perfectamente igual a bar_btn_h
    const float knob_cy = (ctrl_min.y + ctrl_max.y) * 0.5f;
    const float knob_cx = ctrl_max.x - 8.0f * scale - knob_r;

    char range_buf[16];
    std::snprintf(range_buf, sizeof(range_buf), "\xc2\xb1%d%%", state.pitch_range);
    const ImVec2 rsz = (mono != nullptr)
                           ? mono->CalcTextSizeA(pill_sz, FLT_MAX, 0.0f, range_buf)
                           : ImGui::CalcTextSize(range_buf);
    const float range_btn_w = rsz.x + 10.0f * scale;
    const float range_btn_h = 20.0f * scale;
    const float range_x = knob_cx - knob_r - 7.0f * scale - range_btn_w;
    const float range_y = knob_cy - range_btn_h * 0.5f;

    ImGui::SetCursorScreenPos(ImVec2(range_x, range_y));
    if (ImGui::InvisibleButton("##pitch_range", ImVec2(range_btn_w, range_btn_h))) {
        out.pitch_range_cycled = true;
    }
    const bool r_hov = ImGui::IsItemHovered();
    if (r_hov) {
        ImGui::SetTooltip("%s", tr(Str::PitchRangeTooltip));
    }
    dl->AddRectFilled(ImVec2(range_x, range_y), ImVec2(range_x + range_btn_w, range_y + range_btn_h),
                      r_hov ? theme::kGlassFillStrong : theme::kGlassFill, 5.0f * scale);
    dl->AddRect(ImVec2(range_x, range_y), ImVec2(range_x + range_btn_w, range_y + range_btn_h),
                r_hov ? accent : theme::kGlassBorder, 5.0f * scale, 0, r_hov ? 1.5f : 1.0f);
    const ImVec2 rtxt_pos = {range_x + (range_btn_w - rsz.x) * 0.5f,
                             range_y + (range_btn_h - rsz.y) * 0.5f};
    if (mono != nullptr) {
        dl->AddText(mono, pill_sz, rtxt_pos, r_hov ? theme::kTextHot : theme::kTextDim, range_buf);
    } else {
        dl->AddText(rtxt_pos, r_hov ? theme::kTextHot : theme::kTextDim, range_buf);
    }

    const char* pitch_lbl = tr(Str::Pitch);
    const ImVec2 pls = (ui != nullptr)
                           ? ui->CalcTextSizeA(pill_sz, FLT_MAX, 0.0f, pitch_lbl)
                           : ImGui::CalcTextSize(pitch_lbl);
    const float pitch_lbl_x = range_x - 6.0f * scale - pls.x;
    if (ui != nullptr) {
        dl->AddText(ui, pill_sz, ImVec2(pitch_lbl_x, knob_cy - pls.y * 0.5f),
                    theme::kTextDim, pitch_lbl);
    } else {
        dl->AddText(ImVec2(pitch_lbl_x, knob_cy - pls.y * 0.5f),
                    theme::kTextDim, pitch_lbl);
    }

    // DIVIDER 4: Separador elegante antes de la sección Pitch si hay espacio
    const float div4_x = pitch_lbl_x - 10.0f * scale;
    if (cur_x + 6.0f * scale < div4_x) {
        dl->AddLine(ImVec2(div4_x, div_y), ImVec2(div4_x, div_y + div_h), theme::kGlassBorder, 1.0f);
    }

    ImGui::SetCursorScreenPos(ImVec2(knob_cx - knob_r, knob_cy - knob_r));
    float pitch = static_cast<float>(state.pitch_pct);
    if (pitch_knob(&pitch, knob_r, static_cast<float>(state.pitch_range), accent)) {
        out.pitch_changed = true;
        out.pitch_new = pitch;
    }

    // Fija el cursor al pie exacto del panel de controles: tras el knob el
    // cursor ya está cerca del pie, así que un Dummy(ctrl_h) desbordaría el
    // child. Un dummy de 1px en ctrl_max.y fija la altura total exacta.
    ImGui::SetCursorScreenPos(ImVec2(ctrl_min.x, ctrl_max.y - 1.0f));
    ImGui::Dummy(ImVec2(0.0f, 1.0f));
    ImGui::PopID();
    return out;
}

float deck_panel_height(float scale) {
    const float pad = 12.0f * scale;    // p-3
    const float gap = 12.0f * scale;    // gap-3
    const float bpm_sz = 30.0f * scale; // text-3xl
    const float lbl_sz = 12.0f * scale; // text-xs
    const float ov_h = 24.0f * scale;   // h-6
    const float dt_h = 64.0f * scale;   // h-16
    const float ts_h = 20.0f * scale;   // altura para timestamps
    const float pb3 = 12.0f * scale; // pb-3 (info)
    const float bpm_block_h = bpm_sz + 4.0f * scale + lbl_sz + 2.0f * scale;
    const float info_h = bpm_block_h + pb3;
    const float panel_h =
        pad + info_h + gap + ov_h + gap + dt_h + 4.0f * scale + ts_h + pad;
    const float ctrl_h = 48.0f * scale; // p-2 + btn h-8
    return panel_h + 8.0f * scale + ctrl_h;
}

} // namespace melodeus::app
