#pragma once
#include "CAudioPlayer.h"
#include "CFfmpeg.h"

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
        float Yuv2Rgb[3][4]{};
    };
    struct VideoFrameContext
    {
        eck::CByteBuffer* pSoftwareBuffer{};
        PCVOID pPlaneY, pPlaneUv;
        UINT cbStrideY, cbStrideUv;
        BOOLEAN b10Bit;
    };
private:
    CFfmpeg m_Ffmpeg{};
    CAudioPlayer m_AudioPlayer{};
    eck::CWaitableObject m_ThreadAudio{};
    std::atomic_bool m_bStopAudio{};

    SwsContext* m_pSws{};

    void ThreadAudio() noexcept;

    void StopThreadAudio() noexcept;

    void ConvertSoftwareFrame(
        _In_ const AVFrame* pFrame,
        _Inout_ VideoFrameContext& Context) noexcept;
public:
    CPlayer() = default;
    ~CPlayer() noexcept;

    HRESULT Initialize() noexcept;

    int OpenFile(std::wstring_view svFile) noexcept;

    auto& GetFfmpeg() noexcept { return m_Ffmpeg; }

    BOOL PopVideoFrame(
        Eck_Out_buffer_ VideoFrame& Frame,
        _Inout_ VideoFrameContext& Context) noexcept;

    double GetMasterClock() noexcept;
};