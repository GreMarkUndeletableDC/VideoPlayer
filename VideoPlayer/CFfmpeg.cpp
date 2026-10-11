#include "pch.h"
#include "CFfmpeg.h"

static void SleepDecodeThread() noexcept
{
    LARGE_INTEGER li{ .QuadPart = -5'0000ll };
    NtDelayExecution(FALSE, &li);
}

BOOL CFfmpeg::WriteAudioFrame(
    _In_ const AVFrame* pFrame,
    UINT cChannel,
    _Out_writes_(cChannel* pFrame->nb_samples) float* pBuf) noexcept
{
    const auto cBuf = cChannel * pFrame->nb_samples;
    switch (pFrame->format)
    {
    case AV_SAMPLE_FMT_U8:
    {
        auto p = (BYTE*)pFrame->data[0];
        const auto pEnd = p + cBuf;
        for (; p < pEnd; ++p)
            *pBuf++ = (float(*p) - 128.f) / 128.f;
    }
    break;
    case AV_SAMPLE_FMT_S16:
    {
        auto p = (short*)pFrame->data[0];
        const auto pEnd = p + cBuf;
        for (; p < pEnd; ++p)
            *pBuf++ = (float(*p) / 32768.f);
    }
    break;
    case AV_SAMPLE_FMT_S32:
    {
        auto p = (int*)pFrame->data[0];
        const auto pEnd = p + cBuf;
        for (; p < pEnd; ++p)
            *pBuf++ = (float(*p) / 2147483648.f);
    }
    break;
    case AV_SAMPLE_FMT_FLT:
        memcpy(pBuf, pFrame->data[0], cBuf * sizeof(float));
        break;
    case AV_SAMPLE_FMT_DBL:
    {
        auto p = (double*)pFrame->data[0];
        const auto pEnd = p + cBuf;
        for (; p < pEnd; ++p)
            *pBuf++ = float(*p);
    }
    break;
    case AV_SAMPLE_FMT_U8P:
    {
        const auto p = (BYTE**)pFrame->data;
        for (UINT i = 0; i < cBuf; ++i)
            for (UINT j = 0; j < cChannel; ++j)
                *pBuf++ = (float(p[j][i]) - 128.f) / 128.f;
    }
    break;
    case AV_SAMPLE_FMT_S16P:
    {
        const auto p = (short**)pFrame->data;
        for (int i = 0; i < pFrame->nb_samples; ++i)
            for (UINT j = 0; j < cChannel; ++j)
                *pBuf++ = (float(p[j][i]) / 32768.f);
    }
    break;
    case AV_SAMPLE_FMT_S32P:
    {
        const auto p = (int**)pFrame->data;
        for (int i = 0; i < pFrame->nb_samples; ++i)
            for (UINT j = 0; j < cChannel; ++j)
                *pBuf++ = (float(p[j][i]) / 2147483648.f);
    }
    break;
    case AV_SAMPLE_FMT_FLTP:
    {
        const auto p = (float**)pFrame->data;
        for (int i = 0; i < pFrame->nb_samples; ++i)
            for (UINT j = 0; j < cChannel; ++j)
                *pBuf++ = p[j][i];
    }
    break;
    case AV_SAMPLE_FMT_DBLP:
    {
        const auto p = (double**)pFrame->data;
        for (int i = 0; i < pFrame->nb_samples; ++i)
            for (UINT j = 0; j < cChannel; ++j)
                *pBuf++ = float(p[j][i]);
    }
    break;
    default: return FALSE;
    }
    return TRUE;
}

void CFfmpeg::ThreadVideoDecode() noexcept
{
    int r;
    FfPtr<AVPacket> pPacket;
    while (!m_bStopDecode.load(std::memory_order_acquire))
    {
        if (!pPacket)
        {
            if (!m_qVideo.Pop(pPacket))
                break;
        }

        r = avcodec_send_packet(m_pVideoCodecCtx.Get(), pPacket.Get());
        if (r == AVERROR(ENOMEM))
        {
            pPacket.Clear();
            SleepDecodeThread();
        }
        else
        {
            if (r && r != AVERROR(EAGAIN))
            {
                pPacket.Clear();
                continue;
            }
            else if (!r)
                pPacket.Clear();
        }

        FfPtr<AVFrame> pFrame{ av_frame_alloc() };
        if (!pFrame)
            break;
        r = avcodec_receive_frame(m_pVideoCodecCtx.Get(), pFrame.Get());
        if (r == AVERROR(ENOMEM))
        {
            SleepDecodeThread();
            continue;
        }
        if (r)
            continue;

        if (!m_qVideoDecoded.Push(std::move(pFrame)))
            break;
    }
}

void CFfmpeg::ThreadAudioDecode() noexcept
{
    int r;
    FfPtr<AVPacket> pPacket;
    while (!m_bStopDecode.load(std::memory_order_acquire))
    {
        if (!pPacket)
        {
            if (!m_qAudio.Pop(pPacket))
                break;
        }

        r = avcodec_send_packet(m_pAudioCodecCtx.Get(), pPacket.Get());
        if (r && r != AVERROR(EAGAIN))
        {
            pPacket.Clear();
            continue;
        }
        else if (!r)
            pPacket.Clear();

        FfPtr<AVFrame> pFrame{ av_frame_alloc() };
        if (!pFrame)
            break;
        r = avcodec_receive_frame(m_pAudioCodecCtx.Get(), pFrame.Get());
        if (r)
            continue;

        if (!m_qAudioDecoded.Push(std::move(pFrame)))
            break;
    }
}

void CFfmpeg::ThreadDemux() noexcept
{
    int r;
    while (!m_bStopDecode.load(std::memory_order_acquire))
    {
        FfPtr<AVPacket> pPacket{ av_packet_alloc() };
        if (!pPacket)
            break;
        r = av_read_frame(m_pFmtCtx.Get(), pPacket.Get());
        if (r < 0)
            break;

        BOOL b;
        if (pPacket->stream_index == m_idxStreamVideo)
            b = m_qVideo.Push(std::move(pPacket));
        else if (pPacket->stream_index == m_idxStreamAudio)
            b = m_qAudio.Push(std::move(pPacket));
        else
            b = TRUE;

        if (!b)
            break;
    }
}

CFfmpeg::Result CFfmpeg::PrepareVideoCodec() noexcept
{
    int r;
    const auto& Stream = *m_pFmtCtx->streams[m_idxStreamVideo];

    const auto pCodec = avcodec_find_decoder(Stream.codecpar->codec_id);
    m_pVideoCodecCtx.Attach(avcodec_alloc_context3(pCodec));
    if (!m_pVideoCodecCtx)
        return { ErrorType::VideoCodec, AVERROR(ENOMEM) };
    avcodec_parameters_to_context(m_pVideoCodecCtx.Get(), Stream.codecpar);

    BOOL bHardware{};
    if (m_pD3DDevice)
    {
        if (!m_pHwDeviceCtx)
        {
            FfRefPtr<AVBufferRef> pHwCtx{ av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA) };
            if (pHwCtx)
            {
                const auto pCtx =
                    (AVD3D11VADeviceContext*)((AVHWDeviceContext*)pHwCtx->data)->hwctx;
                pCtx->device = m_pD3DDevice.Get();
                pCtx->device_context = nullptr;
                pCtx->lock = nullptr;
                pCtx->unlock = nullptr;
                pCtx->BindFlags |= D3D11_BIND_SHADER_RESOURCE;
                m_pD3DDevice->AddRef();

                r = av_hwdevice_ctx_init(pHwCtx.Get());
                if (!r)
                    m_pHwDeviceCtx = std::move(pHwCtx);
            }
        }

        bHardware = m_pHwDeviceCtx ? TRUE : FALSE;
        if (bHardware)
        {
            m_pVideoCodecCtx->hw_device_ctx = av_buffer_ref(m_pHwDeviceCtx.Get());
            m_pVideoCodecCtx->get_format = [](AVCodecContext*, const AVPixelFormat* pPixFmts)
                {
                    for (auto p = pPixFmts; *p != AV_PIX_FMT_NONE; ++p)
                        if (*p == AV_PIX_FMT_D3D11)
                            return *p;
                    return pPixFmts[0];
                };
        }
    }

    r = avcodec_open2(m_pVideoCodecCtx.Get(), pCodec, nullptr);
    if (r && bHardware)
    {
        m_pVideoCodecCtx.Attach(avcodec_alloc_context3(pCodec));
        if (!m_pVideoCodecCtx)
            return { ErrorType::VideoCodec, AVERROR(ENOMEM) };
        avcodec_parameters_to_context(m_pVideoCodecCtx.Get(), Stream.codecpar);
        r = avcodec_open2(m_pVideoCodecCtx.Get(), pCodec, nullptr);
    }

    if (r)
        return { ErrorType::VideoCodec, r };
    return { ErrorType::Ok };
}

