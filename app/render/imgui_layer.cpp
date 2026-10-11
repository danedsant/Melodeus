#include "render/imgui_layer.hpp"

#include "render/device_d3d12.hpp"

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

namespace melodeus::app {

namespace {

DescriptorHeapAllocator* g_srv_alloc = nullptr;
ID3D12DescriptorHeap* g_srv_heap = nullptr;

} // namespace

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
