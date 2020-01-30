#pragma once
#include <string>
#include <chrono>
#include <iostream>
#include "GLObject.hpp"

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
            GLuint queries[2];

            ActiveTiming(Timer& timer, const std::string& text)
                : timer{ timer }
                , text { text }
                , startTime{ Clock::now() }
            {
                glCreateQueries(GL_TIMESTAMP, 2, queries);
                glQueryCounter(queries[0], GL_TIMESTAMP);
            }

        public:
            ~ActiveTiming()
            {
                auto endTime = Clock::now();
                auto time = endTime - startTime;
                auto duration = time / std::chrono::milliseconds(1);
                std::cout << text << " took " << duration << " ms";

                glQueryCounter(queries[1], GL_TIMESTAMP);

                GLuint64 start, end;
                glGetQueryObjectui64v(queries[0], GL_QUERY_RESULT, &start);
                glGetQueryObjectui64v(queries[1], GL_QUERY_RESULT, &end);
                auto gpuDuration = (end - start) * 1e-9;
                std::cout << " (" << gpuDuration * 1e3 << " ms GPU)\n";

                glDeleteQueries(2, queries);
            }
        };

        ActiveTiming Begin(std::string text)
        {
            return { *this, text };
        }

        void EndFrame();
    };
}