CFfmpeg::Result CFfmpeg::PrepareAudioCodec() noexcept
{
    int r;
    const auto& Stream = *m_pFmtCtx->streams[m_idxStreamAudio];

    const auto pCodec = avcodec_find_decoder(Stream.codecpar->codec_id);
    m_pAudioCodecCtx.Attach(avcodec_alloc_context3(pCodec));
    if (!m_pAudioCodecCtx)
        return { ErrorType::AudioCodec, AVERROR(ENOMEM) };
    avcodec_parameters_to_context(m_pAudioCodecCtx.Get(), Stream.codecpar);

    r = avcodec_open2(m_pAudioCodecCtx.Get(), pCodec, nullptr);
    if (r)
        return { ErrorType::AudioCodec, r };
    return { ErrorType::Ok };
}

void CFfmpeg::StopDecodeThread() noexcept
{
    m_bStopDecode.store(true, std::memory_order_release);

    m_qVideo.Close();
    m_qAudio.Close();
    m_qVideoDecoded.Close();
    m_qAudioDecoded.Close();

    if (m_ThreadDemux.Get())
    {
        eck::WaitObject(m_ThreadDemux);
        m_ThreadDemux.Clear();
    }
    if (m_ThreadVideo.Get())
    {
        eck::WaitObject(m_ThreadVideo);
        m_ThreadVideo.Clear();
    }
    if (m_ThreadAudio.Get())
    {
        eck::WaitObject(m_ThreadAudio);
        m_ThreadAudio.Clear();
    }
}

