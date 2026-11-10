#include "pch.h"
#include "CPlayer.h"

CPlayer::~CPlayer() noexcept
{
    m_Ffmpeg.Stop();
    StopThreadAudio();
    if (m_pSws)
        sws_freeContext(m_pSws);
}

void CPlayer::ThreadAudio() noexcept
{
    FfPtr<AVFrame> pFrame{};
    while (!m_bStopAudio.load(std::memory_order_acquire))
    {
        if (!pFrame)
        {
            if (!m_Ffmpeg.PopAudioDecoded(pFrame))
                break;
        }
        // 高16位格式序列号，低16位Seek序列号
        const auto dwOpaque = eck::PointerToDword<DWORD>(pFrame->opaque);
        const auto nSerial = TSerial(dwOpaque & 0xFFFF);
        if (TSerial(dwOpaque >> 16) != m_Ffmpeg.GetFormatSerial() ||
            nSerial != m_Ffmpeg.GetSeekSerial())
        {
            pFrame.Clear();
            continue;
        }
        if (pFrame->nb_samples <= 0)
        {
            pFrame.Clear();
            continue;
        }

        UINT cWritten{};
        switch (m_AudioPlayer.Write(pFrame->extended_data[0],
            (UINT)pFrame->nb_samples, cWritten))
        {
        case CAudioPlayer::WriteState::Seek:
            pFrame.Clear();
            continue;
        case CAudioPlayer::WriteState::DeviceLost:
            m_ClockAudio.Invalidate();
            pFrame.Clear();
            if (!ReopenAudioDevice())
                return;
            continue;
        case CAudioPlayer::WriteState::Exit:
            return;
        case CAudioPlayer::WriteState::Ok:
            break;
        }

        if (nSerial == m_Ffmpeg.GetSeekSerial())
            m_ClockAudio.SetForward(
                m_Ffmpeg.GetAudioPresentTimestamp(pFrame.Get()) +
                ((double)cWritten - (double)m_AudioPlayer.GetCurrentPadding()) /
                m_AudioPlayer.GetWaveFormat().nSamplesPerSec);

        pFrame.Clear();
    }
}

