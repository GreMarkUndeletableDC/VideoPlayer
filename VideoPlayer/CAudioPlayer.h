#pragma once
class CAudioPlayer
{
private:
    ComPtr<IMMDeviceEnumerator> m_pDeviceEnumerator{};
    ComPtr<IMMDevice> m_pDevice{};
    ComPtr<IAudioClient> m_pClient{};
    ComPtr<IAudioRenderClient> m_pRenderClient{};
    ComPtr<IAudioClock> m_pClock{};

    eck::CEvent m_Event{};

    UINT m_cAudioBuffer{};
    UINT m_cChannel{};
    UINT64 m_ullFrequency{};
public:
    HRESULT Initialize(UINT uSampleRate, UINT cChannel) noexcept;
    // 返回[-1, 1]归一化交错浮点缓冲区
    std::span<float> GetBuffer(UINT cRequested) noexcept;
    void ReleaseBuffer(UINT cWritten) noexcept;
    double GetCurrentTime() noexcept;
    void Reset() noexcept;
    EckInlineNdCe UINT GetChannelCount() const noexcept { return m_cChannel; }
    EckInlineNdCe BOOL HasClock() const noexcept { return !!m_pClock; }
};