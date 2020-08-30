// TestSharedEvent.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <atomic>
#include "SharedEvent.h"

int main()
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