bool CPlayer::ReopenAudioDevice() noexcept
{
    while (!m_bStopAudio.load(std::memory_order_acquire))
    {
        if (SUCCEEDED(m_AudioPlayer.Initialize()))
        {
            AoFormat Fmt;
            AoMakeFormat(&m_AudioPlayer.GetWaveFormat(), Fmt);
            m_Ffmpeg.SwrSetAoFormat(Fmt);
            return true;
        }
        LARGE_INTEGER Delay{ .QuadPart = -200'0000 };
        NtDelayExecution(false, &Delay);
    }
    return false;
}

void CPlayer::StopThreadAudio() noexcept
{
    m_bStopAudio.store(true, std::memory_order_release);
    m_ClockAudio.Invalidate();
    m_AudioPlayer.RequestShutdown();
    if (m_ThreadAudio.Get())
    {
        eck::WaitObject(m_ThreadAudio);
        m_ThreadAudio.Clear();
    }
    m_AudioPlayer.Shutdown();
}

static void CalculateYuvToRgbMatrix(
    _In_ const AVFrame* pFrame,
    bool b10Bit,
    _Out_ float pMatrix[3][4]) noexcept
{
    const AVLumaCoefficients* pCoeffs =
        av_csp_luma_coeffs_from_avcsp(pFrame->colorspace);
    if (!pCoeffs)
        pCoeffs = av_csp_luma_coeffs_from_avcsp(
            (pFrame->height > 576) ?
            AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M);

    double KR = 0.2126, KG = 0.7152, KB = 0.0722;// 默认BT.709
    if (pCoeffs)
    {
        KR = av_q2d(pCoeffs->cr);
        KG = av_q2d(pCoeffs->cg);
        KB = av_q2d(pCoeffs->cb);
    }

    const auto Max = b10Bit ? 1023. : 255.;
    double yOff, cOff, yScale, cScale;
    if (pFrame->color_range == AVCOL_RANGE_JPEG)
    {
        yOff = 0.;
        cOff = (b10Bit ? 512. : 128.) / Max;
        yScale = cScale = 1.;
    }
    else
    {
        yOff = (b10Bit ? 64. : 16.) / Max;
        cOff = (b10Bit ? 512. : 128.) / Max;
        yScale = Max / (b10Bit ? 876. : 219.);
        cScale = Max / (b10Bit ? 896. : 224.);
    }

    const double rCr = cScale * 2. * (1. - KR);
    const double gCb = cScale * 2. * (1. - KB) * KB / KG;
    const double gCr = cScale * 2. * (1. - KR) * KR / KG;
    const double bCb = cScale * 2. * (1. - KB);

    pMatrix[0][0] = (float)yScale;
    pMatrix[0][1] = 0.f;
    pMatrix[0][2] = (float)rCr;
    pMatrix[0][3] = (float)-(yScale * yOff + rCr * cOff);

    pMatrix[1][0] = (float)yScale;
    pMatrix[1][1] = (float)-gCb;
    pMatrix[1][2] = (float)-gCr;
    pMatrix[1][3] = (float)((gCb + gCr) * cOff - yScale * yOff);

    pMatrix[2][0] = (float)yScale;
    pMatrix[2][1] = (float)bCb;
    pMatrix[2][2] = 0.f;
    pMatrix[2][3] = (float)-(yScale * yOff + bCb * cOff);
}

void CPlayer::ConvertSoftwareFrame(
    _In_ const AVFrame* pFrame,
    _Inout_ VideoFrame& Frame,
    const VideoFrameContext& Context) noexcept
{
    int r;
    Frame.pPlaneY = nullptr;
    Frame.cbStrideY = 0;
    Frame.pPlaneUv = nullptr;
    Frame.cbStrideUv = 0;
    Frame.b10Bit = false;

    const auto eFmtSource = (AVPixelFormat)pFrame->format;
    const auto* const pFmtDesc = av_pix_fmt_desc_get(eFmtSource);
    if (!pFmtDesc)
        return;
    auto eFmtDest = (pFmtDesc->comp[0].depth > 8) ?
        AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12;

    if (eFmtSource == eFmtDest)
    {
        Frame.pPlaneY = pFrame->data[0];
        Frame.cbStrideY = (UINT)pFrame->linesize[0];
        Frame.pPlaneUv = pFrame->data[1];
        Frame.cbStrideUv = (UINT)pFrame->linesize[1];
    }
    else
    {
        m_pSws = sws_getCachedContext(
            m_pSws,
            pFrame->width, pFrame->height, eFmtSource,
            pFrame->width, pFrame->height, eFmtDest,
            SWS_BILINEAR,
            nullptr, nullptr, nullptr);
        if (!m_pSws && eFmtDest == AV_PIX_FMT_P010LE)
        {
            eFmtDest = AV_PIX_FMT_NV12;
            m_pSws = sws_getCachedContext(
                m_pSws,
                pFrame->width, pFrame->height, eFmtSource,
                pFrame->width, pFrame->height, eFmtDest,
                SWS_BILINEAR,
                nullptr, nullptr, nullptr);
        }
        if (!m_pSws)
            return;
        const auto cbBuffer = av_image_get_buffer_size(
            eFmtDest, pFrame->width, pFrame->height, 1);
        if (cbBuffer <= 0)
            return;
        Context.pSoftwareBuffer->ReSize((size_t)cbBuffer);

        BYTE* pDstData[4]{};
        int cbDstStride[4]{};
        r = av_image_fill_arrays(
            pDstData, cbDstStride,
            Context.pSoftwareBuffer->Data(),
            eFmtDest,
            pFrame->width, pFrame->height,
            1);
        if (r < 0)
            return;

        r = sws_scale(
            m_pSws,
            pFrame->data, pFrame->linesize,
            0, pFrame->height,
            pDstData, cbDstStride);
        if (r < 0)
            return;

        Frame.pPlaneY = pDstData[0];
        Frame.cbStrideY = (UINT)cbDstStride[0];
        Frame.pPlaneUv = pDstData[1];
        Frame.cbStrideUv = (UINT)cbDstStride[1];
    }
    Frame.b10Bit = (eFmtDest == AV_PIX_FMT_P010LE);
}

void CPlayer::ReadAudioCover(_In_z_ PCWSTR pszFile) noexcept
{
    Tag::CMediaFile File{ pszFile };
    if (!File.IsValid())
        return;
    Tag::SimpleData mi{};
    mi.uMask = Tag::MIM_COVER;
    if (File.GetTagType() & (Tag::TAG_ID3V2_3 | Tag::TAG_ID3V2_4))
    {
        Tag::CID3v2 Tag{ File };
        Tag.ReadTag();
        Tag.SimpleGet(mi, {});
    }
    if (File.GetTagType() & Tag::TAG_APE)
    {
        Tag::CApe Tag{ File };
        Tag.ReadTag();
        Tag.SimpleGet(mi, {});
    }
    if (File.GetTagType() & Tag::TAG_FLAC)
    {
        Tag::CFlac Tag{ File };
        Tag.ReadTag();
        Tag.SimpleGet(mi, {});
    }

    const auto pCover = mi.GetMainCover();
    if (!pCover)
        return;

    ComPtr<IStream> pStream{};
    pCover->CreateStream(pStream.Self(), true);

    eck::WicLoadSource(m_pCoverBitmap.SelfClear(), pStream.Get());
}

HRESULT CPlayer::Initialize() noexcept
{
    m_ClockVideo.Invalidate();
    m_ClockAudio.Invalidate();

    m_Ffmpeg.InitializeDirect3D(eck::g_pD3D11Device);
    return S_OK;
}

int CPlayer::OpenFile(std::wstring_view svFile) noexcept
{
    eck::CStringA rsPathU8{};
    eck::EcdWideToUtf8(rsPathU8, svFile.data(), (int)svFile.size());

    m_Ffmpeg.Stop();
    StopThreadAudio();

    m_ClockVideo.Invalidate();
    m_ClockAudio.Invalidate();

    m_pCoverBitmap.Clear();

    const auto bAudioDevice = SUCCEEDED(m_AudioPlayer.Initialize());
    AoFormat Fmt{};
    if (bAudioDevice)
        AoMakeFormat(&m_AudioPlayer.GetWaveFormat(), Fmt);
    m_Ffmpeg.SwrSetAoFormat(Fmt);

    const auto r = m_Ffmpeg.OpenFile(rsPathU8.Data());
    if (r.eType != CFfmpeg::ErrorType::Ok)
    {
        if (bAudioDevice)
            m_AudioPlayer.Shutdown();
        return r.nCode;
    }

    if (m_Ffmpeg.GetAudioStream() >= 0 && bAudioDevice)
    {
        m_bStopAudio.store(false, std::memory_order_release);
        m_ThreadAudio.Attach(eck::CrtCreateThread(
            [](void* p) noexcept -> UINT
            {
                ((CPlayer*)p)->ThreadAudio();
                return 0;
            }, this));
        if (m_Ffmpeg.GetVideoStream() < 0)
            ReadAudioCover(svFile.data());
    }
    else if (bAudioDevice)
    {
        m_AudioPlayer.Shutdown();
        m_Ffmpeg.SwrSetAoFormat({});
    }

    return r.nCode;
}

bool CPlayer::VideoPopFrame(
    Eck_Out_buffer_ VideoFrame& Frame,
    _Inout_ VideoFrameContext& Context) noexcept
{
    FfPtr<AVFrame> pFrame{};
    while (m_Ffmpeg.TryPopVideoDecoded(pFrame))
    {
        if (eck::PointerToDword<USHORT>(pFrame->opaque) == m_Ffmpeg.GetSeekSerial())
            break;
        pFrame.Clear();
    }
    if (!pFrame)
        return false;

    if (pFrame->format == AV_PIX_FMT_D3D11)
    {
        Frame.pTexture = (ID3D11Texture2D*)pFrame->data[0];
        Frame.idxSubResource = eck::PointerToDword<UINT>(pFrame->data[1]);
        Frame.b10Bit = false;
        const auto* const pFrames =
            (const AVHWFramesContext*)pFrame->hw_frames_ctx->data;
        const auto* const pDesc = av_pix_fmt_desc_get(pFrames->sw_format);
        if (pDesc)
            Frame.b10Bit = (pDesc->comp[0].depth > 8);
    }
    else
    {
        Frame.pTexture.Clear();
        Frame.idxSubResource = 0;
        ConvertSoftwareFrame(pFrame.Get(), Frame, Context);
    }
    CalculateYuvToRgbMatrix(pFrame.Get(), Frame.b10Bit, Frame.YuvToRgb);

    Frame.pts = m_Ffmpeg.GetPresentTimestamp(pFrame.Get(), m_Ffmpeg.GetVideoStream());
    Frame.cx = (UINT)pFrame->width;
    Frame.cy = (UINT)pFrame->height;
    Frame.pFrame = std::move(pFrame);
    return true;
}

void CPlayer::VideoAnchorClock(double pts) noexcept
{
    m_ClockVideo.Set(pts);
}

void CPlayer::Seek(double s) noexcept
{
    m_Ffmpeg.Seek(s);
    m_ClockAudio.Invalidate();
    m_AudioPlayer.RequestSeek();
    VideoAnchorClock(s);
}

double CPlayer::VideoGetMasterClock() noexcept
{
    if (m_ClockAudio.IsValid())
        return m_ClockAudio.Get();
    if (m_ClockVideo.IsValid())
        return m_ClockVideo.Get();
    return CClock::None;
}