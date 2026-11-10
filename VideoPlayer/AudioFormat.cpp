#include "pch.h"
#include "AudioFormat.h"

DWORD AoDefaultChannelMask(UINT cChannel) noexcept
{
    switch (cChannel)
    {
    case 1:
        return SPEAKER_FRONT_CENTER;
    case 2:
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    case 3:
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER;
    case 4:
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
    case 5:// 5.0
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
            SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
    case 6:// 5.1
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
            SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
    case 7:// 6.1
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
            SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_BACK_CENTER;
    case 8:// 7.1
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
            SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT |
            SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT;
    default:
        return 0;
    }
}

AVSampleFormat AoWaveFormatToAvSampleFormat(_In_ const WAVEFORMATEX* pwfx) noexcept
{
    if (!pwfx->nChannels || !pwfx->wBitsPerSample || !pwfx->nSamplesPerSec)
        return AV_SAMPLE_FMT_NONE;
    // 非紧密交错
    if (pwfx->nBlockAlign != pwfx->nChannels * pwfx->wBitsPerSample / 8)
        return AV_SAMPLE_FMT_NONE;

    bool bPcm, bFloat;
    if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        // 判定为非常规格式
        if (pwfx->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
            return AV_SAMPLE_FMT_NONE;
        const auto pExt = (const WAVEFORMATEXTENSIBLE*)pwfx;
        bPcm = (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_PCM);
        bFloat = (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    else
    {
        bPcm = pwfx->wFormatTag == WAVE_FORMAT_PCM;
        bFloat = pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    }

    if (bPcm)
        switch (pwfx->wBitsPerSample)
        {
        case 8:  return AV_SAMPLE_FMT_U8;
        case 16: return AV_SAMPLE_FMT_S16;
        case 32: return AV_SAMPLE_FMT_S32;
        default: return AV_SAMPLE_FMT_NONE;
        }
    if (bFloat)
        switch (pwfx->wBitsPerSample)
        {
        case 32: return AV_SAMPLE_FMT_FLT;
        case 64: return AV_SAMPLE_FMT_DBL;
        default: return AV_SAMPLE_FMT_NONE;
        }
    return AV_SAMPLE_FMT_NONE;
}

void AoMakeFormat(
    _In_opt_ const WAVEFORMATEX* pwfx,
    _Out_ AoFormat& Fmt) noexcept
{
    Fmt = {};
    if (!pwfx ||
        sizeof(WAVEFORMATEX) + pwfx->cbSize > sizeof(WAVEFORMATEXTENSIBLE))
        return;
    const auto eSampleFormat = AoWaveFormatToAvSampleFormat(pwfx);
    if (eSampleFormat == AV_SAMPLE_FMT_NONE)
        return;
    Fmt.nSampleRate = pwfx->nSamplesPerSec;
    Fmt.cChannel = pwfx->nChannels;
    Fmt.eSampleFormat = eSampleFormat;
    if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        Fmt.dwChannelMask = ((const WAVEFORMATEXTENSIBLE*)pwfx)->dwChannelMask;
}

void AoMakeFloatWaveFormat(
    _In_ const WAVEFORMATEX* pSrc,
    _Out_ WAVEFORMATEXTENSIBLE& wfx) noexcept
{
    wfx = {};
    wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels = pSrc->nChannels;
    wfx.Format.nSamplesPerSec = pSrc->nSamplesPerSec;
    wfx.Format.wBitsPerSample = 32;
    wfx.Format.nBlockAlign = WORD(pSrc->nChannels * 4);
    wfx.Format.nAvgBytesPerSec = pSrc->nSamplesPerSec * wfx.Format.nBlockAlign;
    wfx.Format.cbSize = WORD(sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX));
    wfx.Samples.wValidBitsPerSample = 32;
    wfx.dwChannelMask =
        (pSrc->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            pSrc->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) ?
        ((const WAVEFORMATEXTENSIBLE*)pSrc)->dwChannelMask :
        AoDefaultChannelMask(pSrc->nChannels);
    wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
}