#include <stdio.h>
#include <iostream>
#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <new>
#include <thread>
#include <vector>
#include <memory>
#include <string>
#include "MPSCQ.h" 



//Example Lock Free Queue
int main()
{
    MPSCQ<int> mpscq(1024);
    MPSCQ<std::string> logServer(1024);
    std::atomic<bool> shutdown{ false };

    // consumer pop value produced by producer threads
    std::thread consumerThread([&] {
        while (!shutdown.load(std::memory_order_acquire)) {
            int val;
            while (mpscq.pop(val))
            {    //Adding log slows down, but adding for demo
                logServer.push("[consumer] popped: " + std::to_string(val) + "\n");
            }
        }

        // optional: drain at termination
        int val;
        while (mpscq.pop(val)) {
            logServer.push("[consumer] final drain: " + std::to_string(val) + "\n");
        }
        });

    std::thread logServerThread([&] {
        std::string logVal;
        while (!shutdown.load(std::memory_order_acquire))
        {
            while (logServer.pop(logVal))
            {
                std::cout << logVal;
            }
        }

        while (logServer.pop(logVal))
        {
            std::cout << logVal;
        }
        });

    // make multiple producers
    const int producerCount = 5;
    std::vector<std::thread> producers;
    producers.reserve(producerCount);

    //Producers pushing in values 1 second interval
    for (int i = 0; i < producerCount; ++i) {
        producers.emplace_back([&,i] {
            auto interval = std::chrono::seconds(1);
            auto nextTime = std::chrono::steady_clock::now();

            // give each producer its own value range
            int value = (i+1)*100; // 1000, 2000, ...

            while (!shutdown.load(std::memory_order_acquire)) {
                std::this_thread::sleep_until(nextTime);
                if (shutdown.load(std::memory_order_acquire))
                    break;
                //Adding log slows down, but adding for demo
                logServer.push("[producer " + std::to_string(i) + "] pushing :" + std::to_string(value) + "\n");
                auto pushed = mpscq.push(value);
                while (!pushed)
                {
                    pushed = mpscq.push(value);
                }
                ++value;

                nextTime += interval;
            }
            });
    }

    // let everything run for 60s
    std::this_thread::sleep_for(std::chrono::seconds(10));
    shutdown.store(true, std::memory_order_release);

    // join producers
    for (auto& th : producers) {
        th.join();
    }
    consumerThread.join();
    logServerThread.join();

    std::cout << "Done.\n";
    return 0;
}