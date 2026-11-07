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
    CPlayer::VideoFrame m_VideoFrame{};
    CPlayer::VideoFrameContext m_VideoContext{};

    CVeVideo m_Video{};
    Dui::CTrackBar m_TB{};
    CVePlayList m_ListContainer{};

    BITBOOL m_bHasPendingVideoFrame : 1{};

    int m_msCurrentInterval{};


    LRESULT OnCreate();

    void OpenFile(PCWSTR pszFile) noexcept;
public:
    LRESULT OnMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept override;

    LRESULT OnRenderEvent(UINT uMsg, Dui::RENDER_EVENT& e) noexcept override;

    LRESULT OnElementNotify(Dui::CElement* pEle, Dui::ELENMHDR* pnm) noexcept override;

    void TlTick(int ms) noexcept override;
    bool TlIsValid() noexcept override { return true; }
    int TlGetCurrentInterval() const noexcept override { return m_msCurrentInterval; }
};