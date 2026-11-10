#include "pch.h"
#include "CFfmpeg.h"

void CFfmpeg::InitializeDirect3D(ID3D11Device* pDevice) noexcept
{
    m_pD3DDevice = pDevice;
    ComPtr<ID3D10Multithread> pMt;
    if (SUCCEEDED(m_pD3DDevice->QueryInterface(&pMt)))
        pMt->SetMultithreadProtected(TRUE);
}

void CFfmpeg::SwrFree() noexcept
{
    swr_free(&m_pSwr);
    av_channel_layout_uninit(&m_SwrInLayout);
    av_channel_layout_uninit(&m_SwrOutLayout);
    m_nSwrInRate = 0;
    m_eSwrInFormat = AV_SAMPLE_FMT_NONE;
}

void CFfmpeg::SwrSetAoFormat(const AoFormat& Format) noexcept
{
    {
        const eck::CCsGuard _{ m_Lock };
        m_OutFormat = Format;
    }
    m_nFormatSerial.fetch_add(1, std::memory_order_acq_rel);
    m_qAudioDecoded.Clear();
}

AoFormat CFfmpeg::SwrEnsureValid(_In_ const AVFrame* pFrame) noexcept
{
    AoFormat Out;
    {
        const eck::CCsGuard _{ m_Lock };
        Out = m_OutFormat;
    }

    if (!Out.IsValid())
        return {};

    const auto eInFormat = (AVSampleFormat)pFrame->format;
    if (m_pSwr &&
        m_nSwrInRate == pFrame->sample_rate &&
        m_eSwrInFormat == eInFormat &&
        av_channel_layout_compare(&m_SwrInLayout, &pFrame->ch_layout) == 0)
        return Out;

    SwrFree();

    // AV_CH_LAYOUT_*与SPEAKER_*定义相同
    if (Out.dwChannelMask)
        av_channel_layout_from_mask(&m_SwrOutLayout, Out.dwChannelMask);
    if ((UINT)m_SwrOutLayout.nb_channels != Out.cChannel)
    {
        av_channel_layout_uninit(&m_SwrOutLayout);
        av_channel_layout_default(&m_SwrOutLayout, (int)Out.cChannel);
    }

    if (swr_alloc_set_opts2(
        &m_pSwr,
        &m_SwrOutLayout,
        Out.eSampleFormat,
        (int)Out.nSampleRate,
        &pFrame->ch_layout,
        eInFormat,
        pFrame->sample_rate,
        0,
        nullptr) < 0)
    {
        SwrFree();
        return {};
    }

    if (swr_init(m_pSwr) < 0)
    {
        SwrFree();
        return {};
    }

    m_nSwrInRate = pFrame->sample_rate;
    m_eSwrInFormat = eInFormat;
    av_channel_layout_copy(&m_SwrInLayout, &pFrame->ch_layout);
    return Out;
}

FfPtr<AVFrame> CFfmpeg::SwrConvertAudio(_In_ const AVFrame* pFrame) noexcept
{
    if (pFrame->nb_samples <= 0)
        return {};
    const auto Out = SwrEnsureValid(pFrame);
    if (!Out.IsValid())
        return {};

    const auto cOutMax = swr_get_out_samples(m_pSwr, pFrame->nb_samples);
    if (cOutMax <= 0)
        return {};
    FfPtr<AVFrame> pOut{ av_frame_alloc() };
    if (!pOut)
        return {};

    pOut->format = (int)Out.eSampleFormat;
    pOut->sample_rate = (int)Out.nSampleRate;
    pOut->nb_samples = cOutMax;
    av_channel_layout_copy(&pOut->ch_layout, &m_SwrOutLayout);
    if (av_frame_get_buffer(pOut.Get(), 0) < 0)
        return {};

    BYTE* pDst = pOut->extended_data[0];
    const auto cOut = swr_convert(
        m_pSwr,
        &pDst,
        cOutMax,
        (const BYTE**)pFrame->extended_data,
        pFrame->nb_samples);
    if (cOut <= 0)
        return {};

    pOut->nb_samples = cOut;
    pOut->pts = pFrame->pts;
    pOut->best_effort_timestamp = pFrame->best_effort_timestamp;
    pOut->opaque = pFrame->opaque;
    return pOut;
}

