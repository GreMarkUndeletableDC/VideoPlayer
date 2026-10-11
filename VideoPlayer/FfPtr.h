#pragma once
template<class T>
class FfPtr
{
private:
    T* m_p{};
    
    void Destroy() noexcept
    {
        if constexpr (std::is_same_v<T, AVFrame>)
            av_frame_free(&m_p);
        else if constexpr (std::is_same_v<T, AVPacket>)
            av_packet_free(&m_p);
        else if constexpr (std::is_same_v<T, AVFormatContext>)
            avformat_free_context(m_p);
        else if constexpr (std::is_same_v<T, AVCodecContext>)
            avcodec_free_context(&m_p);
        else
            static_assert(!sizeof(T), "Unsupported type for FfPtr");
    }
public:
    constexpr FfPtr() noexcept = default;
    constexpr FfPtr(T* p) noexcept : m_p{ p } {}

    FfPtr(const FfPtr&) = delete;
    FfPtr& operator=(const FfPtr&) = delete;

    FfPtr(FfPtr&& x) noexcept : m_p{ x.m_p }
    {
        x.m_p = nullptr;
    }
    FfPtr& operator=(FfPtr&& x) noexcept
    {
        std::swap(m_p, x.m_p);
        return *this;
    }

    ~FfPtr() noexcept { Destroy(); }

    EckInlineNdCe T* Get() const noexcept { return m_p; }
    void Clear() noexcept
    {
        Destroy();
        m_p = nullptr;
    }

    EckInlineNdCe T** AtClear() noexcept
    {
        Clear();
        return &m_p;
    }
    EckInlineNdCe T** At() noexcept { return &m_p; }
    EckInlineNdCe T** operator&() noexcept { return &m_p; }

    EckInlineNdCe T* operator->() const noexcept { return m_p; }

    explicit operator bool() const noexcept { return !!m_p; }

    EckInline void Attach(T* p) noexcept
    {
        Destroy();
        m_p = p;
    }
    EckInlineNd T* Detach() noexcept
    {
        T* p{};
        std::swap(p, m_p);
        return p;
    }
};

template<class T>
class FfRefPtr
{
private:
    T* m_p{};

    void Destroy() noexcept
    {
        if (!m_p)
            return;
        if constexpr (std::is_same_v<T, AVBufferRef>)
            av_buffer_unref(&m_p);
        else
            static_assert(!sizeof(T), "Unsupported type for FfRefPtr");
    }
    void Reference() noexcept
    {
        if (!m_p)
            return;
        if constexpr (std::is_same_v<T, AVBufferRef>)
            av_buffer_ref(&m_p);
        else
            static_assert(!sizeof(T), "Unsupported type for FfRefPtr");
    }
public:
    constexpr FfRefPtr() noexcept = default;
    constexpr FfRefPtr(T* p) noexcept : m_p{ p } {}
    FfRefPtr(const FfRefPtr& x) noexcept
    {
        m_p = x.m_p;
        Reference();
    }
    FfRefPtr& operator=(const FfRefPtr& x) noexcept
    {
        if (this != &x)
        {
            Destroy();
            m_p = x.m_p;
            Reference();
        }
        return *this;
    }
    FfRefPtr(FfRefPtr&& x) noexcept : m_p{ x.m_p }
    {
        x.m_p = nullptr;
    }
    FfRefPtr& operator=(FfRefPtr&& x) noexcept
    {
        std::swap(m_p, x.m_p);
        return *this;
    }

    ~FfRefPtr() noexcept { Destroy(); }

    EckInlineNdCe T* Get() const noexcept { return m_p; }
    void Clear() noexcept
    {
        Destroy();
        m_p = nullptr;
    }

    EckInlineNdCe T** AtClear() noexcept
    {
        Clear();
        return &m_p;
    }
    EckInlineNdCe T** At() noexcept { return &m_p; }
    EckInlineNdCe T** operator&() noexcept { return &m_p; }

    EckInlineNdCe T* operator->() const noexcept { return m_p; }

    explicit operator bool() const noexcept { return !!m_p; }

    EckInline void Attach(T* p) noexcept
    {
        Destroy();
        m_p = p;
    }
    EckInlineNd T* Detach() noexcept
    {
        T* p{};
        std::swap(p, m_p);
        return p;
    }
};