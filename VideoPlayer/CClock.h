#pragma once
struct CClock
{
public:
    constexpr static TSecond None = std::numeric_limits<TSecond>::max();
private:
    eck::CSrwLock m_Lock{};
    TSecond m_pts{ None };
    LARGE_INTEGER m_Qpc{};

    EckInlineNdCe bool InternalIsValid() const noexcept { return m_pts != None; }
public:
    void Set(TSecond pts, LARGE_INTEGER Qpc) noexcept;
    void Set(TSecond pts) noexcept;
    void SetForward(TSecond pts) noexcept;
    TSecond Get() noexcept;

    void Invalidate() noexcept;
    bool IsValid() noexcept;
};