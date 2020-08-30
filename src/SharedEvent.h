#pragma once
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <functional>
#include <iostream>


#include "GlobalMutex.h"

enum class TransactionMessageType
{
    TransactionsAdded,
    TransactionsCleared,
    TransactionsRemoved
};

struct TransactionEvent
{
public:
    TransactionMessageType type;
    long long startDate;
    bool startExclusive;
    long long endDate;
    bool endExclusive;

    TransactionEvent(TransactionMessageType type);

    friend std::ostream& operator << (std::ostream& s, TransactionEvent message);
};

class SharedEvent
{
public:
    explicit SharedEvent(const std::string& name, int maximumListeners = 1024);
    ~SharedEvent();
    void Dispose();

    void Emit(TransactionEvent message, bool suppressSelfHandler = false);

    void RegisterCallback(const std::function<void(TransactionEvent)>& callback);

private:
    std::string GetRegistrationLockName() const;
    std::string GetRegistrationMapName() const;
    std::string GetEventsMapName() const;
    std::string GetEmitWaitHandleNameById(int waitHandleId) const;
    std::string GetReadWaitHandleNameById(int waitHandleId) const;

    void RegisterSelf();
    void UnregisterSelf();

    std::vector<int> ReadHandleIds() const;
    void WriteHandleIds(const std::vector<int>& ids) const;
    static int FindNextHandleId(const std::vector<int>& ids);

    HANDLE GetEmitWaitHandle(int waitHandleId, bool acquire) const;
    HANDLE GetReadWaitHandle(int waitHandleId, bool acquire) const;

    void ReadThreadFunc();
    void OnWaitEventTriggered();

    TransactionEvent ReadEvent() const;
    void WriteEvent(TransactionEvent) const;

    std::string m_Name;
    int m_MaximumListeners {1024};

    int m_LocalId{ -1 };
    GlobalMutex* m_RegistrationLock {nullptr};
    HANDLE m_RegistrationMapHandle {nullptr};
    HANDLE m_EventsMapHandle {nullptr};
    HANDLE m_EmitWaitHandle {nullptr};
    HANDLE m_ReadWaitHandle {nullptr};

    std::atomic<bool> m_ReadThreadRunning {false};
    std::thread *m_ReadThread {nullptr};

    bool m_Disposed{ false };
    bool m_Registered{ false };

    std::vector<std::function<void(TransactionEvent)>> m_EventCallbacks;
};

