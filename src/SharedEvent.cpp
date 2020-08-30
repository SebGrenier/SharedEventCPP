#include "SharedEvent.h"
#include "Utils.h"

#include <Windows.h>
#include <algorithm>
#include <sstream>

TransactionEvent::TransactionEvent(TransactionMessageType type)
{
    this->type = type;
    startDate = 0;
    startExclusive = false;
    endDate = 0;
    endExclusive = false;
}

std::ostream& operator<<(std::ostream& s, TransactionEvent message)
{
    std::string typeString;
    switch (message.type)
    {
    case TransactionMessageType::TransactionsAdded:
        typeString = "Added";
        break;
    case TransactionMessageType::TransactionsRemoved:
        typeString = "Removed";
        break;
    case TransactionMessageType::TransactionsCleared:
        typeString = "Cleared";
        break;
    }
    s << "(" << typeString << ") " << (message.startExclusive ? "]" : "[") << message.startDate << ", " << message.endDate << (message.endExclusive ? "[" : "]");
    return s;
}

SharedEvent::SharedEvent(const std::string& name, int maximumListeners)
    : m_MaximumListeners(maximumListeners)
{
    m_Name = "SHAREDEVENT_" + Utils::SanitizeName(name);

    try
    {
        const auto registrationLockName = GetRegistrationLockName();
        m_RegistrationLock = new GlobalMutex(registrationLockName);

        const auto registrationMapName = GetRegistrationMapName();
        m_RegistrationMapHandle = CreateFileMappingA(
            INVALID_HANDLE_VALUE,
            NULL,
            PAGE_EXECUTE_READWRITE,
            0, // High 32 bits of 64 bits
            4 + m_MaximumListeners * 4, // Low 32 bits of 64 bits
            registrationMapName.c_str()
        );
        if (m_RegistrationMapHandle == nullptr)
        {
            Dispose();
            return;
        }

        const auto eventsMapName = GetEventsMapName();
        m_EventsMapHandle = CreateFileMappingA(
            INVALID_HANDLE_VALUE,
            NULL,
            PAGE_EXECUTE_READWRITE,
            0, // High 32 bits of 64 bits
            sizeof(TransactionEvent), // Low 32 bits of 64 bits
            eventsMapName.c_str()
        );
        if (m_EventsMapHandle == nullptr)
        {
            Dispose();
            return;
        }

        RegisterSelf();
    }
    catch (...)
    {
        Dispose();
        throw;
    }
}

SharedEvent::~SharedEvent()
{
    Dispose();
}


void SharedEvent::Dispose()
{
    if (m_Disposed)
        return;

    if (m_Registered)
        UnregisterSelf();

    delete m_RegistrationLock;
    CloseHandle(m_RegistrationMapHandle);
    m_RegistrationMapHandle = nullptr;
    CloseHandle(m_EventsMapHandle);
    m_EventsMapHandle = nullptr;

    m_EventCallbacks.clear();

    m_Disposed = true;
}

void SharedEvent::Emit(TransactionEvent message, bool suppressSelfHandler)
{
    bool modifiedList = false;

    try
    {
        GlobalMutex::Lock lock(*m_RegistrationLock);

        // Wait for every read to complete
        auto ids = ReadHandleIds();
        std::vector<HANDLE> readWaitHandles;
        std::vector<HANDLE> nonNullReadWaitHandles;
        for (auto id : ids)
        {
            HANDLE readWaitHandle = GetReadWaitHandle(id, false);
            readWaitHandles.push_back(readWaitHandle);
            if (readWaitHandle != nullptr)
                nonNullReadWaitHandles.push_back(readWaitHandle);
        }
        WaitForMultipleObjects(nonNullReadWaitHandles.size(), nonNullReadWaitHandles.data(), true, INFINITE);

        // Update the memory map with the new message
        WriteEvent(message);

        // Notify listeners
        for (int i = 0; i < ids.size(); )
        {
            HANDLE readWaitHandle = nullptr;
            HANDLE emitHandle = nullptr;
            try
            {
                int listenerId = ids[i];
                readWaitHandle = readWaitHandles[i];

                if (suppressSelfHandler && listenerId == m_LocalId)
                {
                    ++i;
                    CloseHandle(readWaitHandle);
                    continue;
                }

                emitHandle = GetEmitWaitHandle(listenerId, false);
                if (emitHandle == nullptr)
                {
                    // The listener's wait handle is gone. This means that the listener died
                    // without unregistering themselves.
                    ids.erase(ids.begin() + i);
                    readWaitHandles.erase(readWaitHandles.begin() + i);
                    modifiedList = true;
                }
                else
                {
                    ResetEvent(readWaitHandle);
                    SetEvent(emitHandle);
                    ++i;
                }
            }
            catch (...)
            {
            }

            if (emitHandle != nullptr)
                CloseHandle(emitHandle);
            if (readWaitHandle != nullptr)
                CloseHandle(readWaitHandle);
        }

        if (modifiedList)
        {
            WriteHandleIds(ids);
        }
    }
    catch (...)
    {
    }
}

