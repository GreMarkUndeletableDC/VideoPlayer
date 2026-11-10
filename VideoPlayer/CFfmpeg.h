#pragma once
#include "FfPtr.h"
#include "CRingBuffer.h"
#include "AudioFormat.h"

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
    static constexpr size_t QueuePacketSize = 72;
    static constexpr size_t QueueVideoSize = 10;
    static constexpr size_t QueueAudioSize = 64;

    FfPtr<AVFormatContext> m_pFmtCtx{};
    int m_idxStreamVideo{ -1 };
    int m_idxStreamAudio{ -1 };

    TSecond m_ptsStart{};

    FfPtr<AVCodecContext> m_pVideoCodecCtx{};
    FfPtr<AVCodecContext> m_pAudioCodecCtx{};

    FfRefPtr<AVBufferRef> m_pHwDeviceCtx{};

    ComPtr<ID3D11Device> m_pD3DDevice{};

    eck::CWaitableObject m_ThreadVideo{};
    eck::CWaitableObject m_ThreadAudio{};
    eck::CWaitableObject m_ThreadDemux{};

    std::atomic<double> m_ptsSeek{};// 负值表示无效

    eck::CCriticalSection m_Lock{};

    std::atomic<bool> m_bStopDecode{};
    std::atomic<TSerial> m_nSeekSerial{};

    std::atomic<TSerial> m_nFormatSerial{};
    AoFormat m_OutFormat{};

    SwrContext* m_pSwr{};
    int m_nSwrInRate{};
    AVSampleFormat m_eSwrInFormat{ AV_SAMPLE_FMT_NONE };
    AVChannelLayout m_SwrInLayout{};
    AVChannelLayout m_SwrOutLayout{};

    CRingBuffer<FfPtr<AVPacket>> m_qVideo{ QueuePacketSize };
    CRingBuffer<FfPtr<AVPacket>> m_qAudio{ QueuePacketSize };
    CRingBuffer<FfPtr<AVFrame>> m_qVideoDecoded{ QueueVideoSize };
    CRingBuffer<FfPtr<AVFrame>> m_qAudioDecoded{ QueueAudioSize };

    void SwrFree() noexcept;
    // swr创建成功时返回有效的AoFormat
    AoFormat SwrEnsureValid(_In_ const AVFrame* pFrame) noexcept;
    FfPtr<AVFrame> SwrConvertAudio(_In_ const AVFrame* pFrame) noexcept;

    void ThreadVideoDecode() noexcept;
    void ThreadAudioDecode() noexcept;
    void ThreadDemux() noexcept;

    Result PrepareVideoCodec() noexcept;
    Result PrepareAudioCodec() noexcept;

    void StopDecodeThread() noexcept;
public:
    void InitializeDirect3D(ID3D11Device* pDevice) noexcept;

    void SwrSetAoFormat(const AoFormat& Format) noexcept;

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

    double GetPresentTimestamp(_In_ const AVFrame* pFrame, int idxStream) const noexcept;
    double GetAudioPresentTimestamp(_In_ const AVFrame* pFrame) const noexcept;
    double GetVideoPresentTimestamp(_In_ const AVFrame* pFrame) const noexcept;

    EckInlineNdCe int GetVideoStream() const noexcept { return m_idxStreamVideo; }
    EckInlineNdCe int GetAudioStream() const noexcept { return m_idxStreamAudio; }

    USHORT GetSeekSerial() const noexcept
    {
        return m_nSeekSerial.load(std::memory_order_acquire);
    }
    TSerial GetFormatSerial() const noexcept
    {
        return m_nFormatSerial.load(std::memory_order_acquire);
    }

    void Seek(double s) noexcept;

    double GetDuration() noexcept;
};