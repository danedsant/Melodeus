#pragma once

// Tema F3.6: glassmorphism oscuro centralizado. Un solo sitio cambia la
// paleta de toda la app; los paneles usan draw_glass_panel en lugar de
// rectángulos opacos.

#include "imgui.h"

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

inline constexpr ImU32 kAccentA = IM_COL32(86, 148, 255, 255); // deck A
inline constexpr ImU32 kAccentB = IM_COL32(255, 150, 86, 255); // deck B
inline constexpr ImU32 kAccentADim = IM_COL32(40, 74, 130, 255);
inline constexpr ImU32 kAccentBDim = IM_COL32(130, 74, 40, 255);
inline constexpr ImU32 kSuccess = IM_COL32(72, 200, 120, 255);
inline constexpr ImU32 kDanger = IM_COL32(240, 90, 80, 255);
inline constexpr ImU32 kWarn = IM_COL32(240, 200, 90, 255);

// Barra de peak del master (top-right): degradado azul marino -> purple.
inline constexpr ImU32 kPeakNavy = IM_COL32(18, 32, 92, 255);
inline constexpr ImU32 kPeakPurple = IM_COL32(178, 60, 240, 255);
inline constexpr ImU32 kPeakHot = IM_COL32(236, 130, 250, 255); // >0 dBFS clip
inline constexpr ImU32 kPeakTrack = IM_COL32(40, 46, 62, 200);  // carril vacío

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
    const ImVec4 hdr = ImGui::ColorConvertU32ToFloat4(IM_COL32(86, 148, 255, 46));
    const ImVec4 hdr_sel = ImGui::ColorConvertU32ToFloat4(IM_COL32(86, 148, 255, 90));
    const ImVec4 grab = ImGui::ColorConvertU32ToFloat4(IM_COL32(190, 200, 220, 255));
    const ImVec4 grab_hov = ImGui::ColorConvertU32ToFloat4(IM_COL32(120, 170, 255, 255));

    s.Colors[ImGuiCol_Text] = t;
    s.Colors[ImGuiCol_TextDisabled] = dim;
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0); // el fondo lo da el clear
    s.Colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_PopupBg] = glass_strong;
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
    s.Colors[ImGuiCol_CheckMark] = t;
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
