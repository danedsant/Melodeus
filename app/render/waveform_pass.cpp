#include "render/waveform_pass.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace melodeus::app {

namespace {

/// Carga un .cso compilado con fxc (junto al exe). nullptr si falta.
std::vector<BYTE> load_cso(const wchar_t* name) {
    wchar_t module_path[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    std::wstring path(module_path);
    const std::size_t slash = path.find_last_of(L'\\');
    path = (slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1)) + name;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return {};
    }
    const std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<BYTE> bytes(static_cast<std::size_t>(size));
    if (size <= 0 || !f.read(reinterpret_cast<char*>(bytes.data()), size)) {
        return {};
    }
    return bytes;
}

} // namespace

bool WaveformPass::init(DeviceD3D12& device) {
    const std::vector<BYTE> vs_blob = load_cso(L"waveform.vso");
    const std::vector<BYTE> ps_blob = load_cso(L"waveform.pso");
    if (vs_blob.empty() || ps_blob.empty()) {
        return false;
    }

    D3D12_ROOT_SIGNATURE_DESC rs_desc{};
    rs_desc.NumParameters = 0;
    rs_desc.NumStaticSamplers = 0;
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Microsoft::WRL::ComPtr<ID3DBlob> rs_blob;
    Microsoft::WRL::ComPtr<ID3DBlob> rs_error;
    if (FAILED(::D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob,
                                             &rs_error)) ||
        FAILED(device.device()->CreateRootSignature(
            0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(), IID_PPV_ARGS(&root_sig_)))) {
        return false;
    }

    static const D3D12_INPUT_ELEMENT_DESC kLayout[] = {
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         static_cast<UINT>(offsetof(wave::WaveInstance, x0)),
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0,
         static_cast<UINT>(offsetof(wave::WaveInstance, y0)),
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         static_cast<UINT>(offsetof(wave::WaveInstance, r)),
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = root_sig_.Get();
    pso.VS = {vs_blob.data(), vs_blob.size()};
    pso.PS = {ps_blob.data(), ps_blob.size()};
    pso.BlendState.AlphaToCoverageEnable = FALSE;
    pso.BlendState.IndependentBlendEnable = FALSE;
    pso.BlendState.RenderTarget[0].BlendEnable = FALSE;
    pso.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
    pso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    pso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    pso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.FrontCounterClockwise = FALSE;
    pso.RasterizerState.DepthBias = 0;
    pso.RasterizerState.DepthBiasClamp = 0.0f;
    pso.RasterizerState.SlopeScaledDepthBias = 0.0f;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.RasterizerState.MultisampleEnable = FALSE;
    pso.RasterizerState.AntialiasedLineEnable = FALSE;
    pso.RasterizerState.ForcedSampleCount = 0;
    pso.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.InputLayout = {kLayout, static_cast<UINT>(std::size(kLayout))};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = device.rtv_format();
    pso.SampleDesc.Count = 1;
    if (FAILED(device.device()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pso_)))) {
        return false;
    }

    const UINT64 upload_bytes = kMaxInstances * sizeof(wave::WaveInstance);
    for (UINT i = 0; i < DeviceD3D12::kFramesInFlight; ++i) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC res{};
        res.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        res.Alignment = 0;
        res.Width = upload_bytes;
        res.Height = 1;
        res.DepthOrArraySize = 1;
        res.MipLevels = 1;
        res.Format = DXGI_FORMAT_UNKNOWN;
        res.SampleDesc.Count = 1;
        res.SampleDesc.Quality = 0;
        res.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        res.Flags = D3D12_RESOURCE_FLAG_NONE;
        if (FAILED(device.device()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &res, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&uploads_[i].buf)))) {
            return false;
        }
        D3D12_RANGE range{0, 0};
        if (FAILED(
                uploads_[i].buf->Map(0, &range, reinterpret_cast<void**>(&uploads_[i].mapped)))) {
            return false;
        }
        // Se mapea una vez y no se desmapea (upload heap, vida útil = app).
    }

    frame_slot_ = 0;
    cursor_ = 0;
    initialized_ = true;
    return true;
}

void WaveformPass::shutdown() {
    for (UploadSlot& slot : uploads_) {
        if (slot.buf != nullptr && slot.mapped != nullptr) {
            slot.buf->Unmap(0, nullptr);
        }
        slot.mapped = nullptr;
        slot.buf.Reset();
    }
    pso_.Reset();
    root_sig_.Reset();
    initialized_ = false;
}

void WaveformPass::begin_frame(UINT frame_slot) {
    frame_slot_ = frame_slot % DeviceD3D12::kFramesInFlight;
    cursor_ = 0;
}

void WaveformPass::draw_lane(ID3D12GraphicsCommandList* cmd, const wave::WaveInstance* instances,
                             std::size_t count, const RECT& lane_px) {
    if (!initialized_ || cmd == nullptr || instances == nullptr || count == 0) {
        return;
    }
    const LONG vw = lane_px.right - lane_px.left;
    const LONG vh = lane_px.bottom - lane_px.top;
    if (vw <= 0 || vh <= 0) {
        return;
    }
    if (cursor_ + count > kMaxInstances) {
        count = kMaxInstances - cursor_;
    }
    if (count == 0) {
        return;
    }

    UploadSlot& slot = uploads_[frame_slot_];
    std::memcpy(slot.mapped + cursor_, instances, count * sizeof(wave::WaveInstance));

    D3D12_VIEWPORT vp{static_cast<FLOAT>(lane_px.left),
                      static_cast<FLOAT>(lane_px.top),
                      static_cast<FLOAT>(vw),
                      static_cast<FLOAT>(vh),
                      0.0f,
                      1.0f};
    D3D12_RECT sc{lane_px.left, lane_px.top, lane_px.right, lane_px.bottom};
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);
    cmd->SetPipelineState(pso_.Get());
    cmd->SetGraphicsRootSignature(root_sig_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = slot.buf->GetGPUVirtualAddress() +
                         static_cast<UINT64>(cursor_) * sizeof(wave::WaveInstance);
    vbv.SizeInBytes = static_cast<UINT>(count * sizeof(wave::WaveInstance));
    vbv.StrideInBytes = sizeof(wave::WaveInstance);
    cmd->IASetVertexBuffers(1, 1, &vbv);
    cmd->DrawInstanced(6, static_cast<UINT>(count), 0, 0);
    cursor_ += count;
}

} // namespace melodeus::app
