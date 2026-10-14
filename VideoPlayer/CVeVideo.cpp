#include "pch.h"
#include "CVeVideo.h"

constexpr static std::string_view VertexShader
{ R"(
struct VSOut
{
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

VSOut main(float4 pos : POSITION, float2 tex : TEXCOORD0)
{
    VSOut r;
    r.pos = pos;
    r.tex = tex;
    return r;
}
)" };

constexpr static std::string_view PixelShader
{ R"(
Texture2DArray<float> gTexY : register(t0);
Texture2DArray<float2> gTexUV : register(t1);
SamplerState gSamplerY : register(s0);
SamplerState gSamplerUV : register(s1);

cbuffer Cb : register(b0)
{
    float2 gUvMax;
    float2 gUnused;
    row_major matrix gYuv2Rgb;
};

struct PSIn
{
    float4 pos : SV_POSITION;
    float2 uvPos : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    if (input.uvPos.x < 0.f || input.uvPos.y < 0.f ||
        input.uvPos.x > gUvMax.x || input.uvPos.y > gUvMax.y)
        return float4(0.f, 0.f, 0.f, 1.f);

    const float y = gTexY.Sample(gSamplerY, float3(input.uvPos, 0));
    const float2 uv = gTexUV.Sample(gSamplerUV, float3(input.uvPos, 0));
    const float3 rgb = mul(gYuv2Rgb, float4(y, uv, 1.f)).rgb;
    return float4(saturate(rgb), 1);
}
)" };

struct alignas(16) VERTEX
{
    DirectX::XMFLOAT4 pos;
    DirectX::XMFLOAT2 uv;
};

constexpr static VERTEX ConstantVertices[]
{
    { { -1.f,  1.f, 0.f, 1.f }, { 0.f, 0.f } },
    { {  1.f,  1.f, 0.f, 1.f }, { 1.f, 0.f } },
    { { -1.f, -1.f, 0.f, 1.f }, { 0.f, 1.f } },
    { {  1.f, -1.f, 0.f, 1.f }, { 1.f, 1.f } }
};

struct alignas(16) CONSTANT_BUFFER
{
    DirectX::XMFLOAT2 UvMax;
    DirectX::XMFLOAT2 Unused;
    DirectX::XMFLOAT4X4 Yuv2Rgb;
};

HRESULT CVeVideo::DxInitialize() noexcept
{
    HRESULT hr;

    m_pDevice = eck::g_pD3D11Device;
    m_pDevice->GetImmediateContext(&m_pContext);

    constexpr D3D11_INPUT_ELEMENT_DESC Layout[]
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,       },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 16 },
    };

    ComPtr<ID3DBlob> pVsBlob, pPsBlob;
    hr = Dx3D::CompileShader(
        pVsBlob.Self(),
        VertexShader.data(), (int)VertexShader.size(),
        "main",
        "vs_5_0");
    if (FAILED(hr))
        return hr;

    hr = Dx3D::CompileShader(
        pPsBlob.Self(),
        PixelShader.data(), (int)PixelShader.size(),
        "main",
        "ps_5_0");
    if (FAILED(hr))
        return hr;

    hr = m_VsAndIl.Create(
        nullptr,
        pVsBlob->GetBufferPointer(),
        pVsBlob->GetBufferSize(),
        Layout, ARRAYSIZE(Layout));
    if (FAILED(hr))
        return hr;

    hr = m_Ps.Create(
        nullptr,
        pPsBlob->GetBufferPointer(),
        pPsBlob->GetBufferSize());
    if (FAILED(hr))
        return hr;

    hr = m_Vb.Create(
        nullptr,
        sizeof(ConstantVertices),
        D3D11_BIND_VERTEX_BUFFER,
        D3D11_USAGE_DYNAMIC,
        D3D11_CPU_ACCESS_WRITE);
    if (FAILED(hr))
        return hr;

    hr = m_Cb.CreateConstantBuffer(nullptr, sizeof(CONSTANT_BUFFER));
    if (FAILED(hr))
        return hr;

    hr = DxCreateSampler();
    if (FAILED(hr))
        return hr;
    return S_OK;
}

HRESULT CVeVideo::DxCreateSampler() noexcept
{
    HRESULT hr;

    D3D11_SAMPLER_DESC Desc{};
    Desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    Desc.AddressU = Desc.AddressV = Desc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    Desc.MaxAnisotropy = 1;
    Desc.MaxLOD = FLT_MAX;
    Desc.MinLOD = -FLT_MAX;

    Desc.BorderColor[0] = 0.0627f;
    hr = m_pDevice->CreateSamplerState(&Desc, m_pSamplerY.AtClear());
    if (FAILED(hr))
        return hr;

    Desc.BorderColor[0] = Desc.BorderColor[1] = 0.5020f;
    hr = m_pDevice->CreateSamplerState(&Desc, m_pSamplerUv.AtClear());
    if (FAILED(hr))
        return hr;
    return S_OK;
}

