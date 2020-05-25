#include "Benchmarker.hpp"
#include "App.hpp"
#include <filesystem>
#include <util/json.hpp>
using nlohmann::json;

namespace Mulen {
    
    static const std::string basePath = "benchmark/";
    static const std::string
        configPath  = basePath + "config/",
        recordPath  = basePath + "record/",
        resultsPath = basePath + "results/";

    Benchmarker::Benchmarker(App& app)
        : app{ app }
    {}

    void Benchmarker::StartBenchmark()
    {
        if (Mode::Inactive != mode) return;
        Load(configPath);
        if (configs.empty()) return; // no benchmark configuration to run
        mode = Mode::Benchmarking;

        currentConfig = currentFrame = warmUpFrame = 0u;
        app.window.SetVSync(false); // V-sync has to be turned off so the GPU doesn't downclock itself
        profilerStartFrame = app.timer.GetFrame();
    }

    void Benchmarker::OnFrame(double& dt)
    {
        switch (mode)
        {
        case Mode::Benchmarking: OnBenchmarkingFrame(dt); break;
        case Mode::Recording: OnRecordingFrame(dt); break;
        }
    }
    void Benchmarker::OnBenchmarkingFrame(double& dt)
    {
        dt = 1.0 / 60.0; // - to do: make this configurable
        if (currentConfig >= configs.size())
        {
            // - to do: store intermediate results to be safe?
            // - to do: move to the next config
            StopBenchmark();
            return;
        }
        auto& config = configs[currentConfig];
        if (currentFrame == 0 && warmUpFrame == 0u) // starting on a new config?
        {
            results.clear();
            std::cout << " init frame of " << config.sequence.size() << std::endl;
            const auto needsReInit = app.gpuMemBudgetMiB != config.gpuMemBudgetMiB;
            app.gpuMemBudgetMiB = config.gpuMemBudgetMiB;
            if (needsReInit)
            {
                app.InitializeAtmosphere(); // - maybe to do: only do this if the GPU memory budget changed?
            }
            // - to do: await updater thread iteration completion, if it's not idle already
            app.renderResolution = config.resolution;
            app.atmUpdateParams = app.atmUpdateParams;
        }

        const auto inWarmUp = warmUpFrame < config.warmUpFrames;
        if (inWarmUp) // still in warm-up?
        {
            ++warmUpFrame;
        }
        if (currentFrame >= config.sequence.size()) // done with this configuration?
        {
            // - to do: await remaining profiler values for this pass before continuing?
            // (it would really be better to just receive them later, though)

            SaveResults(config.fileName, results);

            // Advance to the next configuration.
            warmUpFrame = currentFrame = 0u;
            ++currentConfig;
            OnBenchmarkingFrame(dt);
            return;
        }

        if (ImGui::Begin("Benchmarker"))
        {
            if (ImGui::Button("Abort benchmark"))
            {
                StopBenchmark();
                return;
            }
            ImGui::Spacing();
            // - to do: show current stage and so
            //ImGui::Text("Frame %4d of %d.", currentFrame + 1u, sequence.size());
            ImGui::Text("Progress:");
            for (size_t i = 0u; i < configs.size(); ++i)
            {
                auto& config = configs[i];
                auto progress = unsigned(100.0 * currentFrame / config.sequence.size());
                if (currentConfig < i) progress = 0;
                if (currentConfig > i) progress = 100;
                auto isWarmUp = currentConfig == i && inWarmUp;
                ImGui::Text(" %3d%% %5dx%4d %s", progress, config.resolution.x, config.resolution.y, isWarmUp ? "(warm-up)" : "");
            }
        }
        ImGui::End();

        auto& frame = config.sequence[currentFrame];

        app.camera.SetPosition(frame.cameraPosition);
        app.camera.SetOrientation(frame.cameraOrientation);
        app.camera.SetFovy(frame.cameraFovy);
        app.atmosphere.SetAnimationTime(frame.animationTime);
        app.atmosphere.SetLightTime(frame.lightTime);
        // - to do: set time difference to benchmark value. Or maybe just set absolute times instead (yeah)

        if (!inWarmUp)
        {
            // Get new profiler values, if available.
            auto& profiler = app.timer;
            auto num = profiler.GetNumNames();
            if (results.size() < num) results.resize(num);
            for (size_t i = 0u; i < num; ++i)
            {
                auto& name = profiler.RefToName(i);
                auto& t = profiler.GetTimings(name).gpuTimes;
                if (!t.Size()) continue;
                // - to do
                auto& result = results[i];
                
                auto num = 0;
                for (int ti = 0; ti < t.Size(); ++ti, ++num)
                {
                    if (t[-ti].frame <= result.lastFrame) break;
                    //sum += t[-i].duration / t[-i].meta.factor;
                }
                if (!num) continue;
                for (; num > 0;)
                {
                    --num;
                    result.durations.push_back(static_cast<decltype(result.durations)::value_type>(t[-num].duration * 1e6));
                    // - to do: also store the meta factor
                    result.lastFrame = t[-num].frame;
                }
            }

            // Advance to next frame.
            ++currentFrame;
        }
    }

