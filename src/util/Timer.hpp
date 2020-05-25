#pragma once
#include <string>
#include <chrono>
#include <iostream>
#include "GLObject.hpp"
#include <stack>
#include <unordered_map>
#include <queue>
#include <glm/glm.hpp>

namespace Util {
    class Timer
    {
    public:
        struct DurationMeta
        {
            double factor;
        };

        struct Duration
        {
            double duration;
            int frame;
            DurationMeta meta;
        };

        typedef std::vector<Duration>::size_type NameRef;

    private:
        struct Timings
        {
            class DurationVector
            {
                friend class Timer;
                std::vector<Duration> durations;
                size_t nextIndex = 0u;

            public:
                void Insert(Duration d, size_t maxLength);

                size_t Size() const
                {
                    return durations.size();
                }

                const Duration& operator[](int i)
                {
                    const auto size = static_cast<int>(durations.size());
                    return durations[(nextIndex + i + size - 1) % size];
                }

                double Average(size_t window)
                {
                    if (!Size()) return 0.0;
                    const auto num = static_cast<int>(glm::min(window, Size()));
                    auto sum = 0.0;
                    for (auto i = 0; i < num; ++i)
                    {
                        sum += (*this)[-i].duration;
                    }
                    auto duration = sum / double(num);
                    return duration;
                }
            } cpuTimes, gpuTimes;
            
            // - maybe storing a few sets of query objects here would actually be better
            // (fully dynamic handling might be overkill - do we ever really need e.g. a latency of more than 4 frames?)
        };
        size_t maxTimesStored = 128u; // - arbitrary (to do: make this configurable)

        std::vector<Timings> timings;
        std::unordered_map<std::string, NameRef> nameToRef;
        std::vector<std::string> refToName;

        typedef GLuint GpuQuery;
        std::stack<GpuQuery> freeGpuQueries;
        struct PendingGpuQuery
        {
            NameRef nameRef;
            GpuQuery gpuQueries[2];
            int frame;
            DurationMeta meta;
        };
        std::queue<PendingGpuQuery> pendingGpuQueries;
        // - to do: sync objects with associated numbers of pending queries

        GpuQuery AllocateGpuQuery();
        void FreeGpuQuery(GpuQuery&);

        int frame = 0;


    public:
        class ActiveTiming;
        void StartTiming(ActiveTiming&);
        void EndTiming(ActiveTiming&);

        size_t GetFrame() const { return frame; }

        NameRef NameToRef(const std::string& name)
        {
            auto it = nameToRef.find(name);
            if (it != nameToRef.end()) return it->second;
            const auto ref = timings.size();
            timings.push_back({});
            refToName.push_back(name);
            nameToRef[name] = ref;
            return ref;
        }
        const std::string& RefToName(NameRef ref)
        {
            return refToName[ref];
        }
        size_t GetNumNames() const { return refToName.size(); }
        Timings& GetTimings(NameRef ref)
        {
            return timings[ref];
        }
        Timings& GetTimings(const std::string& name)
        {
            return GetTimings(NameToRef(name));
        }

        typedef std::chrono::high_resolution_clock Clock;

        class ActiveTiming
        {
            friend class Timer;
            Timer& timer;
            NameRef nameRef;
            DurationMeta meta;
            Clock::time_point startTime;
            GLuint queries[2];

            ActiveTiming(Timer& timer, const std::string& name, const DurationMeta& meta)
                : timer{ timer }
                , nameRef { timer.NameToRef(name) }
                , meta{ meta }
            {
                timer.StartTiming(*this);
            }

        public:
            ~ActiveTiming()
            {
                timer.EndTiming(*this);
            }
        };

        ActiveTiming Begin(const std::string& name, DurationMeta meta = { 1.0 })
        {
            return { *this, name, meta };
        }

        void EndFrame();
    };
}