HRESULT CVeVideo::DxHandleHardwareTexture(
    _In_ ID3D11Texture2D* pTexture,
    UINT idxSubResource) noexcept
{
    HRESULT hr;
    D3D11_TEXTURE2D_DESC Desc{};
    pTexture->GetDesc(&Desc);

    DXGI_FORMAT eFormatY = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT eFormatUv = DXGI_FORMAT_UNKNOWN;
    if (Desc.Format == DXGI_FORMAT_NV12)
    {
        eFormatY = DXGI_FORMAT_R8_UNORM;
        eFormatUv = DXGI_FORMAT_R8G8_UNORM;
    }
    else if (Desc.Format == DXGI_FORMAT_P010 ||
        Desc.Format == DXGI_FORMAT_P016)
    {
        eFormatY = DXGI_FORMAT_R16_UNORM;
        eFormatUv = DXGI_FORMAT_R16G16_UNORM;
    }
    else
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);

    D3D11_SHADER_RESOURCE_VIEW_DESC SrvDesc{};
    SrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    SrvDesc.Texture2DArray.MipLevels = 1;
    SrvDesc.Texture2DArray.FirstArraySlice = m_VideoFrame.idxSubResource;
    SrvDesc.Texture2DArray.ArraySize = 1;

    SrvDesc.Format = eFormatY;
    hr = m_SrvY.Create(nullptr, m_VideoFrame.pTexture.Get(), SrvDesc);
    if (FAILED(hr))
        return hr;

    SrvDesc.Format = eFormatUv;
    hr = m_SrvUv.Create(nullptr, m_VideoFrame.pTexture.Get(), SrvDesc);
    if (FAILED(hr))
        return hr;

    m_cxTexture = Desc.Width;
    m_cyTexture = Desc.Height;
    m_eFormatY = eFormatY;
    m_eFormatUv = eFormatUv;
    return S_OK;
}

HRESULT CVeVideo::DxHandleSoftwareTexture(
    UINT cx, UINT cy,
    const CPlayer::VideoFrameContext& Context) noexcept
{
    if (!Context.pPlaneY || !Context.pPlaneUv)
        return E_POINTER;

    HRESULT hr;
    const auto eFormatY = Context.b10Bit ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
    const auto eFormatUv = Context.b10Bit ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
    const auto cxChroma = (cx + 1) / 2;
    const auto cyChroma = (cy + 1) / 2;

    if (m_bHardwareDecode ||
        m_cxTexture != cx ||
        m_cyTexture != cy)
    {
        if (m_eFormatY != eFormatY || !m_TextureY.Get())
        {
            hr = m_TextureY.Create(nullptr, cx, cy, eFormatY, D3D11_BIND_SHADER_RESOURCE);
            if (FAILED(hr))
                return hr;
            hr = m_SrvY.Create(nullptr, m_TextureY.Get(), eFormatY);
            if (FAILED(hr))
                return hr;
            m_eFormatY = eFormatY;
        }

        if (m_eFormatUv != eFormatUv || !m_TextureUv.Get())
        {
            hr = m_TextureUv.Create(nullptr,
                cxChroma, cyChroma, eFormatUv, D3D11_BIND_SHADER_RESOURCE);
            if (FAILED(hr))
                return hr;
            hr = m_SrvUv.Create(nullptr, m_TextureUv.Get(), eFormatUv);
            if (FAILED(hr))
                return hr;
            m_eFormatUv = eFormatUv;
        }

        m_bHardwareDecode = FALSE;
        m_cxTexture = cx;
        m_cyTexture = cy;
    }

    ID3D11ShaderResourceView* const NullSrv[2]{};
    m_pContext->PSSetShaderResources(0, 2, NullSrv);

    const D3D11_BOX BoxY{ 0, 0, 0, cx, cy, 1 };
    m_pContext->UpdateSubresource(
        m_TextureY.Get(),
        0,
        &BoxY,
        Context.pPlaneY,
        Context.cbStrideY,
        0);

    const D3D11_BOX BoxUv{ 0, 0, 0, cxChroma, cyChroma, 1 };
    m_pContext->UpdateSubresource(
        m_TextureUv.Get(),
        0,
        &BoxUv,
        Context.pPlaneUv,
        Context.cbStrideUv,
        0);
    m_pContext->Flush();
    return S_OK;
}