CFfmpeg::Result CFfmpeg::OpenFile(_In_z_ PCSTR pszPathU8) noexcept
{
    m_pFmtCtx.Attach(avformat_alloc_context());
    if (!m_pFmtCtx)
        return { ErrorType::FormatContext, AVERROR(ENOMEM) };
    int r;

    r = avformat_open_input(m_pFmtCtx.At(), pszPathU8, nullptr, nullptr);
    if (r)
        return { ErrorType::OpenInput, r };

    r = avformat_find_stream_info(m_pFmtCtx.Get(), nullptr);
    if (r)
        return { ErrorType::StreamInformation, r };

    m_idxStreamVideo = av_find_best_stream(m_pFmtCtx.Get(),
        AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    m_idxStreamAudio = av_find_best_stream(m_pFmtCtx.Get(),
        AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_idxStreamVideo < 0 && m_idxStreamAudio < 0)
        return { ErrorType::FindStream, AVERROR_STREAM_NOT_FOUND };

    m_bStopDecode.store(false, std::memory_order_release);
    m_qVideo.Open();
    m_qAudio.Open();
    m_qVideoDecoded.Open();
    m_qAudioDecoded.Open();

    if (m_idxStreamVideo >= 0)
    {
        const auto r = PrepareVideoCodec();
        if (r.eType != ErrorType::Ok)
            return r;
        m_ThreadVideo.Attach(eck::CrtCreateThread(
            [](void* p) noexcept -> UINT
            {
                ((CFfmpeg*)p)->ThreadVideoDecode();
                return 0;
            }, this));
    }
    if (m_idxStreamAudio >= 0)
    {
        const auto r = PrepareAudioCodec();
        if (r.eType != ErrorType::Ok)
            return r;
        m_ThreadAudio.Attach(eck::CrtCreateThread(
            [](void* p) noexcept -> UINT
            {
                ((CFfmpeg*)p)->ThreadAudioDecode();
                return 0;
            }, this));
    }
    m_ThreadDemux.Attach(eck::CrtCreateThread(
        [](void* p) noexcept -> UINT
        {
            ((CFfmpeg*)p)->ThreadDemux();
            return 0;
        }, this));
    return { ErrorType::Ok };
}

void CFfmpeg::Stop() noexcept
{
    StopDecodeThread();
    m_qVideo.Clear();
    m_qAudio.Clear();
    m_qVideoDecoded.Clear();
    m_qAudioDecoded.Clear();
}

double CFfmpeg::GetPresentTimestamp(const AVFrame* pFrame) noexcept
{
    const auto TimeBase = m_pFmtCtx->streams[m_idxStreamVideo]->time_base;
    return pFrame->best_effort_timestamp * av_q2d(TimeBase);
}

void CFfmpeg::GetAudioFormat(_Out_ UINT& nSampleRate, _Out_ UINT& cChannel) noexcept
{
    const auto* const pCodecPar = m_pFmtCtx->streams[m_idxStreamAudio]->codecpar;
    nSampleRate = m_pAudioCodecCtx->sample_rate;
    cChannel = m_pAudioCodecCtx->ch_layout.nb_channels;
}