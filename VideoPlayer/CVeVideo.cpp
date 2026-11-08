#include "pch.h"
#include "CVeVideo.h"

LRESULT CVeVideo::OnEvent(UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept
{
    switch (uMsg)
    {
    case WM_PAINT:
    {
        Dui::PAINTINFO ps;
        BeginPaint(ps, wParam, lParam);

        if (!m_Frame.cx || !m_Frame.cy ||
            !m_cxTexture || !m_cyTexture)
        {
            GetDC()->FillRectangle(ps.rcClip,
                GetWindow().CcSetBrushColor(D2D1::ColorF{ D2D1::ColorF::Black }));
        }
        else
        {
            GetDC()->Flush();
            ComPtr<ID2D1Image> pTarget;
            ComPtr<ID2D1Bitmap1> pBitmap;
            ComPtr<IDXGISurface> pSurface;
            ComPtr<ID3D11Texture2D> pTargetTexture;
            GetDC()->GetTarget(&pTarget);
            pTarget.As(pBitmap);
            pBitmap->GetSurface(&pSurface);
            pSurface.As(pTargetTexture);

            const auto State = DxSaveState();
            DxRenderVideo(ps, pTargetTexture.Get());
            DxRestoreState(State);
        }

        EndPaint(ps);
    }
    return 0;

    case WM_CREATE:
        DxInitialize();
        break;
    case WM_DESTROY:
        DxUninitialize();
        break;
    }
    return __super::OnEvent(uMsg, wParam, lParam);
}

void CVeVideo::SetVideoFrame(CPlayer::VideoFrame Frame) noexcept
{
    m_Frame = std::move(Frame);
    if (m_Frame.pTexture)
    {
        DxHandleHardwareTexture();
        m_TextureY.Clear();
        m_TextureUv.Clear();
        m_bHardwareDecode = true;
    }
    else
    {
        DxHandleSoftwareTexture();
        m_bHardwareDecode = false;
    }
}

void CVeVideo::ResetVideo() noexcept
{
    m_SrvY.Clear();
    m_SrvUv.Clear();
    m_TextureY.Clear();
    m_TextureUv.Clear();
    m_Frame = {};
    m_cxTexture = 0;
    m_cyTexture = 0;
    m_eFormatY = DXGI_FORMAT_UNKNOWN;
    m_eFormatUv = DXGI_FORMAT_UNKNOWN;
}