void CVeVideo::DxRenderVideo(const Dui::PAINTINFO& ps, ID3D11Texture2D* pTarget) noexcept
{
    Dx3D::CRenderTargetView Rtv{};
    Rtv.Create(nullptr, pTarget);
    m_pContext->OMSetRenderTargets(1, &Rtv, nullptr);

    // --

    const D3D11_VIEWPORT Viewport
    {
        .TopLeftX = LogicalToPixel(ps.rcClip.left + ps.ox),
        .TopLeftY = LogicalToPixel(ps.rcClip.top + ps.oy),
        .Width = LogicalToPixel(ps.rcClip.right - ps.rcClip.left),
        .Height = LogicalToPixel(ps.rcClip.bottom - ps.rcClip.top),
        .MinDepth = 0.f,
        .MaxDepth = 1.f,
    };
    m_pContext->RSSetViewports(1, &Viewport);

    // --

    m_pContext->IASetInputLayout(m_VsAndIl.GetInputLayout());
    m_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    VERTEX Vert[4];

    D2D1_RECT_F rcVideo{ 0.f, 0.f, (float)m_VideoFrame.cx, (float)m_VideoFrame.cy };
    eck::AdjustRectToFitAnother(rcVideo, GetRectInClientD2D());

    const auto kScaleU = (float)m_VideoFrame.cx / (float)m_cxTexture;
    const auto kScaleV = (float)m_VideoFrame.cy / (float)m_cyTexture;

    const auto cxVideo = rcVideo.right - rcVideo.left;
    const auto cyVideo = rcVideo.bottom - rcVideo.top;
    const auto x0 = (ps.rcClip.left - rcVideo.left) * kScaleU / cxVideo;
    const auto y0 = (ps.rcClip.top - rcVideo.top) * kScaleV / cyVideo;
    const auto x1 = (ps.rcClip.right - rcVideo.left) * kScaleU / cxVideo;
    const auto y1 = (ps.rcClip.bottom - rcVideo.top) * kScaleV / cyVideo;
    Vert[0].uv = { x0, y0 };// 左上
    Vert[0].pos = ConstantVertices[0].pos;
    Vert[1].uv = { x1, y0 };// 右上
    Vert[1].pos = ConstantVertices[1].pos;
    Vert[2].uv = { x0, y1 };// 左下
    Vert[2].pos = ConstantVertices[2].pos;
    Vert[3].uv = { x1, y1 };// 右下
    Vert[3].pos = ConstantVertices[3].pos;

    m_Vb.UpdateDynamicBuffer(m_pContext.Get(), &Vert, sizeof(Vert));

    constexpr UINT Stride = sizeof(VERTEX);
    constexpr UINT Offset = 0;
    m_pContext->IASetVertexBuffers(0, 1, &m_Vb, &Stride, &Offset);

    // --

    m_pContext->VSSetShader(m_VsAndIl.GetShader(), nullptr, 0);

    m_pContext->PSSetShader(m_Ps.Get(), nullptr, 0);
    m_pContext->PSSetSamplers(0, 2, &m_pSamplerY);
    m_pContext->PSSetShaderResources(0, 2, &m_SrvY);

    CONSTANT_BUFFER Cb{};
    Cb.UvMax = { kScaleU, kScaleV };
    if (m_cyTexture != m_VideoFrame.cy)
        Cb.UvMax.y -= (1.f / m_cyTexture);
    memcpy(&Cb.Yuv2Rgb, m_VideoFrame.Yuv2Rgb, sizeof(m_VideoFrame.Yuv2Rgb));
    Cb.Yuv2Rgb._41 = 0.f;
    Cb.Yuv2Rgb._42 = 0.f;
    Cb.Yuv2Rgb._43 = 0.f;
    Cb.Yuv2Rgb._44 = 1.f;

    m_Cb.UpdateDynamicBuffer(m_pContext.Get(), &Cb, sizeof(Cb));
    m_pContext->PSSetConstantBuffers(0, 1, &m_Cb);

    // --

    m_pContext->Draw(4, 0);
    m_pContext->Flush();
}

LRESULT CVeVideo::OnEvent(UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept
{
    switch (uMsg)
    {
    case WM_PAINT:
    {
        Dui::PAINTINFO ps;
        BeginPaint(ps, wParam, lParam);

        GetDC()->Flush();

        ComPtr<ID2D1Image> pTarget;
        ComPtr<ID2D1Bitmap1> pBitmap;
        ComPtr<IDXGISurface> pSurface;
        ComPtr<ID3D11Texture2D> pTargetTexture;
        GetDC()->GetTarget(&pTarget);
        pTarget.As(pBitmap);
        pBitmap->GetSurface(&pSurface);
        pSurface.As(pTargetTexture);

        DxRenderVideo(ps, pTargetTexture.Get());

        EndPaint(ps);
    }
    return 0;

    case WM_CREATE:
        DxInitialize();
        break;
    }
    return __super::OnEvent(uMsg, wParam, lParam);
}

void CVeVideo::SetVideoFrame(
    CPlayer::VideoFrame Frame,
    const CPlayer::VideoFrameContext& Context) noexcept
{
    m_VideoFrame = std::move(Frame);
    if (m_VideoFrame.pTexture)
    {
        DxHandleHardwareTexture(
            m_VideoFrame.pTexture.Get(),
            m_VideoFrame.idxSubResource);
        m_TextureY.Clear();
        m_TextureUv.Clear();
        m_bHardwareDecode = TRUE;
    }
    else
    {
        DxHandleSoftwareTexture(
            m_VideoFrame.cx, m_VideoFrame.cy,
            Context);
        m_bHardwareDecode = FALSE;
    }
}