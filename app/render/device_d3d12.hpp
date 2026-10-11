#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace melodeus::app {

/// Free-list de descriptores sobre un heap shader-visible. Lo usa el backend
/// de ImGui 1.92 para crear las SRV de sus texturas (font atlas y demás).
struct DescriptorHeapAllocator {
    ID3D12DescriptorHeap* heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
    D3D12_CPU_DESCRIPTOR_HANDLE heap_start_cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE heap_start_gpu{};
    UINT heap_increment = 0;
    std::vector<int> free_indices;

    void create(ID3D12Device* device, ID3D12DescriptorHeap* in_heap);
    void destroy();
    void alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu);
    void free(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);
};

/// Dispositivo D3D12 + swapchain FLIP_DISCARD con latencia máxima, fence por
/// frame y heaps RTV/SRV. Spike de F0: sin recreación de memoria de profundidad
/// (ImGui no la usa) y sin soporte de alt-Enter (lo gestiona la app).
class DeviceD3D12 {
public:
    static constexpr UINT kFramesInFlight = 2;
    static constexpr UINT kBackBuffers = 2;
    static constexpr UINT kSrvHeapSize = 64;

    DeviceD3D12() = default;
    ~DeviceD3D12();
    DeviceD3D12(const DeviceD3D12&) = delete;
    DeviceD3D12& operator=(const DeviceD3D12&) = delete;

    bool init(HWND hwnd);
    void shutdown();
    bool resize(UINT width, UINT height);

    /// Espera al slot de frame, resetea su allocator y devuelve la lista con
    /// el backbuffer en RENDER_TARGET ya limpiado y configurado (OMSetRenderTargets).
    ID3D12GraphicsCommandList* begin_frame(const float clear_color[4]);

    /// Ejecuta la lista, cierra la transición a PRESENT, senala el fence y
    /// presenta (vsync o con tearing si está soportado).
    void end_frame(bool vsync);

    /// Drena la cola (idle completo); sólo para teardown.
    void wait_idle();

    /// true si el último Present devolvió DXGI_STATUS_OCCLUDED.
    bool occluded() const {
        return occluded_;
    }

    /// Slot de frame actual (tras begin_frame): base para los upload heaps
    /// con kFramesInFlight en vuelo.
    UINT frame_slot() const {
        return frame_index_ % kFramesInFlight;
    }

    ID3D12Device* device() const {
        return device_.Get();
    }
    ID3D12CommandQueue* queue() const {
        return queue_.Get();
    }
    ID3D12DescriptorHeap* srv_heap() const {
        return srv_heap_.Get();
    }
    DescriptorHeapAllocator& srv_alloc() {
        return srv_alloc_;
    }
    DXGI_FORMAT rtv_format() const {
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    const std::wstring& adapter_name() const {
        return adapter_name_;
    }

private:
    struct FrameContext {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        UINT64 fence_value = 0;
    };

    bool create_render_targets();
    void cleanup_render_targets();
    void wait_next_frame(FrameContext** out_ctx, UINT* out_backbuffer);

    HWND hwnd_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmd_list_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv_heap_;
    DescriptorHeapAllocator srv_alloc_;
    FrameContext frames_[kFramesInFlight];
    Microsoft::WRL::ComPtr<ID3D12Resource> back_buffers_[kBackBuffers];
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles_[kBackBuffers]{};
    HANDLE fence_event_ = nullptr;
    HANDLE latency_waitable_ = nullptr;
    UINT64 fence_last_signaled_ = 0;
    UINT frame_index_ = 0;
    bool tearing_support_ = false;
    bool occluded_ = false;
    bool initialized_ = false;
    std::wstring adapter_name_;
};

} // namespace melodeus::app
