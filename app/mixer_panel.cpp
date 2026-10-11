#include "mixer_panel.hpp"

#include "imgui.h"

namespace melodeus::app {

namespace {

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

} // namespace

void draw_mixer_panel(MixerState& state, float peak_a, float peak_b, float master_env,
                      float width_px, float height_px, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(mn, ImVec2(mn.x + width_px, mn.y + height_px), IM_COL32(20, 22, 28, 255));
    dl->AddRect(mn, ImVec2(mn.x + width_px, mn.y + height_px), IM_COL32(45, 50, 60, 255));
    dl->AddText(ImVec2(mn.x + 10.0f * scale, mn.y + 6.0f * scale), IM_COL32(200, 208, 220, 255),
                "MIXER");

    // Faders de canal + VU (dos columnas).
    const float fader_top = mn.y + 32.0f * scale;
    const float fader_h = height_px - 150.0f * scale;
    const float col_w = width_px * 0.5f;

    for (int ch = 0; ch < 2; ++ch) {
        const float x0 = mn.x + col_w * static_cast<float>(ch);
        float* gain = (ch == 0) ? &state.ch_gain_a : &state.ch_gain_b;
        const float peak = (ch == 0) ? peak_a : peak_b;

        ImGui::SetCursorScreenPos(ImVec2(x0 + 8.0f * scale, fader_top));
        ImGui::VSliderFloat(ch == 0 ? "##gain_a" : "##gain_b", ImVec2(28.0f * scale, fader_h), gain,
                            0.0f, 1.0f, "");
        // VU junto al fader.
        const ImVec2 vu0 = {x0 + 42.0f * scale, fader_top};
        const ImVec2 vu1 = {x0 + 54.0f * scale, fader_top + fader_h};
        draw_vu(dl, vu0, vu1, peak);

        dl->AddText(ImVec2(x0 + 8.0f * scale, fader_top + fader_h + 4.0f * scale),
                    IM_COL32(150, 158, 170, 255), ch == 0 ? "A" : "B");
    }

    // Crossfader (horizontal, centrado).
    const float xf_y = fader_top + fader_h + 28.0f * scale;
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
