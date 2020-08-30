// TestSharedEvent.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <atomic>
#include <signal.h>
#include "SharedEvent.h"

void SimpleEmitTest()
{
    std::atomic<bool> messageReceived = false;

    SharedEvent sharedEvent("TestSharedEvent");
    sharedEvent.RegisterCallback([&messageReceived](TransactionEvent evt) -> void
        {
            std::cout << "Message received." << std::endl;
            std::atomic_store_explicit(&messageReceived, true, std::memory_order_release);
        });

    TransactionEvent message(TransactionMessageType::TransactionsAdded);
    std::cout << "Sending message" << std::endl;
    sharedEvent.Emit(message);

    bool expected = false;
    while (messageReceived.compare_exchange_weak(expected, false, std::memory_order_acquire));
}

void ReceiveEventFromExternal()
{
    std::atomic<int> messageCounter = 0;

    SharedEvent sharedEvent("TestSharedEventExternal");
    sharedEvent.RegisterCallback([&messageCounter](TransactionEvent evt) -> void
        {
            std::cout << "Message received: " << evt << std::endl;
            messageCounter.fetch_add(1);
        });

    std::cout << "Waiting for messages" << std::endl;
    int expected = 10;
    while (!messageCounter.compare_exchange_strong(expected, 10, std::memory_order_acquire))
    {
        expected = 10;
    };
}

void ReceiveEventFromExternalCrash()
{
    std::atomic<int> messageCounter = 0;

    SharedEvent sharedEvent("TestSharedEventExternal");
    sharedEvent.RegisterCallback([&messageCounter](TransactionEvent evt) -> void
        {
            raise(SIGSEGV);
        });

    std::cout << "Waiting for messages" << std::endl;
    int expected = 10;
    while (!messageCounter.compare_exchange_strong(expected, 10, std::memory_order_acquire))
    {
        expected = 10;
    };
}

int main()
{
    ReceiveEventFromExternalCrash();
}
