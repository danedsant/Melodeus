#include "settings_ui.hpp"

#include "i18n.hpp"
#include "settings.hpp"
#include "theme.hpp"
#include "render/logo_texture.hpp"

#include "imgui.h"
#include "imgui_internal.h" // BringWindowToDisplayFront (z-order del modal)

#include <cstring>
#include <filesystem>
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>

namespace melodeus::app {

std::string pick_folder_dialog(void* hwnd_parent) {
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::string result;
    IFileOpenDialog* dlg = nullptr;
    if (SUCCEEDED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&dlg)))) {
        DWORD flags = 0;
        dlg->GetOptions(&flags);
        dlg->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (SUCCEEDED(dlg->Show(static_cast<HWND>(hwnd_parent)))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR wpath = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wpath)) && wpath != nullptr) {
                    const int len = ::WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0) {
                        std::string utf8_path(static_cast<std::size_t>(len - 1), '\0');
                        ::WideCharToMultiByte(CP_UTF8, 0, wpath, -1, utf8_path.data(), len, nullptr, nullptr);
                        result = utf8_path;
                    }
                    ::CoTaskMemFree(wpath);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    ::CoUninitialize();
    return result;
}

namespace {

/// Pestaña activa del modal.
int g_tab = 0;

/// Acción en rebind (-1 = ninguna). Cuando >= 0, el siguiente key press la asigna.
int g_rebind_action = -1;

} // namespace

