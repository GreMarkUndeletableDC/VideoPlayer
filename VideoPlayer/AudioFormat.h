#pragma once
struct AoFormat
{
    UINT nSampleRate;
    UINT cChannel;
    DWORD dwChannelMask;
    AVSampleFormat eSampleFormat;

    EckInlineNdCe bool IsValid() const noexcept { return !!nSampleRate; }
};

DWORD AoDefaultChannelMask(UINT cChannel) noexcept;

// swr不适用时返回AV_SAMPLE_FMT_NONE，以下情况不适用
//   1. PCM位宽非8、16、32 或 IEEE Float位宽非32、64
//   2. 非PCM且非IEEE Float
//   3. 非紧密交错
//   4. 非常规格式
AVSampleFormat AoWaveFormatToAvSampleFormat(_In_ const WAVEFORMATEX* pwfx) noexcept;

void AoMakeFormat(
    _In_opt_ const WAVEFORMATEX* pwfx,
    _Out_ AoFormat& Fmt) noexcept;

void AoMakeFloatWaveFormat(
    _In_ const WAVEFORMATEX* pSrc,
    _Out_ WAVEFORMATEXTENSIBLE& wfx) noexcept;