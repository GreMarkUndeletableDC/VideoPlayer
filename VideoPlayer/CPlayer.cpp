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
    m_AudioPlayer.Reset();
    while (!m_bStopAudio.load(std::memory_order_acquire))
    {
        FfPtr<AVFrame> pFrame{};
        if (!m_Ffmpeg.PopAudioDecoded(pFrame))
            break;

        const auto cChannel = m_AudioPlayer.GetChannelCount();
        const auto cSample = pFrame->nb_samples;
        if (cSample <= 0)
            continue;

        const auto spBuffer = m_AudioPlayer.GetBuffer(cSample);
        if (spBuffer.empty())
            break;

        if (!CFfmpeg::WriteAudioFrame(pFrame.Get(), cChannel, spBuffer.data()))
            RtlZeroMemory(spBuffer.data(), spBuffer.size() * sizeof(float));
        m_AudioPlayer.ReleaseBuffer(cSample);
    }
}

void CPlayer::StopThreadAudio() noexcept
{
    m_bStopAudio.store(true, std::memory_order_release);
    if (m_ThreadAudio.Get())
    {
        eck::WaitObject(m_ThreadAudio);
        m_ThreadAudio.Clear();
    }
}

static void CalculateYuvToRgbMatrix(
    _In_ const AVFrame* pFrame,
    BOOL b10Bit,
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
    _Inout_ VideoFrameContext& Context) noexcept
{
    int r;
    Context.pPlaneY = nullptr;
    Context.cbStrideY = 0;
    Context.pPlaneUv = nullptr;
    Context.cbStrideUv = 0;
    Context.b10Bit = FALSE;

    const auto eFmtSource = (AVPixelFormat)pFrame->format;
    const auto* const pFmtDesc = av_pix_fmt_desc_get(eFmtSource);
    if (!pFmtDesc)
        return;
    auto eFmtDest = (pFmtDesc->comp[0].depth > 8) ?
        AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12;

    if (eFmtSource == eFmtDest)
    {
        Context.pPlaneY = pFrame->data[0];
        Context.cbStrideY = (UINT)pFrame->linesize[0];
        Context.pPlaneUv = pFrame->data[1];
        Context.cbStrideUv = (UINT)pFrame->linesize[1];
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

        Context.pPlaneY = pDstData[0];
        Context.cbStrideY = (UINT)cbDstStride[0];
        Context.pPlaneUv = pDstData[1];
        Context.cbStrideUv = (UINT)cbDstStride[1];
    }
    Context.b10Bit = (eFmtDest == AV_PIX_FMT_P010LE);
}

HRESULT CPlayer::Initialize() noexcept
{
    m_Ffmpeg.InitializeDirect3D(eck::g_pD3D11Device);
    return S_OK;
}

int CPlayer::OpenFile(std::wstring_view svFile) noexcept
{
    eck::CStringA rsPathU8{};
    eck::EcdWideToUtf8(rsPathU8, svFile.data(), (int)svFile.size());

    m_Ffmpeg.Stop();
    StopThreadAudio();

    const auto r = m_Ffmpeg.OpenFile(rsPathU8.Data());

    if (m_Ffmpeg.HasAudioStream())
    {
        UINT nSampleRate, cChannel;
        m_Ffmpeg.GetAudioFormat(nSampleRate, cChannel);
        if (SUCCEEDED(m_AudioPlayer.Initialize(nSampleRate, cChannel)))
        {
            m_bStopAudio.store(false, std::memory_order_release);
            m_ThreadAudio.Attach(eck::CrtCreateThread(
                [](void* p) noexcept -> UINT
                {
                    ((CPlayer*)p)->ThreadAudio();
                    return 0;
                }, this));
        }
    }

    return r.nCode;
}

BOOL CPlayer::PopVideoFrame(
    Eck_Out_buffer_ VideoFrame& Frame,
    _Inout_ VideoFrameContext& Context) noexcept
{
    FfPtr<AVFrame> pFrame{};
    if (!m_Ffmpeg.TryPopVideoDecoded(pFrame))
        return FALSE;

    if (pFrame->format == AV_PIX_FMT_D3D11)
    {
        Frame.pTexture = (ID3D11Texture2D*)pFrame->data[0];
        Frame.idxSubResource = eck::PointerToDword<UINT>(pFrame->data[1]);
        Context.b10Bit = FALSE;
        const auto* const pFrames =
            (const AVHWFramesContext*)pFrame->hw_frames_ctx->data;
        const auto* const pDesc = av_pix_fmt_desc_get(pFrames->sw_format);
        if (pDesc)
            Context.b10Bit = (pDesc->comp[0].depth > 8);
    }
    else
    {
        Frame.pTexture.Clear();
        Frame.idxSubResource = 0;
        ConvertSoftwareFrame(pFrame.Get(), Context);
    }
    CalculateYuvToRgbMatrix(pFrame.Get(), Context.b10Bit, Frame.Yuv2Rgb);

    Frame.pts = m_Ffmpeg.GetPresentTimestamp(pFrame.Get());
    Frame.cx = (UINT)pFrame->width;
    Frame.cy = (UINT)pFrame->height;
    Frame.pFrame = std::move(pFrame);
    return TRUE;
}

double CPlayer::GetMasterClock() noexcept
{
    if (m_AudioPlayer.HasClock())
        return m_AudioPlayer.GetCurrentTime();
    return 0.;
}