    void Benchmarker::SaveResults(const std::string& fileName, Results& results)
    {
        // Testing:
        std::cout << "Results: " << results.size() << " different timer sequences\n";
        size_t numTimes = 0u;
        for (Util::Timer::NameRef i = 0u; i < results.size(); ++i)
        {
            auto& r = results[i];
            std::cout << " " << app.timer.RefToName(i) << ": " << r.durations.size() << " times\n";
            numTimes += r.durations.size();
        }
        std::cout << "(for a total of " << numTimes << " times)\n";

        // Saving:

        std::filesystem::create_directories(resultsPath);
        std::ofstream file(resultsPath + fileName);
        if (!file.is_open())
        {
            std::cerr << "Could not open recording file " << fileName << " to save recording\n";
            return;
        }

        json j;
        j["results"] = json::object();
        for (Util::Timer::NameRef ref = 0u; ref < results.size(); ++ref)
        {
            auto& result = results[ref];
            const auto& name = app.timer.RefToName(ref);
            j["results"][name] = json::array();
            json da;
            for (auto& d : result.durations) da.push_back(d);
            j["results"][name] =
            {
                {"duration", da}
            };
            // - to do: more values, possibly
        }
        file << std::setw(4) << j;
    }

    void Benchmarker::OnRecordingFrame(double& dt)
    {
        // - should generator sync points be saved? Nah, let's just trust to its inflexible nature regarding framerate...
        Frame frame{};
        frame.cameraPosition = app.camera.GetPosition();
        frame.cameraOrientation = app.camera.GetOrientation();
        frame.cameraFovy = app.camera.GetFovy();
        frame.animationTime = app.atmosphere.GetAnimationTime();
        frame.lightTime = app.atmosphere.GetLightTime();
        recording.sequence.push_back(frame);
    }

    void Benchmarker::StopBenchmark()
    {
        if (Mode::Benchmarking != mode) return;
        mode = Mode::Inactive;
        // - to do: restore state modified by benchmarking

        // - to do: save results
    }

    void Benchmarker::StartRecording()
    {
        if (Mode::Inactive != mode) return;
        mode = Mode::Recording;

        recording.atmUpdateParams = app.atmUpdateParams;

        // - maybe to do: don't just discard unconditionally (rather allow for pausing in recording)
        recording = Configuration{};
    }

