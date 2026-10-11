#include "pch.h"
#include "CVePlayList.h"

constexpr float
CxyMargin = 8,
CyListItem = 18,
CxyBtnClose = 26;

LRESULT CVePlayList::OnEvent(UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept
{
    switch (uMsg)
    {
    case WM_NOTIFY:
    {
        //const auto* const pnm = (Dui::DUINMHDR*)lParam;
        //if (wParam == (WPARAM)&m_List)
        //    switch (pnm->uCode)
        //    {
        //    case Dui::TBLE_GETDISPINFO:
        //    {
        //        const auto p = (Dui::TBL_DISPINFO*)lParam;
        //        const auto& e = m_WndMain.m_vPlayList[p->idx];
        //        p->pszText = e.rsName.Data();
        //        p->cchText = e.rsName.Size();
        //    }
        //    break;
        //    }
    }
    break;

    case WM_PAINT:
    {
        Dui::PAINTINFO ps;
        BeginPaint(ps, wParam, lParam);
        GetDC()->FillRectangle(ps.rcClip, GetWindow().CcSetBrushColor(
            D2D1::ColorF{ D2D1::ColorF::Black, 0.5f }));
        EndPaint(ps);
    }
    return 0;

    case WM_SIZE:
    {
        Kw::Rect rc;
        rc.left = GetWidth() - CxyBtnClose - CxyMargin;
        rc.top = CxyMargin;
        rc.right = rc.left + CxyBtnClose;
        rc.bottom = rc.top + CxyBtnClose;
        m_BTClose.SetRect(rc);

        rc.left = CxyMargin;
        rc.top = rc.bottom + CxyMargin;
        rc.right = GetWidth() - CxyMargin;
        rc.bottom = GetHeight() - CxyMargin;
        m_List.SetRect(rc);
    }
    break;

    case WM_SETFONT:
        m_BTClose.SetTextFormat(GetTextFormat().Get());
        m_List.SetTextFormat(GetTextFormat().Get());
        break;

    case WM_CREATE:
    {
        m_BTClose.Create(L"关闭", Dui::DES_VISIBLE | Dui::DES_PARENT_COMP, 0,
            0, 0, 50, 50, this);

        m_List.Create(nullptr, Dui::DES_VISIBLE | Dui::DES_PARENT_COMP, 0,
            0, 0, 0, 0, this);
        //auto& SB = m_List.GetScrollBarV();
        //SB.SetStyle(SB.GetStyle() | Dui::DES_PARENT_COMP);
        //m_List.SetItemCount((int)m_WndMain.m_vPlayList.size());
    }
    break;
    }
    return __super::OnEvent(uMsg, wParam, lParam);
}