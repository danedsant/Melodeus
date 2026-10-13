#pragma once

// Tema F3.6: glassmorphism oscuro centralizado. Un solo sitio cambia la
// paleta de toda la app; los paneles usan draw_glass_panel en lugar de
// rectángulos opacos.

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace melodeus::app::theme {

// --- Paleta ----------------------------------------------------------------
inline constexpr ImU32 kGlassFill = IM_COL32(30, 34, 44, 52);       // paneles
inline constexpr ImU32 kGlassFillStrong = IM_COL32(36, 42, 54, 78); // paneles foco
inline constexpr ImU32 kGlassBorder = IM_COL32(200, 215, 240, 46);  // hairline
inline constexpr ImU32 kGlassHighlight = IM_COL32(255, 255, 255, 24);
inline constexpr ImU32 kGlassShadow = IM_COL32(0, 0, 0, 40);
inline constexpr ImU32 kBackdrop = IM_COL32(14, 15, 20, 255);

inline constexpr ImU32 kText = IM_COL32(228, 232, 240, 255);
inline constexpr ImU32 kTextDim = IM_COL32(140, 148, 162, 255);
inline constexpr ImU32 kTextHot = IM_COL32(245, 248, 252, 255);

inline constexpr ImU32 kAccentA = IM_COL32(178, 60, 240, 255);  // deck A (electric purple)
inline constexpr ImU32 kAccentB = IM_COL32(236, 130, 250, 255); // deck B (hot purple)
inline constexpr ImU32 kAccentADim = IM_COL32(90, 30, 120, 255);
inline constexpr ImU32 kAccentBDim = IM_COL32(130, 65, 140, 255);
inline constexpr ImU32 kSuccess = IM_COL32(72, 200, 120, 255);
inline constexpr ImU32 kDanger = IM_COL32(240, 90, 80, 255);
inline constexpr ImU32 kWarn = IM_COL32(240, 200, 90, 255);

// Colores de acento dinámicos configurables por el usuario.
inline ImU32 g_accent_a = kAccentA;
inline ImU32 g_accent_b = kAccentB;
inline ImU32 g_accent_a_dim = kAccentADim;
inline ImU32 g_accent_b_dim = kAccentBDim;

// Barra de peak del master (top-right): valores por defecto.
inline constexpr ImU32 kPeakNavy = IM_COL32(18, 32, 92, 255);
inline constexpr ImU32 kPeakPurple = IM_COL32(178, 60, 240, 255);
inline constexpr ImU32 kPeakHot = IM_COL32(236, 130, 250, 255); // >0 dBFS clip
inline constexpr ImU32 kPeakTrack = IM_COL32(40, 46, 62, 200);  // carril vacío

// Barra de peak del master (top-right): degradado adaptativo dinámico.
inline ImU32 g_peak_low = kPeakNavy;
inline ImU32 g_peak_high = kPeakPurple;
inline ImU32 g_peak_hot = kPeakHot;

inline void apply();

inline void set_deck_colors(const float a[3], const float b[3]) {
    const int ar = static_cast<int>(std::clamp(a[0], 0.0f, 1.0f) * 255.0f);
    const int ag = static_cast<int>(std::clamp(a[1], 0.0f, 1.0f) * 255.0f);
    const int ab = static_cast<int>(std::clamp(a[2], 0.0f, 1.0f) * 255.0f);
    g_accent_a = IM_COL32(ar, ag, ab, 255);
    g_accent_a_dim = IM_COL32(ar / 2, ag / 2, ab / 2, 255);

    const int br = static_cast<int>(std::clamp(b[0], 0.0f, 1.0f) * 255.0f);
    const int bg = static_cast<int>(std::clamp(b[1], 0.0f, 1.0f) * 255.0f);
    const int bb = static_cast<int>(std::clamp(b[2], 0.0f, 1.0f) * 255.0f);
    g_accent_b = IM_COL32(br, bg, bb, 255);
    g_accent_b_dim = IM_COL32(br / 2, bg / 2, bb / 2, 255);

    // Degradado del medidor derivado armónicamente
    g_peak_low = IM_COL32(ar / 6, ag / 6, std::clamp((ab / 3) + 25, 0, 255), 255);
    g_peak_high = g_accent_a;
    g_peak_hot = g_accent_b;

    if (ImGui::GetCurrentContext() != nullptr) {
        apply();
    }
}

inline ImU32 accent_a() { return g_accent_a; }
inline ImU32 accent_b() { return g_accent_b; }
inline ImU32 accent_a_dim() { return g_accent_a_dim; }
inline ImU32 accent_b_dim() { return g_accent_b_dim; }
inline ImU32 peak_low() { return g_peak_low; }
inline ImU32 peak_high() { return g_peak_high; }
inline ImU32 peak_hot() { return g_peak_hot; }

inline constexpr float kRadiusPanel = 12.0f;
inline constexpr float kRadiusControl = 8.0f;
inline constexpr float kRadiusGrab = 5.0f;

// --- Iconos Phosphor 2.1.1 (regular, PUA) ----------------------------------
namespace icons {
inline constexpr ImWchar play = 0xE3D0;
inline constexpr ImWchar pause = 0xE39E;
inline constexpr ImWchar stop_circle = 0xE46E; // CUE
inline constexpr ImWchar flag = 0xE244;
inline constexpr ImWchar folder_plus = 0xE258;
inline constexpr ImWchar magnifier = 0xE30C;
inline constexpr ImWchar music_notes = 0xE340;

/// Codepoint → UTF-8 (PUA son 3 bytes). Devuelve std::string por comodidad.
inline std::string utf8(ImWchar c) {
    std::string s;
    const std::uint32_t cp = c;
    if (cp < 0x80) {
        s.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return s;
}
} // namespace icons

// --- Tema ImGui ------------------------------------------------------------
/// Aplica el look completo a ImGuiStyle (llamar una vez en init).
inline void apply() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = kRadiusPanel;
    s.ChildRounding = kRadiusPanel;
    s.FrameRounding = kRadiusControl;
    s.PopupRounding = kRadiusControl;
    s.ScrollbarRounding = kRadiusControl;
    s.GrabRounding = kRadiusGrab;
    s.TabRounding = kRadiusControl;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 0.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowPadding = ImVec2(10.0f, 8.0f);
    s.FramePadding = ImVec2(10.0f, 6.0f);
    s.ItemSpacing = ImVec2(8.0f, 6.0f);
    s.GrabMinSize = 10.0f;

    const ImVec4 t = ImGui::ColorConvertU32ToFloat4(kText);
    const ImVec4 dim = ImGui::ColorConvertU32ToFloat4(kTextDim);
    const ImVec4 glass = ImGui::ColorConvertU32ToFloat4(kGlassFill);
    const ImVec4 glass_strong = ImGui::ColorConvertU32ToFloat4(kGlassFillStrong);
    const ImVec4 border = ImGui::ColorConvertU32ToFloat4(kGlassBorder);
    const ImVec4 frame = ImGui::ColorConvertU32ToFloat4(IM_COL32(56, 64, 82, 90));
    const ImVec4 frame_hov = ImGui::ColorConvertU32ToFloat4(IM_COL32(70, 80, 102, 120));
    const ImVec4 frame_act = ImGui::ColorConvertU32ToFloat4(IM_COL32(80, 92, 116, 150));
    const ImVec4 btn = ImGui::ColorConvertU32ToFloat4(IM_COL32(62, 70, 90, 100));
    const ImVec4 btn_hov = ImGui::ColorConvertU32ToFloat4(IM_COL32(84, 96, 122, 160));
    const ImVec4 btn_act = ImGui::ColorConvertU32ToFloat4(IM_COL32(100, 114, 144, 200));
    const ImVec4 acc_a = ImGui::ColorConvertU32ToFloat4(g_accent_a);
    const ImVec4 acc_b = ImGui::ColorConvertU32ToFloat4(g_accent_b);
    const ImVec4 hdr = ImVec4(acc_a.x, acc_a.y, acc_a.z, 0.22f);
    const ImVec4 hdr_sel = ImVec4(acc_a.x, acc_a.y, acc_a.z, 0.42f);
    const ImVec4 grab = ImGui::ColorConvertU32ToFloat4(IM_COL32(190, 200, 220, 255));
    const ImVec4 grab_hov = acc_a;

    s.Colors[ImGuiCol_Text] = t;
    s.Colors[ImGuiCol_TextDisabled] = dim;
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0); // el fondo lo da el clear
    s.Colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_PopupBg] = ImVec4(20.0f / 255.0f, 24.0f / 255.0f, 34.0f / 255.0f, 0.96f);
    s.Colors[ImGuiCol_Border] = border;
    s.Colors[ImGuiCol_FrameBg] = frame;
    s.Colors[ImGuiCol_FrameBgHovered] = frame_hov;
    s.Colors[ImGuiCol_FrameBgActive] = frame_act;
    s.Colors[ImGuiCol_Button] = btn;
    s.Colors[ImGuiCol_ButtonHovered] = btn_hov;
    s.Colors[ImGuiCol_ButtonActive] = btn_act;
    s.Colors[ImGuiCol_Header] = hdr;
    s.Colors[ImGuiCol_HeaderHovered] = hdr_sel;
    s.Colors[ImGuiCol_HeaderActive] = hdr_sel;
    s.Colors[ImGuiCol_CheckMark] = acc_a;
    s.Colors[ImGuiCol_SliderGrab] = grab;
    s.Colors[ImGuiCol_SliderGrabActive] = grab_hov;
    s.Colors[ImGuiCol_Separator] = border;
    s.Colors[ImGuiCol_ResizeGrip] = glass;
    s.Colors[ImGuiCol_ResizeGripHovered] = glass_strong;
    s.Colors[ImGuiCol_ResizeGripActive] = glass_strong;
    s.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.35f, 0.38f, 0.46f, 0.60f);
    s.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.45f, 0.50f, 0.60f, 0.70f);
    s.Colors[ImGuiCol_ScrollbarGrabActive] = grab_hov;
    s.Colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.22f, 0.30f, 0.50f);
    s.Colors[ImGuiCol_TabHovered] = ImVec4(acc_a.x, acc_a.y, acc_a.z, 0.40f);
    s.Colors[ImGuiCol_TabActive] = ImVec4(acc_a.x, acc_a.y, acc_a.z, 0.65f);
    s.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.12f, 0.15f, 0.20f, 0.40f);
    s.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(acc_a.x, acc_a.y, acc_a.z, 0.45f);
    s.Colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.02f);
    s.Colors[ImGuiCol_TableHeaderBg] = ImGui::ColorConvertU32ToFloat4(IM_COL32(50, 58, 74, 110));
    s.Colors[ImGuiCol_TableBorderStrong] = border;
    s.Colors[ImGuiCol_TableBorderLight] = ImVec4(border.x, border.y, border.z, 0.5f);
    s.Colors[ImGuiCol_NavCursor] = hdr_sel;
}