void CFfmpeg::ThreadVideoDecode() noexcept
{
    int r;
    FfPtr<AVPacket> pPacket;
    auto nSerial = m_nSeekSerial.load(std::memory_order_acquire);
    while (!m_bStopDecode.load(std::memory_order_acquire))
    {
        if (!pPacket)
        {
            if (!m_qVideo.Pop(pPacket))
                break;
        }

        const auto n = eck::PointerToDword<USHORT>(pPacket->opaque);
        if (n != m_nSeekSerial.load(std::memory_order_acquire))
        {
            pPacket.Clear();
            continue;
        }
        if (n != nSerial)
        {
            nSerial = n;
            avcodec_flush_buffers(m_pVideoCodecCtx.Get());
        }

        r = avcodec_send_packet(m_pVideoCodecCtx.Get(), pPacket.Get());
        if (r == AVERROR(ENOMEM))
            pPacket.Clear();
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
        if (r)
            continue;

        pFrame->opaque = eck::DwordToPointer<void*>(nSerial);
        if (!m_qVideoDecoded.Push(std::move(pFrame)))
            break;
    }
}

void CFfmpeg::ThreadAudioDecode() noexcept
{
    int r;
    FfPtr<AVPacket> pPacket;
    auto nSerial = m_nSeekSerial.load(std::memory_order_acquire);
    auto nFormatSerial = m_nFormatSerial.load(std::memory_order_acquire);
    while (!m_bStopDecode.load(std::memory_order_acquire))
    {
        if (!pPacket)
        {
            if (!m_qAudio.Pop(pPacket))
                break;
        }

        const auto n = eck::PointerToDword<USHORT>(pPacket->opaque);
        if (n != m_nSeekSerial.load(std::memory_order_acquire))
        {
            pPacket.Clear();
            continue;
        }
        if (n != nSerial)
        {
            nSerial = n;
            avcodec_flush_buffers(m_pAudioCodecCtx.Get());
            SwrFree();
        }
        if (const auto s = m_nFormatSerial.load(std::memory_order_acquire); s != nFormatSerial)
        {
            nFormatSerial = s;
            SwrFree();
        }

        r = avcodec_send_packet(m_pAudioCodecCtx.Get(), pPacket.Get());
        if (r && r != AVERROR(EAGAIN))
        {
            pPacket.Clear();
            continue;
        }
        else if (!r)
            pPacket.Clear();

        bool bStop{};
        EckLoop()
        {
            FfPtr<AVFrame> pFrame{ av_frame_alloc() };
            if (!pFrame)
            {
                bStop = true;
                break;
            }
            r = avcodec_receive_frame(m_pAudioCodecCtx.Get(), pFrame.Get());
            if (r)
                break;

            pFrame->opaque = eck::DwordToPointer<void*>(
                (DWORD(nFormatSerial) << 16) | nSerial);

            auto pOut = SwrConvertAudio(pFrame.Get());
            pFrame.Clear();
            if (!pOut)
                continue;
            if (m_nFormatSerial.load(std::memory_order_acquire) != nFormatSerial)
                continue;
            if (!m_qAudioDecoded.Push(std::move(pOut)))
            {
                bStop = true;
                break;
            }
        }
        if (bStop)
            break;
    }
    SwrFree();
}

