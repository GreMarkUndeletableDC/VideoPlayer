#include "pch.h"
#include "CAudioPlayer.h"

static EckInlineNdCe bool IsDeviceLost(HRESULT hr) noexcept
{
    return
        hr == AUDCLNT_E_DEVICE_INVALIDATED ||
        hr == AUDCLNT_E_SERVICE_NOT_RUNNING ||
        hr == AUDCLNT_E_RESOURCES_INVALIDATED;
}

HRESULT CAudioPlayer::Initialize() noexcept
{
    Shutdown();
    m_bExit.store(false, std::memory_order_release);

    HRESULT hr;
    hr = m_pDeviceEnumerator.CreateInstance(CLSID_MMDeviceEnumerator);
    if (FAILED(hr))
        return hr;

    hr = m_pDeviceEnumerator->GetDefaultAudioEndpoint(
        eRender, eMultimedia, &m_pDevice);
    if (FAILED(hr))
        return hr;

    hr = m_pDevice->Activate(
        IID_IAudioClient,
        CLSCTX_ALL,
        nullptr,
        (void**)&m_pClient);
    if (FAILED(hr))
        return hr;

    WAVEFORMATEX* pwfx;
    hr = m_pClient->GetMixFormat(&pwfx);
    if (FAILED(hr))
        return hr;

    DWORD uFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    AoFormat Fmt;
    AoMakeFormat(pwfx, Fmt);
    if (Fmt.IsValid())
    {
        m_wfx = {};
        memcpy(&m_wfx, pwfx, sizeof(WAVEFORMATEX) + pwfx->cbSize);
    }
    else
    {
        AoMakeFloatWaveFormat(pwfx, m_wfx);
        uFlags |= (
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY);
    }

    eck::CScopeGuard ExitGuard{ [this]() noexcept { Shutdown(); } };

    hr = m_pClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        uFlags,
        1000 * 10000/* 1秒 */,
        0,
        &m_wfx.Format,
        nullptr);
    CoTaskMemFree(pwfx);
    if (FAILED(hr))
        return hr;

    m_Event.NoSignal();
    hr = m_pClient->SetEventHandle(m_Event.Get());
    if (FAILED(hr))
        return hr;

    hr = m_pClient->GetBufferSize(&m_cAudioBuffer);
    if (FAILED(hr))
        return hr;

    hr = m_pClient->GetService(
        IID_IAudioRenderClient,
        (void**)&m_pRenderClient);
    if (FAILED(hr))
        return hr;

    hr = m_pClient->Start();
    if (FAILED(hr))
        return hr;
    ExitGuard.Cancel();
    return S_OK;
}

void CAudioPlayer::RequestShutdown() noexcept
{
    m_bExit.store(true, std::memory_order_release);
    m_Event.Signal();
    if (m_pClient)
        m_pClient->Stop();
}

void CAudioPlayer::Shutdown() noexcept
{
    RequestShutdown();
    if (m_pClient)
    {
        m_pClient->Reset();
        m_pClient.Clear();
    }
    m_pRenderClient.Clear();
    m_pDevice.Clear();
    m_pDeviceEnumerator.Clear();
    m_wfx = {};
    m_cAudioBuffer = 0;
    m_bSeek.store(false, std::memory_order_release);
}

void CAudioPlayer::RequestSeek() noexcept
{
    m_bSeek.store(true, std::memory_order_release);
    m_Event.Signal();
}

CAudioPlayer::WriteState CAudioPlayer::Write(
    PCVOID pData,
    UINT cFrames,
    _Out_ UINT& cWritten) noexcept
{
    cWritten = 0;
    if (!m_pClient || !m_pRenderClient)
        return WriteState::Exit;

    HRESULT hr;
    EckLoop()
    {
        eck::WaitObject(m_Event);

        if (m_bExit.load(std::memory_order_acquire))
            return WriteState::Exit;
        if (m_bSeek.exchange(false, std::memory_order_acq_rel))
        {
            m_pClient->Stop();
            m_pClient->Reset();
            m_pClient->Start();
            return WriteState::Seek;
        }

        UINT32 cPadding{};
        hr = m_pClient->GetCurrentPadding(&cPadding);
        if (IsDeviceLost(hr))
            return WriteState::DeviceLost;
        if (FAILED(hr))
            return WriteState::Exit;

        const UINT cFree = (cPadding < m_cAudioBuffer) ? m_cAudioBuffer - cPadding : 0;
        if (!cFree)
            continue;

        const UINT cChunk = std::min(cFrames - cWritten, cFree);
        BYTE* pBuffer;
        hr = m_pRenderClient->GetBuffer(cChunk, &pBuffer);
        if (IsDeviceLost(hr))
            return WriteState::DeviceLost;
        if (FAILED(hr))
            return WriteState::Exit;

        memcpy(
            pBuffer,
            (PCBYTE)pData + cWritten * m_wfx.Format.nBlockAlign,
            cChunk * m_wfx.Format.nBlockAlign);

        hr = m_pRenderClient->ReleaseBuffer(cChunk, 0);
        if (IsDeviceLost(hr))
            return WriteState::DeviceLost;
        if (FAILED(hr))
            return WriteState::Exit;

        cWritten += cChunk;
        if (cWritten == cFrames)
            return WriteState::Ok;
    }
}

UINT CAudioPlayer::GetCurrentPadding() noexcept
{
    UINT32 cPadding{};
    m_pClient->GetCurrentPadding(&cPadding);
    return cPadding;
}