// --- Panel glass -----------------------------------------------------------
/// Relleno redondeado translúcido + hairline + highlight superior + sombra
/// suave fake. Llamar ANTES de los widgets del panel (queda por debajo).
inline void glass(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float radius = kRadiusPanel) {
    // Sombra: 2 capas offset, alpha muy bajo.
    dl->AddRectFilled(ImVec2(mn.x + 2.0f, mn.y + 3.0f), ImVec2(mx.x + 2.0f, mx.y + 4.0f),
                      kGlassShadow, radius);
    dl->AddRectFilled(ImVec2(mn.x + 1.0f, mn.y + 2.0f), ImVec2(mx.x + 1.0f, mx.y + 3.0f),
                      IM_COL32(0, 0, 0, 24), radius);
    // Cuerpo + borde hairline.
    dl->AddRectFilled(mn, mx, kGlassFill, radius);
    dl->AddRect(mn, mx, kGlassBorder, radius, 0, 1.0f);
    // Highlight de vidrio arriba (1px, decae hacia los lados con clip).
    const float hw = (mx.x - mn.x) * 0.6f;
    const float hx = mn.x + (mx.x - mn.x - hw) * 0.5f;
    dl->AddLine(ImVec2(hx, mn.y + 1.0f), ImVec2(hx + hw, mn.y + 1.0f), kGlassHighlight, 1.0f);
}

} // namespace melodeus::app::theme
