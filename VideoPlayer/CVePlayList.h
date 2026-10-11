#pragma once

class CVePlayList : public Dui::CElement
{
private:
    Dui::CButton m_BTClose{};
    Dui::CListView m_List{};
public:
    LRESULT OnEvent(UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept override;
};