#include "render/imgui_layer.hpp"

#include "render/device_d3d12.hpp"

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace melodeus::app {

namespace {

DescriptorHeapAllocator* g_srv_alloc = nullptr;
ID3D12DescriptorHeap* g_srv_heap = nullptr;

ImFont* g_font_ui = nullptr;
ImFont* g_font_title = nullptr;
ImFont* g_font_mono = nullptr;

/// Directorio de fuentes: junto al exe (fonts/) o app/fonts (cwd=raíz).
std::filesystem::path font_dir() {
    wchar_t buf[MAX_PATH]{};
    const DWORD len = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::filesystem::path exe_dir = std::filesystem::path(buf).parent_path();
        std::error_code ec;
        if (std::filesystem::exists(exe_dir / "fonts", ec)) {
            return exe_dir / "fonts";
        }
    }
    return std::filesystem::path("app") / "fonts";
}

/// Carga los3 pesos + iconos; devuelve false si faltan TTF (fallback default).
bool load_fonts(float /*dpi_scale*/) {
    ImGuiIO& io = ImGui::GetIO();
    const std::filesystem::path dir = font_dir();
    const auto clash = dir / "ClashDisplay-Medium.ttf";
    const auto clash_sb = dir / "ClashDisplay-Semibold.ttf";
    const auto jb = dir / "JetBrainsMono-Regular.ttf";
    const auto ph = dir / "Phosphor.ttf";
    std::error_code ec;
    if (!std::filesystem::exists(clash, ec)) {
        return false;
    }

    // Rangos: default (Latin-1) + tildes/ñ + codepoints Phosphor (PUA).
    ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddText("\xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA\xC3\xBC\xC3\xB1" // áéíóúüñ
                    "\xC3\x81\xC3\x89\xC3\x8D\xC3\x93\xC3\x9A\xC3\x9C\xC3\x91" // ÁÉÍÓÚÜÑ
                    "\xE2\x80\x94\xE2\x80\xA6");                               // — …
    // Iconos Phosphor (regular 2.1.1): play, pause, stop-circle (CUE),
    // flag, folder-plus, magnifying-glass, music-notes.
    constexpr ImWchar kIcons[] = {0xE244, 0xE258, 0xE30C, 0xE340, 0xE39E, 0xE3D0, 0xE46E};
    for (const ImWchar ic : kIcons) {
        builder.AddChar(ic);
    }
    builder.BuildRanges(&ranges);

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = false;

    g_font_ui = io.Fonts->AddFontFromFileTTF(clash.string().c_str(), 16.0f, &cfg, ranges.Data);
    if (g_font_ui == nullptr) {
        return false;
    }
    // Phosphor merged (un poco más pequeño para alinear con el x-height).
    ImFontConfig icon_cfg;
    icon_cfg.MergeMode = true;
    icon_cfg.PixelSnapH = true;
    icon_cfg.OversampleH = 1;
    io.Fonts->AddFontFromFileTTF(ph.string().c_str(), 14.0f, &icon_cfg, ranges.Data);

    g_font_title =
        io.Fonts->AddFontFromFileTTF(clash_sb.string().c_str(), 19.0f, &cfg, ranges.Data);
    g_font_mono = io.Fonts->AddFontFromFileTTF(jb.string().c_str(), 15.0f, &cfg, ranges.Data);
    return true;
}

} // namespace

ImFont* imgui_font_ui() {
    return g_font_ui;
}

ImFont* imgui_font_title() {
    return g_font_title;
}

ImFont* imgui_font_mono() {
    return g_font_mono;
}

bool imgui_layer_init(HWND hwnd, DeviceD3D12& device) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;

    if (!load_fonts(scale)) {
        std::fprintf(stderr, "[ui] fuentes en %s no encontradas; fuente default ImGui\n",
                     font_dir().string().c_str());
    }

    if (!ImGui_ImplWin32_Init(hwnd)) {
        return false;
    }

    g_srv_alloc = &device.srv_alloc();
    g_srv_heap = device.srv_heap();

    ImGui_ImplDX12_InitInfo init_info{};
    init_info.Device = device.device();
    init_info.CommandQueue = device.queue();
    init_info.NumFramesInFlight = static_cast<int>(DeviceD3D12::kFramesInFlight);
    init_info.RTVFormat = device.rtv_format();
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init_info.SrvDescriptorHeap = g_srv_heap;
    init_info.SrvDescriptorAllocFn =
        [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu,
           D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) { g_srv_alloc->alloc(out_cpu, out_gpu); };
    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                                       D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
        g_srv_alloc->free(cpu, gpu);
    };
    if (!ImGui_ImplDX12_Init(&init_info)) {
        return false;
    }
    return true;
}

void imgui_layer_shutdown() {
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_srv_alloc = nullptr;
    g_srv_heap = nullptr;
    g_font_ui = nullptr;
    g_font_title = nullptr;
    g_font_mono = nullptr;
}

void imgui_layer_new_frame() {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void imgui_layer_render(ID3D12GraphicsCommandList* cmd) {
    ImGui::Render();
    cmd->SetDescriptorHeaps(1, &g_srv_heap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);
}

} // namespace melodeus::app
