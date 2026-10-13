#pragma once

#include "render/device_d3d12.hpp"
#include "imgui.h"
#include <wrl/client.h>

namespace melodeus::app {

class LogoTexture {
public:
    LogoTexture() = default;
    ~LogoTexture();

    bool init(DeviceD3D12& device);
    void shutdown(DeviceD3D12& device);

    bool is_valid() const { return texture_ != nullptr; }
    ImTextureID texture_id() const { return static_cast<ImTextureID>(gpu_handle_.ptr); }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> texture_;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle_{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_{};
    bool descriptor_allocated_ = false;
    int width_ = 256;
    int height_ = 256;
};

LogoTexture& global_logo();

} // namespace melodeus::app
