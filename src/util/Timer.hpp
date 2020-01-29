#pragma once
#include <string>
#include <chrono>
#include <iostream>

namespace Util {
    class Timer
    {
        // - to do: GPU timing as well

        typedef std::chrono::high_resolution_clock Clock;

    public:
        class ActiveTiming
        {
            friend class Timer;
            Timer& timer;
            std::string text;
            Clock::time_point startTime;

            ActiveTiming(Timer& timer, const std::string& text)
                : timer{ timer }
                , text { text }
                , startTime{ Clock::now() }
            {}

        public:
            ~ActiveTiming()
            {
                auto endTime = Clock::now();
                auto time = endTime - startTime;
                auto duration = time / std::chrono::milliseconds(1);
                std::cout << text << " took " << duration << " ms\n";
            }
        };

        ActiveTiming Begin(std::string text)
        {
            return { *this, text };
        }

        void EndFrame();
    };
}
