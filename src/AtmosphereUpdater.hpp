#pragma once
#include "Common.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>

namespace Mulen {
    class Atmosphere;

    class AtmosphereUpdater
    {
        friend class Atmosphere; // - this should probably be made unnecessary

        struct IterationParameters
        {
            double time;
            Object::Position cameraPosition;
            // - maybe also orientation and field of view, if trying frustum culling
            unsigned depthLimit;
        };
        struct Iteration
        {
            std::vector<UploadNodeGroup> nodesToUpload;
            std::vector<UploadBrick> bricksToUpload;

            IterationParameters params;

            unsigned maxDepth;
            // - to do: full depth distribution? Assuming 32 as max depth should be plenty
            // - to do: more stats
        } iterations[2];
        unsigned updateIteration = 0u; // - to do: better name (this is specifically the threaded CPU index)
        bool nextUpdateReady = true;

        void StageNodeGroup(Iteration&, UploadType, NodeIndex);
        void StageBrick(Iteration&, UploadType, NodeIndex, const glm::vec4& nodePos);
        void StageSplit(Iteration&, NodeIndex gi, const glm::vec4& groupPos);

        Util::Shader& SetShader(Util::Shader&);
        void UpdateMap(GpuState&);
        void UpdateNodes(uint64_t num);
        void GenerateBricks(GpuState&, uint64_t first, uint64_t num);
        void LightBricks(GpuState&, uint64_t first, uint64_t num);
        void FilterLighting(GpuState&, uint64_t first, uint64_t num);
        void ComputeIteration(Iteration&);

        bool NodeInAtmosphere(const Iteration&, const glm::dvec4& nodePosAndScale);

        Iteration& GetRenderIteration() { return iterations[(updateIteration + 1u) % std::extent<decltype(iterations)>::value]; }
        Iteration& GetUpdateIteration() { return iterations[updateIteration]; }

        enum class UpdateStage
        {
            UploadAndGenerate,
            Map,
            Lighting,
            LightFilter,
            Finished
        } updateStage = UpdateStage::Finished;
        double updateFraction = 0.0;
        uint64_t updateStageIndex0 = 0u, updateStageIndex1 = 0u;
        unsigned updateStateIndex = 0u;

        Atmosphere& atmosphere;
        bool done = false;
        std::mutex mutex;
        std::condition_variable cv;
        std::thread thread;


    public:
        AtmosphereUpdater(Atmosphere&);
        ~AtmosphereUpdater();
        void UpdateLoop();

        void InitialSetup();
        void OnFrame(const IterationParameters&, double period);
    };
}
