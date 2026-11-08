#pragma once
class CAudioPlayer
{
private:
    ComPtr<IMMDeviceEnumerator> m_pDeviceEnumerator{};
    ComPtr<IMMDevice> m_pDevice{};
    ComPtr<IAudioClient> m_pClient{};
    ComPtr<IAudioRenderClient> m_pRenderClient{};

    eck::CEvent m_Event{};

    UINT m_cAudioBuffer{};
    UINT m_cChannel{};
    UINT m_nSampleRate{};

    LARGE_INTEGER m_QpcFreq{};
    LARGE_INTEGER m_QpcAnchor{};
    eck::CSrwLock m_Lock{};
    double m_ptsAnchor{};
    bool m_bAnchorValid{};

    std::atomic<bool> m_bSeek{};
    std::atomic<bool> m_bExit{};
public:
    HRESULT Initialize(UINT nSampleRate, UINT cChannel) noexcept;

    void RequestShutdown() noexcept;
    void Shutdown() noexcept;

    void Seek(double pts) noexcept;
    void Reset() noexcept;

    bool IsSeekRequested() const noexcept
    {
        return m_bSeek.load(std::memory_order_acquire);
    }
    void CompleteSeek() noexcept
    {
        m_bSeek.store(false, std::memory_order_release);
    }

    // 返回[-1, 1]归一化交错浮点缓冲区
    std::span<float> GetBuffer(UINT cRequested) noexcept;
    void ReleaseBuffer(UINT cWritten) noexcept;

    // 负值表示无效
    double GetCurrentTime() noexcept;

    EckInlineNdCe UINT GetChannelCount() const noexcept { return m_cChannel; }
    EckInlineNdCe UINT GetSampleRate() const noexcept { return m_nSampleRate; }

    void SetCurrentPresentTimestamp(double pts) noexcept;
};