void CFfmpeg::ThreadDemux() noexcept
{
    int r;
    auto nSerial = m_nSeekSerial.load(std::memory_order_acquire);
    while (!m_bStopDecode.load(std::memory_order_acquire))
    {
        if (const auto s = m_ptsSeek.exchange(-1., std::memory_order_acq_rel); s >= 0.)
        {
            const auto ts = int64_t((s + m_ptsStart) * AV_TIME_BASE);
            r = av_seek_frame(m_pFmtCtx.Get(), -1, ts, AVSEEK_FLAG_BACKWARD);
            if (r < 0)
                r = avformat_seek_file(
                    m_pFmtCtx.Get(), -1, INT64_MIN, ts, INT64_MAX, 0);
            if (r < 0)
                r = av_seek_frame(m_pFmtCtx.Get(), -1, ts, AVSEEK_FLAG_ANY);
            nSerial = m_nSeekSerial.load(std::memory_order_acquire);
            continue;
        }

        FfPtr<AVPacket> pPacket{ av_packet_alloc() };
        if (!pPacket)
            break;
        r = av_read_frame(m_pFmtCtx.Get(), pPacket.Get());
        if (r < 0)
            break;

        if (m_nSeekSerial.load(std::memory_order_acquire) != nSerial)
        {
            pPacket.Clear();
            continue;
        }
        pPacket->opaque = eck::DwordToPointer<void*>(nSerial);

        bool b;
        if (pPacket->stream_index == m_idxStreamVideo)
            b = m_qVideo.Push(std::move(pPacket));
        else if (pPacket->stream_index == m_idxStreamAudio)
            b = m_qAudio.Push(std::move(pPacket));
        else
            b = true;

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

    bool bHardware{};
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

        bHardware = !!m_pHwDeviceCtx;
        if (bHardware)
        {
            m_pVideoCodecCtx->hw_device_ctx = av_buffer_ref(m_pHwDeviceCtx.Get());
            m_pVideoCodecCtx->extra_hw_frames = (int)QueueVideoSize;
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
    eck::CScopeGuard ExitGuard{ [this]() noexcept { Stop(); } };

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

    m_ptsStart =
        (m_pFmtCtx->start_time != AV_NOPTS_VALUE) ?
        (double)m_pFmtCtx->start_time * av_q2d(AV_TIME_BASE_Q) :
        0.;

    m_idxStreamVideo = av_find_best_stream(m_pFmtCtx.Get(),
        AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_idxStreamVideo >= 0)
    {
        const auto& s = *m_pFmtCtx->streams[m_idxStreamVideo];
        if (s.disposition & AV_DISPOSITION_ATTACHED_PIC)
        {
            m_idxStreamVideo = -1;
            for (UINT i = 0; i < m_pFmtCtx->nb_streams; ++i)
            {
                const auto& e = *m_pFmtCtx->streams[i];
                if (e.codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                    !(e.disposition & AV_DISPOSITION_ATTACHED_PIC))
                {
                    m_idxStreamVideo = (int)i;
                    break;
                }
            }
        }
    }

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
        const auto r1 = PrepareVideoCodec();
        if (r1.eType != ErrorType::Ok)
            return { r1.eType, r1.nCode };
        m_ThreadVideo.Attach(eck::CrtCreateThread(
            [](void* p) noexcept -> UINT
            {
                ((CFfmpeg*)p)->ThreadVideoDecode();
                return 0;
            }, this));
    }
    if (m_idxStreamAudio >= 0)
    {
        const auto r1 = PrepareAudioCodec();
        if (r1.eType != ErrorType::Ok)
            return { r1.eType, r1.nCode };
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
    ExitGuard.Cancel();
    return { ErrorType::Ok };
}

void CFfmpeg::Stop() noexcept
{
    StopDecodeThread();
    SwrFree();
    m_qVideo.Clear();
    m_qAudio.Clear();
    m_qVideoDecoded.Clear();
    m_qAudioDecoded.Clear();
    m_pVideoCodecCtx.Clear();
    m_pAudioCodecCtx.Clear();
    m_pFmtCtx.Clear();
    m_idxStreamVideo = -1;
    m_idxStreamAudio = -1;
    m_ptsStart = 0.;
    m_ptsSeek.store(-1., std::memory_order_release);
}

double CFfmpeg::GetPresentTimestamp(_In_ const AVFrame* pFrame, int idxStream) const noexcept
{
    const auto d =
        pFrame->best_effort_timestamp *
        av_q2d(m_pFmtCtx->streams[idxStream]->time_base) -
        m_ptsStart;
    return std::max(d, 0.);
}
double CFfmpeg::GetAudioPresentTimestamp(_In_ const AVFrame* pFrame) const noexcept
{
    return GetPresentTimestamp(pFrame, m_idxStreamAudio);
}
double CFfmpeg::GetVideoPresentTimestamp(_In_ const AVFrame* pFrame) const noexcept
{
    return GetPresentTimestamp(pFrame, m_idxStreamVideo);
}

void CFfmpeg::Seek(double s) noexcept
{
    m_nSeekSerial.fetch_add(1, std::memory_order_acq_rel);
    m_ptsSeek.store(s, std::memory_order_release);
    m_qAudio.Clear();
    m_qVideo.Clear();
    m_qAudioDecoded.Clear();
    m_qVideoDecoded.Clear();
}

double CFfmpeg::GetDuration() noexcept
{
    if (!m_pFmtCtx)
        return 0.;
    if (m_pFmtCtx->duration != AV_NOPTS_VALUE)
        return m_pFmtCtx->duration * av_q2d(AV_TIME_BASE_Q);

    double d = 0.;
    for (UINT i = 0; i < m_pFmtCtx->nb_streams; ++i)
    {
        const auto& e = *m_pFmtCtx->streams[i];
        if (e.duration != AV_NOPTS_VALUE)
            d = std::max(d, e.duration * av_q2d(e.time_base));
    }
    return d;
}