void SharedEvent::RegisterCallback(const std::function<void(TransactionEvent)>& callback)
{
    m_EventCallbacks.push_back(callback);
}

void SharedEvent::RegisterSelf()
{
    try
    {
        GlobalMutex::Lock lock(*m_RegistrationLock);

        auto ids = ReadHandleIds();
        if (ids.size() >= m_MaximumListeners)
        {
            throw std::exception("Cannot register self with SharedEvent - no more room in the shared memory's registration list. Increase 'maximumListeners'.");
        }

        m_LocalId = FindNextHandleId(ids);
        ids.push_back(m_LocalId);
        std::sort(ids.begin(), ids.end());

        m_EmitWaitHandle = GetEmitWaitHandle(m_LocalId, true);
        if (m_EmitWaitHandle == nullptr)
        {
            throw std::exception("Cannot register self with SharedEvent - Emit wait handle already exists.");
        }

        m_ReadWaitHandle = GetReadWaitHandle(m_LocalId, true);
        if (m_ReadWaitHandle == nullptr)
        {
            throw std::exception("Cannot register self with SharedEvent - Read wait handle already exists.");
        }

        // Set the read handle to signaled so writer doesn't wait
        SetEvent(m_ReadWaitHandle);

        m_ReadThreadRunning = true;
        m_ReadThread = new std::thread([this] { ReadThreadFunc(); });

        WriteHandleIds(ids);
        m_Registered = true;
    }
    catch (...)
    {
        UnregisterSelf();
        throw;
    }
}

void SharedEvent::UnregisterSelf()
{
    try
    {
        GlobalMutex::Lock lock(*m_RegistrationLock);

        auto ids = ReadHandleIds();

        if (m_LocalId != -1)
        {
            auto it = std::find(ids.begin(), ids.end(), m_LocalId);
            if (it != ids.end())
                ids.erase(it);
            m_LocalId = -1;

            WriteHandleIds(ids);
        }

        if (m_ReadThread != nullptr)
        {
            std::atomic_store_explicit(&m_ReadThreadRunning, false, std::memory_order_release);
            if (m_EmitWaitHandle != nullptr)
                SetEvent(m_EmitWaitHandle);
            m_ReadThread->join();
            delete m_ReadThread;
        }

        if (m_EmitWaitHandle != nullptr)
        {
            CloseHandle(m_EmitWaitHandle);
            m_EmitWaitHandle = nullptr;
        }

        if (m_ReadWaitHandle != nullptr)
        {
            CloseHandle(m_ReadWaitHandle);
            m_ReadWaitHandle = nullptr;
        }

        m_Registered = false;
    }
    catch (...)
    {
        m_Registered = false;
        throw;
    }
}

void SharedEvent::ReadThreadFunc()
{
    bool expected = true;
    while (m_ReadThreadRunning.compare_exchange_strong(expected, true, std::memory_order_acquire))
    {
        const auto ret = WaitForSingleObject(m_EmitWaitHandle, INFINITE);
        if (ret != WAIT_FAILED)
        {
            if (m_ReadThreadRunning.compare_exchange_strong(expected, true, std::memory_order_acquire))
            {
                OnWaitEventTriggered();
            }
        }

        expected = true;
    }
}

void SharedEvent::OnWaitEventTriggered()
{
    try
    {
        const TransactionEvent message = ReadEvent();
        for (const auto& event_callback : m_EventCallbacks)
        {
            event_callback(message);
        }
    }
    catch (...)
    {
    }

    HANDLE readHandle = GetReadWaitHandle(m_LocalId, false);
    if (readHandle == nullptr)
        return;
    SetEvent(readHandle);
    CloseHandle(readHandle);
}

