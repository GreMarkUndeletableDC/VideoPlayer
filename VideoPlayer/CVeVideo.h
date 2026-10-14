#pragma once
#include "CPlayer.h"

class CVeVideo : public Dui::CElement
{
private:
    ComPtr<ID3D11Device> m_pDevice{};
    ComPtr<ID3D11DeviceContext> m_pContext{};
    ComPtr<ID3D11SamplerState> m_pSamplerY{};
    ComPtr<ID3D11SamplerState> m_pSamplerUv{};
    Dx3D::CShaderResourceView m_SrvY{};
    Dx3D::CShaderResourceView m_SrvUv{};
    Dx3D::CVertexShaderAndInputLayout m_VsAndIl{};
    Dx3D::CPixelShader m_Ps{};
    Dx3D::CBuffer m_Vb{};
    Dx3D::CBuffer m_Cb{};
    Dx3D::CTexture m_TextureY{};
    Dx3D::CTexture m_TextureUv{};
    UINT m_cxTexture{}, m_cyTexture{};

    CPlayer::VideoFrame m_VideoFrame{};

    eck::CByteBuffer m_rbSoftware{};

    DXGI_FORMAT m_eFormatY{}, m_eFormatUv{};
    BOOLEAN m_bHardwareDecode{};


    HRESULT DxInitialize() noexcept;
    HRESULT DxCreateSampler() noexcept;
    HRESULT DxHandleHardwareTexture(
        _In_ ID3D11Texture2D* pTexture,
        UINT idxSubResource) noexcept;
    HRESULT DxHandleSoftwareTexture(
        UINT cx, UINT cy,
        const CPlayer::VideoFrameContext& Context) noexcept;
    void DxRenderVideo(const Dui::PAINTINFO& ps, ID3D11Texture2D* pTarget) noexcept;
public:
    LRESULT OnEvent(UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept override;

    void SetVideoFrame(
        CPlayer::VideoFrame Frame,
        const CPlayer::VideoFrameContext& Context) noexcept;

    EckInlineNdCe auto& GetSoftwareByteBuffer() noexcept { return m_rbSoftware; }
};