#include "pch.h"
#include "CVeVideo.h"

static constexpr UINT MaximumCoverPixelSize = 640;
static constexpr float MaximumCoverSize = 640.f;
static constexpr float CoverBorderWidth = 1.f;

LRESULT CVeVideo::OnEvent(UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept
{
    switch (uMsg)
    {
    case WM_PAINT:
    {
        Dui::PAINTINFO ps;
        BeginPaint(ps, wParam, lParam);

        if (m_bShowCover ||
            !m_Frame.cx || !m_Frame.cy ||
            !m_cxTexture || !m_cyTexture)
        {
            GetDC()->FillRectangle(ps.rcClip,
                GetWindow().CcSetBrushColor(D2D1::ColorF{ D2D1::ColorF::Black }));

            if (m_bShowCover)
            {
                D2D1_RECT_F rcCenter
                {
                    0.f,
                    0.f,
                    std::min(MaximumCoverSize, GetWidth()),
                    std::min(MaximumCoverSize, GetHeight()),
                };
                eck::CenterRect(rcCenter, GetRectInClientD2D());
                const auto Size = m_pCoverBitmap ?
                    m_pCoverBitmap->GetSize() :
                    D2D1_SIZE_F{ 100.f, 100.f };
                D2D1_RECT_F rcCover{ 0.f, 0.f, Size.width, Size.height, };
                eck::AdjustRectToFitAnother(rcCover, rcCenter);

                if (eck::IsRectsIntersect(rcCover, ps.rcClip))
                {
                    if (m_pCoverBitmap)
                        GetDC()->DrawBitmap(
                            m_pCoverBitmap.Get(),
                            rcCover,
                            1.f,
                            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

                    eck::InflateRect(rcCover, -CoverBorderWidth / 2.f, -CoverBorderWidth / 2.f);
                    GetDC()->DrawRectangle(
                        rcCover,
                        GetWindow().CcSetBrushColor(D2D1::ColorF{ D2D1::ColorF::White }),
                        CoverBorderWidth);
                }
            }
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

void CVeVideo::SetCoverBitmap(IWICBitmapSource* pBitmap) noexcept
{
    if (!pBitmap)
    {
        m_pCoverBitmap.Clear();
        return;
    }

    UINT cx, cy;
    pBitmap->GetSize(&cx, &cy);

    ComPtr<IWICBitmapScaler> pScaler;
    if (cx > MaximumCoverPixelSize || cy > MaximumCoverPixelSize)
    {
        constexpr RCWH rcMax{ 0, 0, (int)MaximumCoverPixelSize, (int)MaximumCoverPixelSize };
        RCWH rc{ 0, 0, (int)cx, (int)cy };
        eck::AdjustRectToFitAnother(rc, rcMax);
        eck::WicScaleBitmap(
            pScaler.Self(), pBitmap, rc.cx, rc.cy,
            WICBitmapInterpolationModeFant);
        if (pScaler)
            pBitmap = pScaler.Get();
    }

    GetDC()->CreateBitmapFromWicBitmap(pBitmap, m_pCoverBitmap.AtClear());
}