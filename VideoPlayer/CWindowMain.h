#pragma once
#include "CApplication.h"
#include "CPlayer.h"
#include "CVeVideo.h"
#include "CVePlayList.h"

class CWindowMain : public Dui::CDuiWindow, public eck::ITimeLine
{
private:
    ComPtr<IDWriteTextFormat> m_pTextFormat{};

    CPlayer m_Player{};
    CPlayer::VideoFrame m_PendingVideoFrame{};
    CPlayer::VideoFrameContext m_VideoContext{};

    CVeVideo m_Video{};
    Dui::CTrackBar m_TB{};
    CVePlayList m_ListContainer{};

    BITBOOL m_bHasPendingVideoFrame : 1{};

    int m_msCurrentInterval{};


    LRESULT OnCreate();
public:
    LRESULT OnMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept override;

    LRESULT OnRenderEvent(UINT uMsg, Dui::RENDER_EVENT& e) noexcept override;

    LRESULT OnElementNotify(Dui::CElement* pEle, Dui::ELENMHDR* pnm) noexcept override;

    void TlTick(int ms) noexcept override;
    BOOL TlIsValid() noexcept override { return TRUE; }
    int TlGetCurrentInterval() const noexcept override { return m_msCurrentInterval; }
};