bool draw_settings_modal(bool* open) {
    if (open == nullptr || !*open) {
        return false;
    }
    bool should_close = false;

    const ImVec2 display = ImGui::GetIO().DisplaySize;

    // Fondo modal semi-transparente que atenúa visualmente y absorbe eventos del fondo.
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(display);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.50f));
    if (ImGui::Begin("##settings_backdrop", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    }
    ImGui::End();
    ImGui::PopStyleColor();

    ImGui::SetNextWindowSize(ImVec2(520.0f, 520.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    // El tema deja WindowBg transparente; forzamos fondo opaco solo aquí.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(18.0f / 255.0f, 20.0f / 255.0f,
                                                    28.0f / 255.0f, 1.0f));
    const bool visible =
        ImGui::Begin(tr(Str::SettingsTitle), open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    // Un clic en el deck enfoca ##body y ImGui lo manda al frente de la lista
    // de dibujo, tapando el modal. Re-afirmamos el modal al frente solo si no
    // hay un popup hijo activo (como el ColorPicker) para no taparlo.
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    }
    if (visible) {
        // --- Pestañas -------------------------------------------------------
        if (ImGui::BeginTabBar("##settings_tabs")) {
            if (ImGui::BeginTabItem(tr(Str::TabGeneral))) {
                g_tab = 0;
                ImGui::Dummy(ImVec2(0.0f, 8.0f));
                ImGui::TextDisabled("%s", tr(Str::Language));
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                Settings& s = global_settings();
                struct LangOpt {
                    Language lang;
                    const char* label;
                };
                const LangOpt opts[] = {
                    {Language::Es, tr(Str::LangEs)},
                    {Language::En, tr(Str::LangEn)},
                    {Language::PtBr, tr(Str::LangPtBr)},
                };
                for (const LangOpt& o : opts) {
                    const bool sel = (s.lang == o.lang);
                    if (ImGui::Selectable(o.label, sel)) {
                        s.lang = o.lang;
                        s.save(settings_path());
                    }
                    if (sel) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                ImGui::TextDisabled("%s", tr(Str::DjPlayback));
                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                if (ImGui::Checkbox(tr(Str::QuantizeSnap), &s.quantize)) {
                    s.save(settings_path());
                }
                if (ImGui::Checkbox(tr(Str::ShowBeatgrid), &s.show_beatgrid)) {
                    s.save(settings_path());
                }
                const char* pr_items[] = {"±8%", "±16%", "±50%", "WIDE (±100%)"};
                const int pr_vals[] = {8, 16, 50, 100};
                int cur_pr = 1;
                for (int pi = 0; pi < 4; ++pi) {
                    if (s.pitch_range == pr_vals[pi]) {
                        cur_pr = pi;
                    }
                }
                if (ImGui::Combo(tr(Str::PitchRangeSetting), &cur_pr, pr_items, 4)) {
                    s.pitch_range = pr_vals[cur_pr];
                    s.save(settings_path());
                }

                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                ImGui::TextDisabled("%s", tr(Str::StreamingDownloads));
                ImGui::Dummy(ImVec2(0.0f, 2.0f));

                char dl_buf[512];
                if (s.download_dir.empty()) {
                    std::snprintf(dl_buf, sizeof(dl_buf), "%s", tr(Str::DefaultDownloadDir));
                } else {
                    std::snprintf(dl_buf, sizeof(dl_buf), "%s", s.download_dir.c_str());
                }

                ImGui::PushItemWidth(-1.0f);
                ImGui::InputText("##dl_path", dl_buf, sizeof(dl_buf), ImGuiInputTextFlags_ReadOnly);
                ImGui::PopItemWidth();

                if (ImGui::Button(tr(Str::BrowseFolder))) {
                    const std::string chosen = pick_folder_dialog(nullptr);
                    if (!chosen.empty()) {
                        s.download_dir = chosen;
                        s.save(settings_path());
                    }
                }
                ImGui::SameLine();
                if (!s.download_dir.empty()) {
                    if (ImGui::Button(tr(Str::ResetDefaultFolder))) {
                        s.download_dir.clear();
                        s.save(settings_path());
                    }
                    ImGui::SameLine();
                }
                if (ImGui::Button(tr(Str::OpenInExplorer))) {
                    std::string path_to_open = s.download_dir;
                    if (path_to_open.empty()) {
                        path_to_open = (std::filesystem::path(settings_path()).parent_path() / "downloads").string();
                    }
                    std::error_code ec;
                    std::filesystem::create_directories(path_to_open, ec);
                    ::ShellExecuteA(nullptr, "open", path_to_open.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
                }

                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(tr(Str::TabAppearance))) {
                g_tab = 1;
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                Settings& s = global_settings();
                ImGui::TextDisabled("%s", tr(Str::DeckColors));
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                bool color_changed = false;
                constexpr ImGuiColorEditFlags kFlags =
                    ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB;
                if (ImGui::ColorEdit3(tr(Str::ColorDeckA), s.color_deck_a, kFlags)) {
                    color_changed = true;
                }
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                if (ImGui::ColorEdit3(tr(Str::ColorDeckB), s.color_deck_b, kFlags)) {
                    color_changed = true;
                }

                if (color_changed) {
                    s.theme_preset = -1; // Personalizado
                    theme::set_deck_colors(s.color_deck_a, s.color_deck_b);
                    s.save(settings_path());
                }

                ImGui::Dummy(ImVec2(0.0f, 8.0f));
                ImGui::TextDisabled("%s", tr(Str::SkinPresets));
                ImGui::Dummy(ImVec2(0.0f, 2.0f));

                struct SkinPreset {
                    const char* name;
                    float a[3];
                    float b[3];
                };
                const SkinPreset kPresets[] = {
                    {"Electric Purple (Original)", {178.0f/255.0f, 60.0f/255.0f, 240.0f/255.0f}, {236.0f/255.0f, 130.0f/255.0f, 250.0f/255.0f}},
                    {s.lang == Language::En ? "Pioneer Club (Blue/Red)" : (s.lang == Language::PtBr ? "Pioneer Club (Azul/Vermelho)" : "Pioneer Club (Azul/Rojo)"),    {30.0f/255.0f, 112.0f/255.0f, 235.0f/255.0f},  {230.0f/255.0f, 42.0f/255.0f, 69.0f/255.0f}},
                    {"Cyberpunk (Cyan/Magenta)",    {0.0f, 229.0f/255.0f, 255.0f/255.0f},          {255.0f/255.0f, 0.0f, 127.0f/255.0f}},
                    {s.lang == Language::En ? "Acid Neon (Green/Orange)" : (s.lang == Language::PtBr ? "Acid Neon (Verde/Laranja)" : "Acid Neon (Verde/Naranja)"),   {57.0f/255.0f, 255.0f/255.0f, 20.0f/255.0f},   {255.0f/255.0f, 153.0f/255.0f, 0.0f}},
                    {s.lang == Language::En ? "Monochrome Ice (White/Cyan)" : (s.lang == Language::PtBr ? "Monochrome Ice (Branco/Cyan)" : "Monochrome Ice (Blanco/Cyan)"), {230.0f/255.0f, 236.0f/255.0f, 245.0f/255.0f}, {0.0f, 210.0f/255.0f, 255.0f/255.0f}},
                    {s.lang == Language::En ? "Solar Sunset (Amber/Pink)" : (s.lang == Language::PtBr ? "Solar Sunset (\xC3\x82mbar/Rosa)" : "Solar Sunset (\xC3\x81mbar/Rosa)"),   {255.0f/255.0f, 140.0f/255.0f, 0.0f},          {255.0f/255.0f, 30.0f/255.0f, 100.0f/255.0f}},
                    {s.lang == Language::En ? "Deep Ocean (Blue/Aqua)" : (s.lang == Language::PtBr ? "Deep Ocean (Azul/Aqua)" : "Deep Ocean (Azul/Aqua)"),      {0.0f, 140.0f/255.0f, 255.0f/255.0f},          {0.0f, 255.0f/255.0f, 170.0f/255.0f}},
                };
                constexpr int kNumPresets = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

                for (int pi = 0; pi < kNumPresets; ++pi) {
                    const auto& p = kPresets[pi];
                    const bool is_active = (s.theme_preset == pi);
                    if (is_active) {
                        ImGui::PushStyleColor(ImGuiCol_Border, theme::accent_a());
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
                    }
                    if (ImGui::Button(p.name)) {
                        s.color_deck_a[0] = p.a[0]; s.color_deck_a[1] = p.a[1]; s.color_deck_a[2] = p.a[2];
                        s.color_deck_b[0] = p.b[0]; s.color_deck_b[1] = p.b[1]; s.color_deck_b[2] = p.b[2];
                        s.theme_preset = pi;
                        theme::set_deck_colors(s.color_deck_a, s.color_deck_b);
                        s.save(settings_path());
                    }
                    if (is_active) {
                        ImGui::PopStyleVar();
                        ImGui::PopStyleColor();
                    }
                    if (pi % 2 == 0 && pi + 1 < kNumPresets) {
                        ImGui::SameLine();
                    }
                }
                if (s.theme_preset == -1) {
                    ImGui::Dummy(ImVec2(0.0f, 4.0f));
                    ImGui::TextColored(ImVec4(0.7f, 0.8f, 1.0f, 1.0f), "%s", tr(Str::CustomMode));
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(tr(Str::TabShortcuts))) {
                g_tab = 1;
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                ImGui::TextDisabled("%s", tr(Str::ShortcutsHint));
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                Settings& s = global_settings();
                if (ImGui::Button(tr(Str::ResetDefaults))) {
                    s = Settings::defaults();
                    s.save(settings_path());
                    g_rebind_action = -1;
                }
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                // Captura de tecla para rebind activo.
                if (g_rebind_action >= 0) {
                    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false)) {
                            if (k == ImGuiKey_Escape) {
                                g_rebind_action = -1; // cancelar
                            } else {
                                s.set_key(static_cast<Action>(g_rebind_action),
                                          static_cast<ImGuiKey>(k));
                                s.save(settings_path());
                                g_rebind_action = -1;
                            }
                            break;
                        }
                    }
                }
                // Lista de acciones con su tecla actual.
                if (ImGui::BeginTable("##shortcuts", 2,
                                      ImGuiTableFlags_SizingStretchProp |
                                          ImGuiTableFlags_NoSavedSettings)) {
                    ImGui::TableSetupColumn("##action", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("##key", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                    for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
                        const Action a = static_cast<Action>(i);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(Settings::action_label(a));
                        ImGui::TableSetColumnIndex(1);
                        const bool rebinding = (g_rebind_action == i);
                        ImGui::PushID(i);
                        if (rebinding) {
                            ImGui::Button("...", ImVec2(110.0f, 0.0f));
                        } else {
                            const char* kl = Settings::key_label(s.key(a));
                            if (ImGui::Button(kl, ImVec2(110.0f, 0.0f))) {
                                g_rebind_action = i;
                            }
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(tr(Str::TabAbout))) {
                g_tab = 3;
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::BeginChild("##about_scroll", ImVec2(0.0f, 380.0f), false);

                // Logo + Título
                if (global_logo().is_valid()) {
                    ImGui::Image(global_logo().texture_id(), ImVec2(48.0f, 48.0f));
                    ImGui::SameLine(0.0f, 12.0f);
                }
                ImGui::BeginGroup();
                ImGui::TextColored(ImVec4(0.95f, 0.96f, 1.0f, 1.0f), "MELODEUS DJ STUDIO");
                ImGui::TextColored(ImVec4(0.68f, 0.45f, 1.0f, 1.0f), "v1.0.0 (Windows x64)");
                ImGui::EndGroup();

                ImGui::Dummy(ImVec2(0.0f, 8.0f));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 4.0f));

                // Orquestación
                ImGui::TextColored(ImVec4(0.9f, 0.75f, 1.0f, 1.0f), "Orquestaci\xC3\xB3n y Dise\xC3\xB1o:");
                ImGui::BulletText("%s", tr(Str::AboutOrchestrated));

                ImGui::Dummy(ImVec2(0.0f, 6.0f));

                // Datos del Desarrollo con Inteligencia Artificial
                ImGui::TextColored(ImVec4(0.9f, 0.75f, 1.0f, 1.0f), "Desarrollo con Inteligencia Artificial:");
                ImGui::BulletText("%s", tr(Str::AboutAiModel));
                ImGui::BulletText("%s", tr(Str::AboutAiPlatform));
                ImGui::BulletText("%s", tr(Str::AboutTokens));
                ImGui::BulletText("Arquitectura: C++20 nativo, DirectX 12, HLSL Shaders, WASAPI / JUCE");

                ImGui::Dummy(ImVec2(0.0f, 8.0f));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 4.0f));

                // Licencias y Atribuciones de Código Abierto
                ImGui::TextColored(ImVec4(0.9f, 0.75f, 1.0f, 1.0f), "%s:", tr(Str::AboutLicensesHeading));
                ImGui::Dummy(ImVec2(0.0f, 2.0f));

                struct LibLicense { const char* name; const char* lic; const char* desc; };
                const LibLicense kLibs[] = {
                    {"Dear ImGui", "MIT License", "Interfaz grafica inmediata de alto rendimiento (Omar Cornut)"},
                    {"JUCE Core / Audio", "ISC / Apache / GPLv3", "Motor de audio, buffers y gestion de dispositivos"},
                    {"SQLite", "Public Domain", "Motor de base de datos FTS5 de alta velocidad (D. Richard Hipp)"},
                    {"ONNX Runtime", "MIT License", "Inferencia de aprendizaje profundo y analisis musical (Microsoft)"},
                    {"yt-dlp", "The Unlicense", "Motor de consulta y extraccion de streaming (yt-dlp team)"},
                    {"FFmpeg", "LGPL v2.1+ / GPL v2+", "Decodificacion y transcodificacion de audio multiformato"},
                    {"Inter Font", "SIL OFL 1.1", "Tipografia de interfaz de usuario (Rasmus Andersson)"},
                    {"JetBrains Mono", "Apache License 2.0", "Tipografia monoespaciada para metricas (JetBrains s.r.o.)"},
                    {"Phosphor Icons", "MIT License", "Iconografia vectorial moderna (Phosphor Icons team)"},
                    {"Inno Setup", "Inno Setup License", "Empaquetador e instalador de Windows (Jordan Russell)"},
                };

                for (const auto& l : kLibs) {
                    ImGui::TextColored(ImVec4(0.8f, 0.85f, 0.95f, 1.0f), "%s", l.name);
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.9f, 1.0f), "[%s]", l.lic);
                    ImGui::TextDisabled("    %s", l.desc);
                }

                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        // Botón cerrar alineado a la derecha.
        const float w = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (ImGui::Button(tr(Str::Close), ImVec2(w, 0.0f))) {
            should_close = true;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(); // WindowBg opaco del modal

    if (should_close) {
        *open = false;
        g_rebind_action = -1;
        return true;
    }
    return false;
}

} // namespace melodeus::app
