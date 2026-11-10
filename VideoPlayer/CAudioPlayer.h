#pragma once
#include "AudioFormat.h"

class CAudioPlayer
{
public:
    enum class WriteState
    {
        Ok,         // 所有数据已成功写入
        Seek,       // 由于查找操作，调用方应丢弃当前数据
        DeviceLost, // 设备已无效，调用方应重新初始化音频播放
        Exit,       // 正在退出
    };
private:
    ComPtr<IMMDeviceEnumerator> m_pDeviceEnumerator{};
    ComPtr<IMMDevice> m_pDevice{};
    ComPtr<IAudioClient> m_pClient{};
    ComPtr<IAudioRenderClient> m_pRenderClient{};

    eck::CEvent m_Event{};

    WAVEFORMATEXTENSIBLE m_wfx{};

    UINT m_cAudioBuffer{};

    std::atomic<bool> m_bSeek{};
    std::atomic<bool> m_bExit{};
public:
    HRESULT Initialize() noexcept;

    void RequestShutdown() noexcept;
    void Shutdown() noexcept;

    void RequestSeek() noexcept;

    WriteState Write(PCVOID pData, UINT cFrames, _Out_ UINT& cWritten) noexcept;

    EckInlineNdCe auto& GetWaveFormat() const noexcept { return m_wfx.Format; }

    UINT GetCurrentPadding() noexcept;
};