#pragma once
#include <Windows.h>
#include <string>

class GlobalMutex
{
public:
    class Lock
    {
    public:
        explicit Lock(GlobalMutex& m);
        ~Lock();

        Lock(const Lock& other) = delete;
        Lock(const Lock&& other) = delete;
        Lock& operator = (const Lock& other) = delete;
        Lock& operator = (const Lock&& other) = delete;
    private:
        GlobalMutex& m_Mutex;
    };


    explicit GlobalMutex(const std::string& name);
    ~GlobalMutex();

    bool WaitOne() const;
    bool ReleaseMutex() const;

    // Deleted functions
    GlobalMutex(const GlobalMutex& other) = delete;
    GlobalMutex(const GlobalMutex&& other) = delete;
    GlobalMutex& operator = (const GlobalMutex& other) = delete;
    GlobalMutex& operator = (const GlobalMutex&& other) = delete;

private:
    HANDLE m_MutexHandle;
};
