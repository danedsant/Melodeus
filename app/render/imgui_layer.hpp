#pragma once

#include <windows.h>
#include <d3d12.h>

namespace melodeus::app {

class DeviceD3D12;

/// Capa ImGui (contexto + backends Win32 y DX12) para el spike F0.
bool imgui_layer_init(HWND hwnd, DeviceD3D12& device);
void imgui_layer_shutdown();

/// Lanza el frame de ImGui (backends + NewFrame). Tras dibujar la UI, llamar
/// a imgui_layer_render() con la lista abierta por DeviceD3D12::begin_frame().
void imgui_layer_new_frame();

/// ImGui::Render() + transcripción del draw data a la lista.
void imgui_layer_render(ID3D12GraphicsCommandList* cmd);

} // namespace melodeus::app
