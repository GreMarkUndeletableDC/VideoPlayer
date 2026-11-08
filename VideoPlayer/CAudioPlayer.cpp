#include "pch.h"
#include "CAudioPlayer.h"

HRESULT CAudioPlayer::Initialize(UINT nSampleRate, UINT cChannel) noexcept
{
    Shutdown();
    m_bExit.store(false, std::memory_order_release);

    LARGE_INTEGER Dummy;
    NtQueryPerformanceCounter(&Dummy, &m_QpcFreq);

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

    m_cChannel = cChannel;
    m_nSampleRate = nSampleRate;

    WAVEFORMATEX* pwfx;
    hr = m_pClient->GetMixFormat(&pwfx);
    if (FAILED(hr))
        return hr;

    pwfx->nSamplesPerSec = nSampleRate;
    pwfx->nChannels = cChannel;
    pwfx->nBlockAlign = (WORD)(pwfx->nChannels * pwfx->wBitsPerSample / 8);
    pwfx->nAvgBytesPerSec = pwfx->nSamplesPerSec * pwfx->nBlockAlign;
    pwfx->wFormatTag = WAVE_FORMAT_EXTENSIBLE;

    constexpr DWORD Flags =
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY |
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK
        ;
    hr = m_pClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        Flags,
        1000 * 10000/* 1秒 */,
        0,
        pwfx,
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
    m_cAudioBuffer = 0;
    m_cChannel = 0;
    m_nSampleRate = 0;
    {
        const eck::CSrwWriteGuard _{ m_Lock };
        m_bAnchorValid = false;
        m_ptsAnchor = 0.;
        m_QpcAnchor.QuadPart = 0;
    }
}

void CAudioPlayer::Seek(double pts) noexcept
{
    LARGE_INTEGER Qpc;
    NtQueryPerformanceCounter(&Qpc, nullptr);
    {
        const eck::CSrwWriteGuard _{ m_Lock };
        m_ptsAnchor = pts;
        m_QpcAnchor = Qpc;
        m_bAnchorValid = true;
    }
    m_bSeek.store(true, std::memory_order_release);
    m_Event.Signal();
}

void CAudioPlayer::Reset() noexcept
{
    m_pClient->Stop();
    m_pClient->Reset();
    m_pClient->Start();
}

std::span<float> CAudioPlayer::GetBuffer(UINT cRequested) noexcept
{
    if (!m_pClient || !m_pRenderClient)
        return {};

    if (cRequested > m_cAudioBuffer)
        cRequested = m_cAudioBuffer;

    EckLoop()
    {
        eck::WaitObject(m_Event);
        if (m_bExit.load(std::memory_order_acquire) ||
            m_bSeek.load(std::memory_order_acquire))
            return {};

        UINT32 cPadding{};
        if (FAILED(m_pClient->GetCurrentPadding(&cPadding)))
            return {};
        if (cRequested <= m_cAudioBuffer - cPadding)
            break;
    }

    BYTE* pBuffer;
    if (FAILED(m_pRenderClient->GetBuffer(cRequested, &pBuffer)))
        return {};
    return { (float*)pBuffer, (size_t)cRequested * m_cChannel };
}

void CAudioPlayer::ReleaseBuffer(UINT cWritten) noexcept
{
    m_pRenderClient->ReleaseBuffer(cWritten, 0);
}

void CAudioPlayer::SetCurrentPresentTimestamp(double pts) noexcept
{
    if (m_pClient && m_nSampleRate)
    {
        UINT32 cPadding{};
        if (SUCCEEDED(m_pClient->GetCurrentPadding(&cPadding)))
            pts -= double(cPadding) / double(m_nSampleRate);
    }

    LARGE_INTEGER Qpc;
    NtQueryPerformanceCounter(&Qpc, nullptr);

    const eck::CSrwWriteGuard _{ m_Lock };
    if (m_bAnchorValid)
    {
        pts = m_ptsAnchor +
            double(Qpc.QuadPart - m_QpcAnchor.QuadPart) / m_QpcFreq.QuadPart;
    }
    m_ptsAnchor = pts;
    m_QpcAnchor = Qpc;
    m_bAnchorValid = true;
}

double CAudioPlayer::GetCurrentTime() noexcept
{
    LARGE_INTEGER Qpc;
    NtQueryPerformanceCounter(&Qpc, nullptr);

    const eck::CSrwReadGuard _{ m_Lock };
    if (!m_bAnchorValid)
        return -1.;
    return m_ptsAnchor +
        double(Qpc.QuadPart - m_QpcAnchor.QuadPart) / m_QpcFreq.QuadPart;
}