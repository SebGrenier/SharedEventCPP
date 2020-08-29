#include "GlobalMutex.h"
#include <windows.h>
#include "Utils.h"

GlobalMutex::Lock::Lock(GlobalMutex& m)
    : m_Mutex(m)
{
    m_Mutex.WaitOne();
}

GlobalMutex::Lock::~Lock()
{
    m_Mutex.ReleaseMutex();
}

GlobalMutex::GlobalMutex(const std::string& name)
{
    std::string mutexId = "Global\\Mutex_" + Utils::SanitizeName(name);
    m_MutexHandle = CreateMutexA(nullptr, false, mutexId.c_str());
}

GlobalMutex::~GlobalMutex()
{
    CloseHandle(m_MutexHandle);
}

bool GlobalMutex::WaitOne() const
{
    const DWORD ret = WaitForSingleObject(m_MutexHandle, INFINITE);
    return ret != WAIT_FAILED;
}

bool GlobalMutex::ReleaseMutex() const
{
    const BOOL ret = ::ReleaseMutex(m_MutexHandle);
    return ret != 0;
}

