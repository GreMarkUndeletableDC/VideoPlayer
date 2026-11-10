#pragma once
#include "CAudioPlayer.h"
#include "CFfmpeg.h"
#include "CClock.h"

class CPlayer final
{
public:
    struct VideoFrame
    {
        FfPtr<AVFrame> pFrame{};

        ComPtr<ID3D11Texture2D> pTexture{};
        UINT idxSubResource{};
        double pts{};
        UINT cx{};
        UINT cy{};
        float YuvToRgb[3][4]{};

        PCVOID pPlaneY, pPlaneUv;
        UINT cbStrideY, cbStrideUv;
        bool b10Bit;
    };
    struct VideoFrameContext
    {
        eck::CByteBuffer* pSoftwareBuffer{};
    };
private:
    CFfmpeg m_Ffmpeg{};

    CAudioPlayer m_AudioPlayer{};
    eck::CWaitableObject m_ThreadAudio{};
    std::atomic<bool> m_bStopAudio{};

    CClock m_ClockVideo{};
    CClock m_ClockAudio{};

    SwsContext* m_pSws{};

    ComPtr<IWICBitmapSource> m_pCoverBitmap{};

    void ThreadAudio() noexcept;

    bool ReopenAudioDevice() noexcept;

    void StopThreadAudio() noexcept;

    void ConvertSoftwareFrame(
        _In_ const AVFrame* pFrame,
        _Inout_ VideoFrame& Frame,
        const VideoFrameContext& Context) noexcept;

    void ReadAudioCover(_In_z_ PCWSTR pszFile) noexcept;
public:
    CPlayer() = default;
    ~CPlayer() noexcept;

    HRESULT Initialize() noexcept;

    int OpenFile(std::wstring_view svFile) noexcept;

    bool VideoPopFrame(
        Eck_Out_buffer_ VideoFrame& Frame,
        _Inout_ VideoFrameContext& Context) noexcept;

    double VideoGetMasterClock() noexcept;

    void VideoAnchorClock(double pts) noexcept;

    USHORT GetSeekSerial() const noexcept
    {
        return m_Ffmpeg.GetSeekSerial();
    }

    void Seek(double s) noexcept;

    double GetPosition() noexcept
    {
        return VideoGetMasterClock();
    }
    double GetDuration() noexcept
    {
        return m_Ffmpeg.GetDuration();
    }

    bool IsOnlyAudio() const noexcept
    {
        return m_Ffmpeg.GetVideoStream() < 0 && m_Ffmpeg.GetAudioStream() >= 0;
    }
    EckInlineNdCe auto& GetCoverBitmap() noexcept { return m_pCoverBitmap; }
};