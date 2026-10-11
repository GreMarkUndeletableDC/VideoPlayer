#pragma once
template<class T>
class CRingBuffer
{
private:
    std::unique_ptr<T[]> m_pBuffer{};
    size_t m_cCapacity{};
    size_t m_cSize{};
    size_t m_idxHead{};
    size_t m_idxTail{};

    eck::CSrwLock m_Lock{};
    eck::CConditionVariable m_CvNotEmpty{};
    eck::CConditionVariable m_CvNotFull{};

    BOOL m_bClosed{};

    void InternalPush(T x) noexcept
    {
        m_pBuffer[m_idxTail] = std::move(x);
        m_idxTail = (m_idxTail + 1) % m_cCapacity;
        ++m_cSize;
    }
    void InternalPop(T& x) noexcept
    {
        x = std::move(m_pBuffer[m_idxHead]);
        m_idxHead = (m_idxHead + 1) % m_cCapacity;
        --m_cSize;
    }
    void InternalClear() noexcept
    {
        for (size_t i = 0; i < m_cSize; ++i)
            m_pBuffer[(m_idxHead + i) % m_cCapacity] = T{};
        m_cSize = 0;
        m_idxHead = 0;
        m_idxTail = 0;
    }
public:
    CRingBuffer(size_t cCapacity) noexcept :
        m_pBuffer{ std::make_unique<T[]>(cCapacity) },
        m_cCapacity{ cCapacity }
    {
    }

    BOOL Push(T x) noexcept
    {
        m_Lock.EnterWrite();

        while (m_cSize == m_cCapacity && !m_bClosed)
            m_CvNotFull.Sleep(m_Lock.GetPointer(), FALSE, nullptr);

        if (m_bClosed)
        {
            m_Lock.LeaveWrite();
            return FALSE;
        }
        InternalPush(std::move(x));

        m_Lock.LeaveWrite();
        m_CvNotEmpty.WakeAll();
        return TRUE;
    }

    BOOL TryPush(T x) noexcept
    {
        m_Lock.EnterWrite();
        if (m_cSize < m_cCapacity && !m_bClosed)
        {
            InternalPush(std::move(x));
            m_Lock.LeaveWrite();
            m_CvNotEmpty.WakeAll();
            return TRUE;
        }
        m_Lock.LeaveWrite();
        return FALSE;
    }

    BOOL Pop(T& x) noexcept
    {
        m_Lock.EnterWrite();

        while (m_cSize == 0 && !m_bClosed)
            m_CvNotEmpty.Sleep(m_Lock.GetPointer(), FALSE, nullptr);
        if (m_bClosed)
        {
            m_Lock.LeaveWrite();
            return FALSE;
        }
        InternalPop(x);

        m_Lock.LeaveWrite();
        m_CvNotFull.WakeAll();
        return TRUE;
    }
    BOOL TryPop(T& x) noexcept
    {
        m_Lock.EnterWrite();
        if (m_cSize > 0 && !m_bClosed)
        {
            InternalPop(x);
            m_Lock.LeaveWrite();
            m_CvNotFull.WakeAll();
            return TRUE;
        }
        m_Lock.LeaveWrite();
        return FALSE;
    }

    size_t Size() noexcept
    {
        eck::CSrwReadGuard _{ m_Lock };
        return m_cSize;
    }
    size_t Capacity() noexcept
    {
        eck::CSrwReadGuard _{ m_Lock };
        return m_cCapacity;
    }

    void Clear() noexcept
    {
        eck::CSrwWriteGuard _{ m_Lock };
        InternalClear();
        m_CvNotFull.WakeAll();
    }

    void Close() noexcept
    {
        {
            eck::CSrwWriteGuard _{ m_Lock };
            m_bClosed = TRUE;
            InternalClear();
        }
        m_CvNotFull.WakeAll();
        m_CvNotEmpty.WakeAll();
    }

    void Open() noexcept
    {
        eck::CSrwWriteGuard _{ m_Lock };
        m_bClosed = FALSE;
    }
};