TransactionEvent SharedEvent::ReadEvent() const
{
    LPVOID buffer = MapViewOfFile(m_EventsMapHandle, FILE_MAP_READ, 0, 0, sizeof(TransactionEvent));
    TransactionEvent message(TransactionMessageType::TransactionsAdded);
    TransactionEvent* transactionBuffer = static_cast<TransactionEvent*>(buffer);
    CopyMemory(&message, transactionBuffer, sizeof(TransactionEvent));
    UnmapViewOfFile(buffer);
    return message;
}

void SharedEvent::WriteEvent(TransactionEvent message) const
{
    LPVOID buffer = MapViewOfFile(m_EventsMapHandle, FILE_MAP_WRITE, 0, 0, sizeof(TransactionEvent));
    CopyMemory(buffer, &message, sizeof(TransactionEvent));
    UnmapViewOfFile(buffer);
}

std::vector<int> SharedEvent::ReadHandleIds() const
{
    LPVOID buffer = MapViewOfFile(m_RegistrationMapHandle, FILE_MAP_READ, 0, 0, 4 + m_MaximumListeners * 4);
    int* integerBuffer = static_cast<int*>(buffer);

    int numHandles = integerBuffer[0];
    std::vector<int> ids;
    for (int i = 0; i < numHandles; ++i)
    {
        ids.push_back(integerBuffer[i + 1]);
    }

    UnmapViewOfFile(buffer);

    return ids;
}

void SharedEvent::WriteHandleIds(const std::vector<int>& ids) const
{
    LPVOID buffer = MapViewOfFile(m_RegistrationMapHandle, FILE_MAP_WRITE, 0, 0, 4 + m_MaximumListeners * 4);
    int* integerBuffer = static_cast<int*>(buffer);

    integerBuffer[0] = ids.size();
    for (int i = 0; i < ids.size(); ++i)
    {
        integerBuffer[i + 1] = ids[i];
    }

    UnmapViewOfFile(buffer);
}

int SharedEvent::FindNextHandleId(const std::vector<int>& ids)
{
    int currentId = 0;
    for (; currentId < ids.size(); ++currentId)
    {
        if (ids[currentId] != currentId)
            break;
    }

    return currentId;
}

HANDLE SharedEvent::GetEmitWaitHandle(int waitHandleId, bool acquire) const
{
    std::string handleName = GetEmitWaitHandleNameById(waitHandleId);
    HANDLE waitHandle = CreateEventA(nullptr, false, false, handleName.c_str());
    DWORD lastError = GetLastError();
    bool alreadyExisted = lastError == ERROR_ALREADY_EXISTS;
    if ((acquire && alreadyExisted) || (!acquire && !alreadyExisted))
    {
        CloseHandle(waitHandle);
        return nullptr;
    }
    return waitHandle;
}

HANDLE SharedEvent::GetReadWaitHandle(int waitHandleId, bool acquire) const
{
    std::string handleName = GetReadWaitHandleNameById(waitHandleId);
    HANDLE waitHandle = CreateEventA(nullptr, true, false, handleName.c_str());
    DWORD lastError = GetLastError();
    bool alreadyExisted = lastError == ERROR_ALREADY_EXISTS;
    if ((acquire && alreadyExisted) || (!acquire && !alreadyExisted))
    {
        CloseHandle(waitHandle);
        return nullptr;
    }
    return waitHandle;
}

std::string SharedEvent::GetRegistrationLockName() const
{
    return m_Name + "_RL";
}

std::string SharedEvent::GetRegistrationMapName() const
{
    return m_Name + "_RM";
}

std::string SharedEvent::GetEventsMapName() const
{
    return m_Name + "_EM";
}

std::string SharedEvent::GetEmitWaitHandleNameById(int waitHandleId) const
{
    std::stringstream ss;
    ss << m_Name << "_" << waitHandleId;
    return ss.str();
}

std::string SharedEvent::GetReadWaitHandleNameById(int waitHandleId) const
{
    std::stringstream ss;
    ss << m_Name << "_READ_" << waitHandleId;
    return ss.str();
}



