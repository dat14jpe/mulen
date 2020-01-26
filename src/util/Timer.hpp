#pragma once
#include <string>
#include <chrono>
#include <iostream>

namespace Util {
    class Timer
    {
        // - to do: GPU timing as well

        const std::string text;
        typedef std::chrono::high_resolution_clock Clock;
        Clock::time_point startTime;

    public:
        Timer(std::string text) : text{ text } 
        {
            startTime = Clock::now();
        }
        ~Timer()
        {
            auto endTime = Clock::now();
            auto time = endTime - startTime;
            auto duration = time / std::chrono::milliseconds(1);
            std::cout << text << " took " << duration << " ms\n";
        }
    };
}