    void Benchmarker::StopRecording()
    {
        if (Mode::Recording != mode) return;
        mode = Mode::Inactive;

        std::filesystem::create_directories(recordPath);
        const auto fileName = recordPath + "recording.json"; // - to do: destination file name as parameter?
        std::ofstream file(fileName);
        if (!file.is_open())
        {
            std::cerr << "Could not open recording file " << fileName << " to save recording\n";
            return;
        }

        json j;
        j["atmosphereUpdateParams"] = 
        {
            {"update", recording.atmUpdateParams.update},
            {"animate", recording.atmUpdateParams.animate},
            {"rotateLight", recording.atmUpdateParams.rotateLight},
            {"frustumCull", recording.atmUpdateParams.frustumCull},
            {"depthLimit", recording.atmUpdateParams.depthLimit},
            {"useFeatureGenerator", recording.atmUpdateParams.useFeatureGenerator}
        };
        j["config"] =
        {
            {"resolution", { app.renderResolution.x, app.renderResolution.y }},
            {"warmUpFrames", 0},
            {"gpuMemBudgetMiB", app.gpuMemBudgetMiB}
        };

        j["sequence"] = json::array();
        for (auto& frame : recording.sequence)
        {
            auto& p = frame.cameraPosition;
            auto& o = frame.cameraOrientation;

            j["sequence"].push_back({
                {"cameraPosition", {p.x, p.y, p.z}},
                {"cameraOrientation", {o.x, o.y, o.z, o.w}},
                {"cameraFovy", frame.cameraFovy},
                {"animationTime", frame.animationTime},
                {"lightTime", frame.lightTime}
            });
        }

        // Compress by not storing repeat values:
        json p = json::object();
        std::vector<std::string> keysToDelete;
        for (size_t i = 0u; i < j["sequence"].size(); ++i)
        {
            keysToDelete.clear();
            auto& c = j["sequence"][i];
            for (auto& [key, value] : c.items())
            {
                if (p.contains(key) && p[key] == value)
                {
                    // No need to store a repeat value.
                    keysToDelete.push_back(key);
                    continue;
                }
                p[key] = value;
            }
            for (auto& key : keysToDelete)
            {
                c.erase(key);
            }
        }
        file << std::setw(4) << j;

        std::cout << "Recorded " << recording.sequence.size() << " frames" << std::endl;
    }

    template<typename T>
    static void jsonCond(json& j, T& value, const char* key)
    {
        if (j.contains(key)) value = j[key].get<T>();
    }

    void Benchmarker::Load(const std::string& dirPath)
    {
        configs.clear();
        size_t totalFrames = 0;
        for (auto& path : std::filesystem::directory_iterator(dirPath))
        {
            std::ifstream file{ path };
            if (!file.is_open())
            {
                std::cerr << "Could not open benchmark configuration file " << path << ".\n";
                return;
            }

            json j;
            file >> j;
            configs.push_back({});
            auto& config = configs.back();
            config.fileName = path.path().filename().string();

            // Load general parameters.
            int warmUpFrames = 0u;
            auto jc = j["config"];
            jsonCond(jc, config.warmUpFrames, "warmUpFrames");
            jsonCond(jc, config.gpuMemBudgetMiB, "gpuMemBudgetMiB");
            config.resolution = glm::ivec2(jc["resolution"][0].get<int>(), jc["resolution"][1].get<int>());
            std::cout << "Read config of resolution " << config.resolution.x << "*" << config.resolution.y << "\n";
            if (j.contains("atmosphereUpdateParams"))
            {
                auto aj = j["atmosphereUpdateParams"];
                jsonCond(aj, config.atmUpdateParams.update, "update");
                jsonCond(aj, config.atmUpdateParams.animate, "animate");
                jsonCond(aj, config.atmUpdateParams.rotateLight, "rotateLight");
                jsonCond(aj, config.atmUpdateParams.frustumCull, "frustumCull");
                jsonCond(aj, config.atmUpdateParams.depthLimit, "depthLimit");
                jsonCond(aj, config.atmUpdateParams.useFeatureGenerator, "useFeatureGenerator");
            }

            // Load frame sequence.
            Frame frame{};
            config.sequence.reserve(j["sequence"].size());
            for (auto f : j["sequence"])
            {
                if (f.contains("cameraPosition"))
                {
                    auto p = f["cameraPosition"];
                    frame.cameraPosition = Object::Position(p[0].get<double>(), p[1].get<double>(), p[2].get<double>());
                }
                if (f.contains("cameraOrientation"))
                {
                    auto o = f["cameraOrientation"];
                    frame.cameraOrientation = Object::Orientation(o[3].get<double>(), o[0].get<double>(), o[1].get<double>(), o[2].get<double>());
                }
                jsonCond(f, frame.cameraFovy, "cameraFovy");
                jsonCond(f, frame.animationTime, "animationTime");
                jsonCond(f, frame.lightTime, "lightTime");
                config.sequence.push_back(frame);
            }
            totalFrames += config.sequence.size();
        }
        std::cout << "Loaded " << configs.size() << " benchmark configurations (" << totalFrames << " frames in total)." << std::endl;
    }
}
