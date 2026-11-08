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

    bool m_bClosed{};

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

    bool Push(T&& x) noexcept
    {
        m_Lock.EnterWrite();

        while (m_cSize == m_cCapacity && !m_bClosed)
            m_CvNotFull.Sleep(m_Lock.GetPointer(), false, nullptr);

        if (m_bClosed)
        {
            m_Lock.LeaveWrite();
            return false;
        }
        InternalPush(std::move(x));

        m_Lock.LeaveWrite();
        m_CvNotEmpty.WakeAll();
        return true;
    }

    bool TryPush(T&& x) noexcept
    {
        m_Lock.EnterWrite();
        if (m_cSize < m_cCapacity && !m_bClosed)
        {
            InternalPush(std::move(x));
            m_Lock.LeaveWrite();
            m_CvNotEmpty.WakeAll();
            return true;
        }
        m_Lock.LeaveWrite();
        return false;
    }

    bool Pop(T& x) noexcept
    {
        m_Lock.EnterWrite();

        while (m_cSize == 0 && !m_bClosed)
            m_CvNotEmpty.Sleep(m_Lock.GetPointer(), false, nullptr);
        if (m_bClosed)
        {
            m_Lock.LeaveWrite();
            return false;
        }
        InternalPop(x);

        m_Lock.LeaveWrite();
        m_CvNotFull.WakeAll();
        return true;
    }
    bool TryPop(T& x) noexcept
    {
        m_Lock.EnterWrite();
        if (m_cSize > 0 && !m_bClosed)
        {
            InternalPop(x);
            m_Lock.LeaveWrite();
            m_CvNotFull.WakeAll();
            return true;
        }
        m_Lock.LeaveWrite();
        return false;
    }

    size_t Size() noexcept
    {
        const eck::CSrwReadGuard _{ m_Lock };
        return m_cSize;
    }
    size_t Capacity() noexcept
    {
        const eck::CSrwReadGuard _{ m_Lock };
        return m_cCapacity;
    }

    void Clear() noexcept
    {
        {
            const eck::CSrwWriteGuard _{ m_Lock };
            InternalClear();
        }
        m_CvNotFull.WakeAll();
    }

    void Close() noexcept
    {
        {
            const eck::CSrwWriteGuard _{ m_Lock };
            m_bClosed = true;
            InternalClear();
        }
        m_CvNotFull.WakeAll();
        m_CvNotEmpty.WakeAll();
    }

    void Open() noexcept
    {
        const eck::CSrwWriteGuard _{ m_Lock };
        m_bClosed = false;
    }
};