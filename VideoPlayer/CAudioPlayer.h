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

    std::atomic<double> m_ptsLast{};
    std::atomic_bool m_bLastPtsValid{};

    std::atomic_bool m_bExit{};
public:
    HRESULT Initialize(UINT uSampleRate, UINT cChannel) noexcept;
    void Shutdown() noexcept;

    // 返回[-1, 1]归一化交错浮点缓冲区
    std::span<float> GetBuffer(UINT cRequested) noexcept;
    void ReleaseBuffer(UINT cWritten) noexcept;

    // 负值表示无效
    double GetCurrentTime() noexcept;

    EckInlineNdCe UINT GetChannelCount() const noexcept { return m_cChannel; }
    EckInlineNdCe UINT GetSampleRate() const noexcept { return m_nSampleRate; }

    void SetCurrentPresentTimestamp(double pts) noexcept;
};