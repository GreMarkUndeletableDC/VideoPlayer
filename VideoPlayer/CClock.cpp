#include "pch.h"
#include "CClock.h"

const static LARGE_INTEGER QpcFrequency{ []() noexcept
    {
        LARGE_INTEGER Dummy, Freq;
        NtQueryPerformanceCounter(&Dummy, &Freq);
        return Freq;
    }() };

void CClock::Set(TSecond pts, LARGE_INTEGER Qpc) noexcept
{
    const eck::CSrwWriteGuard _{ m_Lock };
    m_pts = pts;
    m_Qpc = Qpc;
}

void CClock::Set(TSecond pts) noexcept
{
    LARGE_INTEGER Qpc;
    NtQueryPerformanceCounter(&Qpc, nullptr);
    Set(pts, Qpc);
}

void CClock::SetForward(TSecond pts) noexcept
{
    LARGE_INTEGER Qpc;
    NtQueryPerformanceCounter(&Qpc, nullptr);

    const eck::CSrwWriteGuard _{ m_Lock };
    if (InternalIsValid() && pts <= m_pts)
        return;
    m_pts = pts;
    m_Qpc = Qpc;
}

TSecond CClock::Get() noexcept
{
    const eck::CSrwReadGuard _{ m_Lock };
    if (!InternalIsValid())
        return None;

    LARGE_INTEGER Qpc;
    NtQueryPerformanceCounter(&Qpc, nullptr);
    return m_pts + (double(Qpc.QuadPart - m_Qpc.QuadPart) / QpcFrequency.QuadPart);
}

void CClock::Invalidate() noexcept
{
    const eck::CSrwWriteGuard _{ m_Lock };
    m_pts = None;
}

bool CClock::IsValid() noexcept
{
    const eck::CSrwReadGuard _{ m_Lock };
    return InternalIsValid();
}