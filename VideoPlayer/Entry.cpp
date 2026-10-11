#include "pch.h"

#include "CApplication.h"
#include "CWindowMain.h"

#include "eck\AutoLink.h"

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
    _In_ PWSTR pszCmdLine, _In_ int nCmdShow)
{
    _CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_LEAK_CHECK_DF);

    if (const auto hr = CoInitialize(nullptr); FAILED(hr))
    {
        EckDbgPrintFormatMessage(hr);
        eck::MessageDialog(
            eck::Format(L"CoInitialize failed!\r\nhr = %08X", hr).Data(),
            L"Error",
            MB_ICONERROR);
        return 0;
    }

    eck::INITPARAM ip{};
    ip.uD3DCreateFlags |= D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    UINT uErr;
    if (const auto r = eck::Initialize(hInstance, &ip, &uErr);
        r != eck::StartupStatus::Ok)
    {
        EckDbgPrintFormatMessage(uErr);
        eck::MessageDialog(
            eck::Format(L"Init failed!\r\nInitStatus = %d\r\nError code = %08X",
            (int)r, uErr).Data(),
            L"Error",
            MB_ICONERROR);
        return 0;
    }

    App = new CApplication{};
    CApplication::Initialize();

    const auto pWnd = new CWindowMain{};
    const auto hMon = eck::GetOwnerMonitor(nullptr);
    const auto iDpi = eck::GetMonitorDpi(hMon);
    auto size = SIZE{ 640, 480 };
    eck::DpiScale(size, iDpi);
    const auto pt = eck::CalculateCenterWindowPosition(nullptr, size.cx, size.cy, FALSE);
    pWnd->SetPresentMode(Dui::PresentMode::DCompositionSurface);
    pWnd->SetUserDpi(iDpi);
    pWnd->Create(L"示例Win32程序", WS_OVERLAPPEDWINDOW, 0,
        pt.x, pt.y, size.cx, size.cy, nullptr, 0);
    pWnd->Show(SW_SHOW);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!eck::PreTranslateMessage(msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    delete pWnd;
    delete App;
    eck::ThreadUninitialize();
    eck::Uninitialize();
#ifdef _DEBUG
    if (eck::g_pDxgiDebug)
        eck::g_pDxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
#endif
    CoUninitialize();
    return (int)msg.wParam;
}