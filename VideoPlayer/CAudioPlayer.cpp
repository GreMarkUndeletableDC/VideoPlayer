#include "pch.h"
#include "CAudioPlayer.h"

HRESULT CAudioPlayer::Initialize(UINT uSampleRate, UINT cChannel) noexcept
{
    HRESULT hr;
    hr = m_pDeviceEnumerator.CreateInstance(CLSID_MMDeviceEnumerator);
    if (FAILED(hr))
        return hr;

    hr = m_pDeviceEnumerator->GetDefaultAudioEndpoint(
        eRender, eMultimedia, m_pDevice.AtClear());
    if (FAILED(hr))
        return hr;

    hr = m_pDevice->Activate(
        IID_IAudioClient,
        CLSCTX_ALL,
        nullptr,
        (void**)m_pClient.AtClear());
    if (FAILED(hr))
        return hr;

    m_cChannel = cChannel;

    WAVEFORMATEX* pwfx;
    hr = m_pClient->GetMixFormat(&pwfx);
    if (FAILED(hr))
        return hr;

    pwfx->nSamplesPerSec = uSampleRate;
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
    if (FAILED(hr))
    {
        CoTaskMemFree(pwfx);
        return hr;
    }

    m_Event.NoSignal();
    hr = m_pClient->SetEventHandle(m_Event.Get());
    if (FAILED(hr))
        return hr;

    hr = m_pClient->GetBufferSize(&m_cAudioBuffer);
    if (FAILED(hr))
        return hr;

    hr = m_pClient->GetService(
        IID_IAudioRenderClient,
        (void**)m_pRenderClient.AtClear());
    if (FAILED(hr))
        return hr;

    hr = m_pClient->GetService(
        IID_IAudioClock,
        (void**)m_pClock.AtClear());
    if (FAILED(hr))
    {
        CoTaskMemFree(pwfx);
        return hr;
    }
    hr = m_pClock->GetFrequency(&m_ullFrequency);

    CoTaskMemFree(pwfx);
    return hr;
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

double CAudioPlayer::GetCurrentTime() noexcept
{
    if (!m_pClock || !m_ullFrequency)
        return 0.;
    UINT64 ullPosition;
    if (FAILED(m_pClock->GetPosition(&ullPosition, nullptr)))
        return 0.;
    return (double)ullPosition / (double)m_ullFrequency;
}

void CAudioPlayer::Reset() noexcept
{
    m_Event.NoSignal();
    m_pClient->Stop();
    m_pClient->Reset();
    m_pClient->Start();
}