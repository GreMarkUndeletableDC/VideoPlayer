#pragma once
#include "CPlayer.h"

class CVeVideo : public Dui::CElement
{
private:
    ComPtr<ID3D11Device> m_pDevice{};
    ComPtr<ID3D11DeviceContext> m_pContext{};
    ComPtr<ID3D11SamplerState> m_pSamplerY{};
    ComPtr<ID3D11SamplerState> m_pSamplerUv{};
    EzDx::CShaderResourceView m_SrvY{};
    EzDx::CShaderResourceView m_SrvUv{};
    EzDx::CVSAndInputLayout m_VsAndIl{};
    EzDx::CPixelShader m_Ps{};
    EzDx::CBuffer m_Vb{};
    EzDx::CBuffer m_Cb{};
    EzDx::CTexture m_TextureY{};
    EzDx::CTexture m_TextureUv{};
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