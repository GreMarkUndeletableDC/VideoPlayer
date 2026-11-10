#include "pch.h"
#include "CWindowMain.h"

static constexpr float
ProgressTrackSize = 6,
ProgressTrackThumbSize = 10,
ProgressTrackTrackBarHeight = 26;

static constexpr double ProgressScale = 100.;

LRESULT CWindowMain::OnCreate()
{
    KctRegisterTimeLine(this);
    KctStartTimer();

    eck::CDWriteFontFactory DwTfFactory{};
    DwTfFactory.NewFont(m_pTextFormat.Self(),
        eck::Alignment::Near, eck::Alignment::Center, -1.f, 400, true);;

    eck::PtcCurrent()->UpdateDefaultColor();

    m_Player.Initialize();

    m_Video.Create({}, Dui::DES_VISIBLE | Dui::DES_NO_CLIP, 0,
        0, 0, 0, 0, nullptr, this);

    m_TB.Create({}, Dui::DES_VISIBLE | Dui::DES_NOTIFY_WND, 0,
        0, 0, 0, 0, nullptr, this);
    m_TB.SetTrackSize(ProgressTrackSize);
    m_TB.SetThumbSize(ProgressTrackThumbSize);
    m_TB.SetRange(0.f, 100.f);
    m_TB.SetThumbTrack(false);
    return 0;
}

LRESULT CWindowMain::OnMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept
{
    switch (uMsg)
    {
    case WM_SIZE:
    {
        const auto lResult = __super::OnMessage(uMsg, wParam, lParam);
        const auto cx = GetClientWidthLogical();
        const auto cy = GetClientHeightLogical();
        m_Video.SetSize(cx, cy);
        m_TB.SetRect({ 0, cy - ProgressTrackTrackBarHeight, cx, cy });
        return lResult;
    }
    break;

    case WM_DROPFILES:
    {
        const auto hDrop = (HDROP)wParam;
        const auto cFile = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        if (cFile)
        {
            const auto cchBuf = DragQueryFileW(hDrop, 0, nullptr, 0);
            eck::CStringW rs{};
            rs.ReSize(cchBuf);
            DragQueryFileW(hDrop, 0, rs.Data(), cchBuf + 1);
            OpenFile(rs.Data());
        }
        DragFinish(hDrop);
    }
    return 0;

    case WM_CREATE:
    {
        const auto lResult = __super::OnMessage(uMsg, wParam, lParam);
        OnCreate();
        return lResult;
    }
    break;

    case WM_DESTROY:
    {
        __super::OnMessage(uMsg, wParam, lParam);
        PostQuitMessage(0);
    }
    return 0;

    case WM_SYSCOLORCHANGE:
        eck::MsgOnSystemColorChangeMainWindow(Handle, wParam, lParam);
        break;
    case WM_SETTINGCHANGE:
        eck::MsgOnSettingChangeMainWindow(Handle, wParam, lParam);
        break;
    }
    return __super::OnMessage(uMsg, wParam, lParam);
}

LRESULT CWindowMain::OnRenderEvent(UINT uMsg, Dui::RENDER_EVENT& e) noexcept
{
    return 0;
}

LRESULT CWindowMain::OnElementNotify(Dui::CElement* pEle, Dui::ELENMHDR* pnm) noexcept
{
    if (pEle == &m_TB)
    {
        switch (pnm->uNotify)
        {
        case Dui::ENC_POSCHANGED:
            Seek(m_TB.GetTrackPosition() / ProgressScale);
            break;
        }
    }
    return 0;
}

void CWindowMain::Seek(double s) noexcept
{
    m_VideoFrame = {};
    m_bHasPendingVideoFrame = false;

    m_Player.Seek(s);

    m_TB.SetTrackPosition(float(s * ProgressScale));
    RdInvalidate();
}

void CWindowMain::OpenFile(PCWSTR pszFile) noexcept
{
    m_VideoFrame = {};
    m_bHasPendingVideoFrame = false;

    m_Video.ResetVideo();
    m_Player.OpenFile(pszFile);

    m_Video.SetShowCover(m_Player.IsOnlyAudio());
    if (m_Player.IsOnlyAudio())
        m_Video.SetCoverBitmap(m_Player.GetCoverBitmap().Get());

    m_TB.SetRange(0.f, float(m_Player.GetDuration() * ProgressScale));
    m_TB.SetTrackPosition(0.f);
    RdInvalidate();
}

void CWindowMain::TlTick(int ms) noexcept
{
    if (m_Player.IsOnlyAudio())
    {
        const auto t = m_Player.GetPosition();
        if (t != CClock::None)
        {
            m_TB.SetTrackPosition(float(t * ProgressScale));
            m_TB.Invalidate();
        }
        return;
    }

    EckLoop()
    {
        if (!m_bHasPendingVideoFrame)
        {
            CPlayer::VideoFrameContext Ctx
            {
                .pSoftwareBuffer = &m_Video.GetSoftwareByteBuffer(),
            };
            if (!m_Player.VideoPopFrame(m_VideoFrame, Ctx))
                break;
            m_bHasPendingVideoFrame = true;
        }

        const auto t = m_Player.VideoGetMasterClock();
        if (t != CClock::None)
        {
            if (m_VideoFrame.pts > t + 0.015)
                break;
            if (m_VideoFrame.pts < t - 0.01)
            {
                m_VideoFrame = {};
                m_bHasPendingVideoFrame = false;
                continue;
            }
        }

        const auto pts = m_VideoFrame.pts;
        m_Player.VideoAnchorClock(pts);
        m_Video.SetVideoFrame(std::move(m_VideoFrame));
        m_VideoFrame = {};
        m_bHasPendingVideoFrame = false;

        m_TB.SetTrackPosition(float(pts * ProgressScale));
        RdInvalidate();
    }
}