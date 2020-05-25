#pragma once
#include <vector>
#include "Object.hpp"
#include "atmosphere/Atmosphere.hpp"

namespace Mulen {

    class App;


    //
    // Frame-count based benchmarker.
    // Runs a fixed sequence of frames, which may take more or less time
    // depending on the computer's hardware performance.
    //

    class Benchmarker
    {
        App& app;
        enum class Mode
        {
            Inactive,
            Recording,
            Benchmarking,
        } mode = Mode::Inactive;

        struct Frame
        {
            Object::Position cameraPosition;
            Object::Orientation cameraOrientation;
            double cameraFovy;
            double animationTime, lightTime;
            // - to do: more, if relevant. Generator sync points?
        };
        struct Configuration
        {
            std::string fileName;
            std::vector<Frame> sequence;
            int warmUpFrames = 0;
            glm::ivec2 resolution;
            int gpuMemBudgetMiB;
            Atmosphere::Atmosphere::UpdateParams atmUpdateParams;
            // - possible to do: more data

        };
        std::vector<Configuration> configs;
        size_t currentConfig = 0, currentFrame = 0, warmUpFrame = 0u;
        size_t profilerStartFrame = 0, lastProfilerFrame = 0;

        struct ResultsItem
        {
            std::vector<unsigned> durations; // in microseconds (because full float precision is unnecessary to output)
            int lastFrame = -1;
        };
        typedef std::vector<ResultsItem> Results;
        Results results; // indexed by NameRefs from the timer


        // - to do: ongoing profiler values when benchmarking

        Configuration recording; // for recording a new path

        void OnBenchmarkingFrame(double& dt);
        void OnRecordingFrame(double& dt);

        void SaveResults(const std::string&, Results&);
        void Load(const std::string& dirPath); // load configuration(s) from file(s)

    public:
        Benchmarker(App&);
        void StartRecording();
        void StopRecording();
        void StartBenchmark();
        void OnFrame(double& dt);
        void StopBenchmark();

        bool IsInactive() const { return mode == Mode::Inactive; }
        bool IsRecording() const { return mode == Mode::Recording; }
        bool IsBenchmarking() const { return mode == Mode::Benchmarking; }
    };
}
