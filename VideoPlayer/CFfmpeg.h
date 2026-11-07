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
    static constexpr size_t QueuePacketSize = 64;
    static constexpr size_t QueueVideoSize = 10;
    static constexpr size_t QueueAudioSize = 256;

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

    CRingBuffer<FfPtr<AVPacket>> m_qVideo{ QueuePacketSize };
    CRingBuffer<FfPtr<AVPacket>> m_qAudio{ QueuePacketSize };
    CRingBuffer<FfPtr<AVFrame>> m_qVideoDecoded{ QueueVideoSize };
    CRingBuffer<FfPtr<AVFrame>> m_qAudioDecoded{ QueueAudioSize };
public:
    static bool WriteAudioFrame(
        _In_ const AVFrame* pFrame,
        UINT32 cChannel,
        _Out_writes_(cChannel* pFrame->nb_samples) float* pBuf) noexcept;

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

    bool TryPopVideoDecoded(FfPtr<AVFrame>& pFrame) noexcept
    {
        return m_qVideoDecoded.TryPop(pFrame);
    }
    bool PopAudioDecoded(FfPtr<AVFrame>& pFrame) noexcept
    {
        return m_qAudioDecoded.Pop(pFrame);
    }

    double GetPresentTimestamp(
        _In_ const AVFrame* pFrame,
        int idxStream) noexcept;

    EckInlineNdCe int GetVideoStream() const noexcept { return m_idxStreamVideo; }
    EckInlineNdCe int GetAudioStream() const noexcept { return m_idxStreamAudio; }

    void GetAudioFormat(
        _Out_ UINT& nSampleRate,
        _Out_ UINT& cChannel) noexcept;
};