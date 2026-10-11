#include "render/device_d3d12.hpp"

namespace melodeus::app {

// ----------------------------------------------------------- DescriptorHeap

void DescriptorHeapAllocator::create(ID3D12Device* device, ID3D12DescriptorHeap* in_heap) {
    heap = in_heap;
    const D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
    heap_type = desc.Type;
    heap_start_cpu = heap->GetCPUDescriptorHandleForHeapStart();
    heap_start_gpu = heap->GetGPUDescriptorHandleForHeapStart();
    heap_increment = device->GetDescriptorHandleIncrementSize(heap_type);
    free_indices.reserve(desc.NumDescriptors);
    for (UINT n = desc.NumDescriptors; n > 0; --n) {
        free_indices.push_back(static_cast<int>(n - 1));
    }
}

void DescriptorHeapAllocator::destroy() {
    heap = nullptr;
    free_indices.clear();
}

void DescriptorHeapAllocator::alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu,
                                    D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) {
    const int idx = free_indices.back();
    free_indices.pop_back();
    const SIZE_T offset = static_cast<SIZE_T>(idx) * heap_increment;
    out_cpu->ptr = heap_start_cpu.ptr + offset;
    out_gpu->ptr = heap_start_gpu.ptr + offset;
}

void DescriptorHeapAllocator::free(D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                                   D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
    const auto cpu_idx = static_cast<int>((cpu.ptr - heap_start_cpu.ptr) / heap_increment);
    const auto gpu_idx = static_cast<int>((gpu.ptr - heap_start_gpu.ptr) / heap_increment);
    free_indices.push_back(cpu_idx == gpu_idx ? cpu_idx : gpu_idx);
}

// ---------------------------------------------------------------- Device

DeviceD3D12::~DeviceD3D12() {
    shutdown();
}

bool DeviceD3D12::init(HWND hwnd) {
    hwnd_ = hwnd;

    if (CreateDXGIFactory1(IID_PPV_ARGS(&factory_)) != S_OK) {
        return false;
    }

    BOOL allow_tearing = FALSE;
    Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory_.As(&factory5))) {
        factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing,
                                      sizeof(allow_tearing));
    }
    tearing_support_ = allow_tearing == TRUE;

    // Adapter de hardware preferente (discreto si existe): omitir el WARP.
    Microsoft::WRL::ComPtr<IDXGIAdapter1> chosen;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory_->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                        __uuidof(ID3D12Device), nullptr))) {
            adapter_name_ = desc.Description;
            chosen = adapter;
            break;
        }
    }
    if (adapter_name_.empty()) {
        // Fallback al adapter por defecto del sistema.
        adapter_name_ = L"(adapter por defecto)";
    }
    if (FAILED(D3D12CreateDevice(chosen.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)))) {
        return false;
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = kBackBuffers;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtv_heap_)) != S_OK) {
            return false;
        }
        const UINT rtv_inc =
            device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < kBackBuffers; ++i) {
            rtv_handles_[i] = handle;
            handle.ptr += rtv_inc;
        }
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = kSrvHeapSize;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srv_heap_)) != S_OK) {
            return false;
        }
        srv_alloc_.create(device_.Get(), srv_heap_.Get());
    }

    {
        D3D12_COMMAND_QUEUE_DESC desc{};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue_)) != S_OK) {
            return false;
        }
    }

    for (UINT i = 0; i < kFramesInFlight; ++i) {
        if (device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&frames_[i].allocator)) != S_OK) {
            return false;
        }
    }

    if (device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames_[0].allocator.Get(),
                                   nullptr, IID_PPV_ARGS(&cmd_list_)) != S_OK ||
        cmd_list_->Close() != S_OK) {
        return false;
    }

    if (device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)) != S_OK) {
        return false;
    }
    fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (fence_event_ == nullptr) {
        return false;
    }

    {
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.BufferCount = kBackBuffers;
        sd.Format = rtv_format();
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SampleDesc.Count = 1;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        if (tearing_support_) {
            sd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }

        Microsoft::WRL::ComPtr<IDXGISwapChain1> swap1;
        if (factory_->CreateSwapChainForHwnd(queue_.Get(), hwnd_, &sd, nullptr, nullptr, &swap1) !=
            S_OK) {
            return false;
        }
        if (swap1.As(&swap_chain_) != S_OK) {
            return false;
        }
        if (tearing_support_) {
            factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
        }
        swap_chain_->SetMaximumFrameLatency(kBackBuffers);
        latency_waitable_ = swap_chain_->GetFrameLatencyWaitableObject();
    }

    if (!create_render_targets()) {
        return false;
    }

    initialized_ = true;
    return true;
}

