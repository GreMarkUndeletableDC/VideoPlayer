#include "pch.h"
#include "CWindowMain.h"

constexpr float
CyTrackSize = 6,
CyTrackBar = 26,
CxPlayList = 300,
CxListContMargin = 10,
CyListContMargin = 10;

constexpr float AnDurList = 400;

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
    m_TB.SetTrackSize(CyTrackSize);
    m_TB.SetThumbSize(10);

    //m_ListContainer.Create(nullptr, Dui::DES_VISIBLE, 0,
    //    0, 0, 0, 0, nullptr, this);
    //m_ListContainer.SetTextFormat(m_pTextFormat.Get());

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
        m_TB.SetRect({ 0, cy - CyTrackBar, cx, cy });
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
    return 0;
}

void CWindowMain::OpenFile(PCWSTR pszFile) noexcept
{
    m_VideoFrame = {};
    m_bHasPendingVideoFrame = false;

    m_Video.ResetVideo();
    m_Player.OpenFile(pszFile);
    RdInvalidate();
}

void CWindowMain::TlTick(int ms) noexcept
{
    EckLoop()
    {
        if (!m_bHasPendingVideoFrame)
        {
            CPlayer::VideoFrameContext Ctx
            {
                .pSoftwareBuffer = &m_Video.GetSoftwareByteBuffer(),
            };
            if (!m_Player.PopVideoFrame(m_VideoFrame, Ctx))
                break;
            m_bHasPendingVideoFrame = true;
        }

        if (m_Player.HasDisplayedFrame())
        {
            const auto t = m_Player.GetMasterClock();
            if (m_VideoFrame.pts > t + 0.02)
                break;
            if (m_VideoFrame.pts < t - 0.02)
            {
                m_VideoFrame = {};
                m_bHasPendingVideoFrame = false;
                continue;
            }
        }

        m_Player.SetCurrentVideoFramePresentTimestamp(m_VideoFrame.pts);
        m_Video.SetVideoFrame(std::move(m_VideoFrame));
        m_VideoFrame = {};
        m_bHasPendingVideoFrame = false;
        RdInvalidate();
    }
}