#include "render/logo_texture.hpp"
#include "res/logo_pixels.hpp"

#include <cstring>

namespace melodeus::app {

LogoTexture::~LogoTexture() {
    // Los recursos ComPtr se liberan solos, pero el descriptor debe liberarse en shutdown()
}

bool LogoTexture::init(DeviceD3D12& device) {
    if (texture_ != nullptr) {
        return true;
    }
    ID3D12Device* dev = device.device();
    if (dev == nullptr) {
        return false;
    }

    width_ = res::kLogoWidth;
    height_ = res::kLogoHeight;

    // 1. Textura en DEFAULT heap
    D3D12_HEAP_PROPERTIES heap_def{};
    heap_def.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_def.CreationNodeMask = 1;
    heap_def.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = static_cast<UINT64>(width_);
    desc.Height = static_cast<UINT>(height_);
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(dev->CreateCommittedResource(&heap_def, D3D12_HEAP_FLAG_NONE, &desc,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                            IID_PPV_ARGS(&texture_)))) {
        return false;
    }

    // 2. Buffer en UPLOAD heap
    const UINT64 row_pitch = static_cast<UINT64>(width_) * 4;
    const UINT64 upload_size = row_pitch * height_;

    D3D12_HEAP_PROPERTIES heap_up{};
    heap_up.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap_up.CreationNodeMask = 1;
    heap_up.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc_up{};
    desc_up.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc_up.Alignment = 0;
    desc_up.Width = upload_size;
    desc_up.Height = 1;
    desc_up.DepthOrArraySize = 1;
    desc_up.MipLevels = 1;
    desc_up.Format = DXGI_FORMAT_UNKNOWN;
    desc_up.SampleDesc.Count = 1;
    desc_up.SampleDesc.Quality = 0;
    desc_up.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc_up.Flags = D3D12_RESOURCE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3D12Resource> upload_buf;
    if (FAILED(dev->CreateCommittedResource(&heap_up, D3D12_HEAP_FLAG_NONE, &desc_up,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                            IID_PPV_ARGS(&upload_buf)))) {
        texture_.Reset();
        return false;
    }

    void* mapped = nullptr;
    D3D12_RANGE range_read{0, 0};
    if (FAILED(upload_buf->Map(0, &range_read, &mapped)) || mapped == nullptr) {
        texture_.Reset();
        return false;
    }
    std::memcpy(mapped, res::kLogoRgba, upload_size);
    upload_buf->Unmap(0, nullptr);

    // 3. Command list temporal para copia
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> cmd_alloc;
    if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           IID_PPV_ARGS(&cmd_alloc)))) {
        texture_.Reset();
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmd_list;
    if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_alloc.Get(),
                                      nullptr, IID_PPV_ARGS(&cmd_list)))) {
        texture_.Reset();
        return false;
    }

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = texture_.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = upload_buf.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = 0;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = static_cast<UINT>(width_);
    src.PlacedFootprint.Footprint.Height = static_cast<UINT>(height_);
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(row_pitch);

    cmd_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd_list->ResourceBarrier(1, &barrier);

    if (FAILED(cmd_list->Close())) {
        texture_.Reset();
        return false;
    }

    ID3D12CommandList* pp_cmd[] = {cmd_list.Get()};
    device.queue()->ExecuteCommandLists(1, pp_cmd);
    device.wait_idle();

    // 4. Descriptor SRV para ImGui
    device.srv_alloc().alloc(&cpu_handle_, &gpu_handle_);
    descriptor_allocated_ = true;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;

    dev->CreateShaderResourceView(texture_.Get(), &srv_desc, cpu_handle_);

    return true;
}

void LogoTexture::shutdown(DeviceD3D12& device) {
    if (descriptor_allocated_) {
        device.srv_alloc().free(cpu_handle_, gpu_handle_);
        descriptor_allocated_ = false;
    }
    texture_.Reset();
}

LogoTexture& global_logo() {
    static LogoTexture s_logo;
    return s_logo;
}

} // namespace melodeus::app