void DeviceD3D12::shutdown() {
    if (!initialized_) {
        return;
    }
    wait_idle();
    cleanup_render_targets();
    if (swap_chain_) {
        swap_chain_->SetFullscreenState(false, nullptr);
        swap_chain_.Reset();
    }
    if (latency_waitable_ != nullptr) {
        CloseHandle(latency_waitable_);
        latency_waitable_ = nullptr;
    }
    for (auto& frame : frames_) {
        frame.allocator.Reset();
    }
    cmd_list_.Reset();
    queue_.Reset();
    srv_alloc_.destroy();
    srv_heap_.Reset();
    rtv_heap_.Reset();
    fence_.Reset();
    if (fence_event_ != nullptr) {
        CloseHandle(fence_event_);
        fence_event_ = nullptr;
    }
    device_.Reset();
    factory_.Reset();
    initialized_ = false;
}

bool DeviceD3D12::create_render_targets() {
    for (UINT i = 0; i < kBackBuffers; ++i) {
        if (swap_chain_->GetBuffer(i, IID_PPV_ARGS(&back_buffers_[i])) != S_OK) {
            return false;
        }
        device_->CreateRenderTargetView(back_buffers_[i].Get(), nullptr, rtv_handles_[i]);
    }
    return true;
}

void DeviceD3D12::cleanup_render_targets() {
    for (auto& buffer : back_buffers_) {
        buffer.Reset();
    }
}

bool DeviceD3D12::resize(UINT width, UINT height) {
    if (!initialized_ || width == 0 || height == 0) {
        return false;
    }
    wait_idle();
    cleanup_render_targets();
    DXGI_SWAP_CHAIN_DESC1 desc{};
    swap_chain_->GetDesc1(&desc);
    if (swap_chain_->ResizeBuffers(0, width, height, desc.Format, desc.Flags) != S_OK) {
        return false;
    }
    return create_render_targets();
}

void DeviceD3D12::wait_next_frame(FrameContext** out_ctx, UINT* out_backbuffer) {
    FrameContext& ctx = frames_[frame_index_ % kFramesInFlight];
    if (fence_->GetCompletedValue() < ctx.fence_value) {
        fence_->SetEventOnCompletion(ctx.fence_value, fence_event_);
        HANDLE waitables[] = {latency_waitable_, fence_event_};
        ::WaitForMultipleObjects(2, waitables, TRUE, INFINITE);
    } else {
        ::WaitForSingleObject(latency_waitable_, INFINITE);
    }
    *out_ctx = &ctx;
    *out_backbuffer = swap_chain_->GetCurrentBackBufferIndex();
}

ID3D12GraphicsCommandList* DeviceD3D12::begin_frame(const float clear_color[4]) {
    FrameContext* ctx = nullptr;
    UINT backbuffer = 0;
    wait_next_frame(&ctx, &backbuffer);

    ctx->allocator->Reset();
    cmd_list_->Reset(ctx->allocator.Get(), nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = back_buffers_[backbuffer].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cmd_list_->ResourceBarrier(1, &barrier);

    cmd_list_->ClearRenderTargetView(rtv_handles_[backbuffer], clear_color, 0, nullptr);
    cmd_list_->OMSetRenderTargets(1, &rtv_handles_[backbuffer], FALSE, nullptr);
    return cmd_list_.Get();
}

void DeviceD3D12::end_frame(bool vsync) {
    FrameContext& ctx = frames_[frame_index_ % kFramesInFlight];
    const UINT backbuffer = swap_chain_->GetCurrentBackBufferIndex();

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = back_buffers_[backbuffer].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    cmd_list_->ResourceBarrier(1, &barrier);
    cmd_list_->Close();

    ID3D12CommandList* lists[] = {cmd_list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    queue_->Signal(fence_.Get(), ++fence_last_signaled_);
    ctx.fence_value = fence_last_signaled_;

    HRESULT hr;
    if (vsync) {
        hr = swap_chain_->Present(1, 0);
    } else {
        hr = swap_chain_->Present(0, tearing_support_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
    }
    occluded_ = (hr == DXGI_STATUS_OCCLUDED);
    ++frame_index_;
}

void DeviceD3D12::wait_idle() {
    if (!queue_) {
        return;
    }
    queue_->Signal(fence_.Get(), ++fence_last_signaled_);
    if (fence_->GetCompletedValue() < fence_last_signaled_) {
        fence_->SetEventOnCompletion(fence_last_signaled_, fence_event_);
        ::WaitForSingleObject(fence_event_, INFINITE);
    }
}

} // namespace melodeus::app
