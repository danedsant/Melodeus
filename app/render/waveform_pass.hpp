#pragma once

#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>

#include "render/device_d3d12.hpp"
#include "waveform/waveform.hpp"

namespace melodeus::app {

/// Pase D3D12 de waveform: PSO de triángulos con instancias WaveInstance en
/// upload heap (persistently mapped, un slot por frame → sin hazard con el
/// fence de DeviceD3D12). Los .cso se compilan con fxc en el build y se
/// copian junto al exe.
class WaveformPass {
public:
    static constexpr std::size_t kMaxInstances = 8192; // por frame total

    bool init(DeviceD3D12& device);
    void shutdown();

    /// Resetea el cursor de instancias para el slot de frame actual.
    void begin_frame(UINT frame_slot);

    /// Copia las instancias al upload del slot y dibuja un carril recortado
    /// al rect en píxeles (viewport + scissor). Acumula varias lanes por
    /// frame en el mismo buffer (offsets distintos, sin hazard).
    void draw_lane(ID3D12GraphicsCommandList* cmd, const wave::WaveInstance* instances,
                   std::size_t count, const RECT& lane_px);

private:
    struct UploadSlot {
        Microsoft::WRL::ComPtr<ID3D12Resource> buf;
        wave::WaveInstance* mapped = nullptr;
    };

    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_sig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;
    UploadSlot uploads_[DeviceD3D12::kFramesInFlight];
    UINT frame_slot_ = 0;
    std::size_t cursor_ = 0;
    bool initialized_ = false;
};

} // namespace melodeus::app
