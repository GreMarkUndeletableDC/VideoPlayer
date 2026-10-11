#pragma once
#include "FfPtr.h"

class CFfmpeg
{
public:
    enum class ErrorType
    {
        Ok,
        FormatContext,
        OpenInput,
        StreamInformation,
        FindStream,
        VideoCodec,
        AudioCodec,
        HardwareDevice,
    };
    struct Result
    {
        ErrorType eType;
        int nCode;
    };
private:
    FfPtr<AVFormatContext> m_pFmtCtx{};
    int m_idxStreamVideo{ -1 };
    int m_idxStreamAudio{ -1 };

    FfPtr<AVCodecContext> m_pVideoCodecCtx{};
    FfPtr<AVCodecContext> m_pAudioCodecCtx{};

    FfRefPtr<AVBufferRef> m_pHwDeviceCtx{};

    ComPtr<ID3D11Device> m_pD3DDevice{};

    eck::CWaitableObject m_ThreadVideo{};
    eck::CWaitableObject m_ThreadAudio{};
    eck::CWaitableObject m_ThreadDemux{};

    std::atomic_bool m_bStopDecode{};
    eck::CCriticalSection m_Lock{};

    CRingBuffer<FfPtr<AVPacket>> m_qVideo{ 64 };
    CRingBuffer<FfPtr<AVPacket>> m_qAudio{ 64 };
    CRingBuffer<FfPtr<AVFrame>> m_qVideoDecoded{ 12 };
    CRingBuffer<FfPtr<AVFrame>> m_qAudioDecoded{ 64 };
public:
    static BOOL WriteAudioFrame(
        _In_ const AVFrame* pFrame,
        UINT32 cChannel,
        _Out_writes_(cChannel * pFrame->nb_samples) float* pBuf) noexcept;

    void ThreadVideoDecode() noexcept;
    void ThreadAudioDecode() noexcept;
    void ThreadDemux() noexcept;

    Result PrepareVideoCodec() noexcept;
    Result PrepareAudioCodec() noexcept;

    void StopDecodeThread() noexcept;
public:
    void InitializeDirect3D(ID3D11Device* pDevice) noexcept
    {
        m_pD3DDevice = pDevice;
    }

    Result OpenFile(_In_z_ PCSTR pszPathU8) noexcept;

    void Stop() noexcept;

    BOOL TryPopVideoDecoded(FfPtr<AVFrame>& pFrame) noexcept
    {
        return m_qVideoDecoded.TryPop(pFrame);
    }
    BOOL PopAudioDecoded(FfPtr<AVFrame>& pFrame) noexcept
    {
        return m_qAudioDecoded.Pop(pFrame);
    }

    double GetPresentTimestamp(const AVFrame* pFrame) noexcept;

    EckInlineNdCe BOOL HasVideoStream() const noexcept { return m_idxStreamVideo >= 0; }
    EckInlineNdCe BOOL HasAudioStream() const noexcept { return m_idxStreamAudio >= 0; }

    void GetAudioFormat(_Out_ UINT& nSampleRate, _Out_ UINT& cChannel) noexcept;
};