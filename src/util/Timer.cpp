#include "Timer.hpp"

namespace Util {

    void Timer::Timings::DurationVector::Insert(Duration d, size_t maxLength)
    {
        if (durations.size() <= nextIndex) durations.resize(nextIndex + 1u);
        durations[nextIndex] = d;
        nextIndex = (nextIndex + 1ULL) % maxLength;
    }

    Timer::GpuQuery Timer::AllocateGpuQuery()
    {
        GpuQuery q;
        if (freeGpuQueries.empty())
        {
            glCreateQueries(GL_TIMESTAMP, 1, &q);
            static unsigned count = 0u;
            //std::cout << "Created " << ++count << " timestamp queries." << std::endl;
        }
        else
        {
            q = freeGpuQueries.top();
            freeGpuQueries.pop();
        }
        return q;
    }
    void Timer::FreeGpuQuery(Timer::GpuQuery& q)
    {
        freeGpuQueries.push(q);
        q = 0u;
    }

    void Timer::StartTiming(Timer::ActiveTiming& t)
    {
        t.startTime = Clock::now();
        t.queries[0] = AllocateGpuQuery();
        t.queries[1] = AllocateGpuQuery();
        pendingGpuQueries.push({ t.nameRef, t.queries[0], t.queries[1], frame, t.meta });
        glQueryCounter(t.queries[0], GL_TIMESTAMP);
    }

    void Timer::EndTiming(Timer::ActiveTiming& t)
    {
        glQueryCounter(t.queries[1], GL_TIMESTAMP);
        auto endTime = Clock::now();
        auto time = endTime - t.startTime;
        auto duration = 1e-6 * (time / std::chrono::microseconds(1));
        //std::cout << refToName[t.nameRef] << " took " << duration * 1e3 << " ms" << std::endl;
        timings[t.nameRef].cpuTimes.Insert({ duration, frame, t.meta }, maxTimesStored);
    }

    void Timer::EndFrame()
    {
        auto num = 0u;
        while (!pendingGpuQueries.empty())
        {
            auto& q = pendingGpuQueries.front();
            GLuint64 start = 0u, end = 0u;
            glGetQueryObjectui64v(q.gpuQueries[1], GL_QUERY_RESULT_NO_WAIT, &end);
            if (0u == end) break; // not yet ready
            glGetQueryObjectui64v(q.gpuQueries[0], GL_QUERY_RESULT, &start);

            const auto gpuDuration = (end - start) * 1e-9;
            timings[q.nameRef].gpuTimes.Insert({ gpuDuration, q.frame, q.meta }, maxTimesStored);

            FreeGpuQuery(q.gpuQueries[0]);
            FreeGpuQuery(q.gpuQueries[1]);
            pendingGpuQueries.pop();

            //std::cout << "GPU query \"" << refToName[q.nameRef] << "\": " << gpuDuration * 1e3 << " ms (next index: " << timings[q.nameRef].nextGpuIndex << ")" << std::endl;
            ++num;
        }

        // - testing:
        //std::cout << "Handled " << num << " queries. Pending queries: " << pendingGpuQueries.size() << std::endl;
    }
}
