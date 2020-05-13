#pragma once
#include "Common.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include "util/Timer.hpp"
#include "Generator.hpp"
#include "FeatureGenerator.hpp"

namespace Mulen {
    class Atmosphere;

    class AtmosphereUpdater
    {
        // - to do: keep these elsewhere (so the updater isn't hardcoded to use just one or a few)
        Generator generator;
        FeatureGenerator featureGenerator;
        // - to do: current generator selection

        friend class Atmosphere; // - this should probably be made unnecessary

        std::vector<NodeIndex> priorSplitGroups;
        UpdateIteration iterations[2];
        unsigned updateIteration = 0u; // - to do: better name (this is specifically the threaded CPU index)
        bool nextUpdateReady = true;

        void StageNodeGroup(UpdateIteration&, UploadType, NodeIndex);
        void StageBrick(UpdateIteration&, UploadType, NodeIndex, const glm::vec4& nodePos, uint32_t genDataOffset, uint32_t genDataSize);
        void StageSplit(UpdateIteration&, NodeIndex gi, const glm::vec4& groupPos);

        Util::Shader& SetShader(Util::Shader&);
        void UpdateMap(Util::Texture&, glm::vec3 pos = glm::vec3(-1.0f), glm::vec3 scale = glm::vec3(2.0f), unsigned depthOffset = 0u);
        void UpdateNodes(uint64_t num);
        void GenerateBricks(GpuState&, Generator&, uint64_t first, uint64_t num);
        void LightBricks(GpuState&, uint64_t first, uint64_t num, const Object::Position& lightDir, const Util::Timer::DurationMeta&);
        void FilterLighting(GpuState&, uint64_t first, uint64_t num);
        void ComputeIteration(UpdateIteration&);

        bool NodeInAtmosphere(const UpdateIteration&, const glm::dvec4& nodePosAndScale);

        UpdateIteration& GetRenderIteration() { return iterations[(updateIteration + 1u) % std::extent<decltype(iterations)>::value]; }
        UpdateIteration& GetUpdateIteration() { return iterations[updateIteration]; }

        struct Stage
        {
            enum class Id
            {
                Init,       // begin a new generation pass
                Generate,   // upload node and brick data, generate cloud density values
                SplitInit,  // initialise previous data for newly split nodes (to avoid animation glitches from brick memory reuse)
                Map,        // create octree traversal optimisation map
                Light,      // cast shadow rays to compute lighting
                Filter,     // filter lighting and combine with brick density values
            } id;
            const std::string str;
            double cost; // time
        };
        std::vector<Stage> stages;
        double totalStagesTime = 0.0;

        uint64_t updateStage = 0u;
        double updateFraction = 0.0;
        uint64_t updateStageIndex0 = 0u, updateStageIndex1 = 0u;
        unsigned updateStateIndex = 0u;

        /*// - old
        enum class UpdateStage
        {
            UploadAndGenerate,
            Map,
            Lighting,
            LightFilter,
            Finished
        } updateStage = UpdateStage::Finished;*/

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
        void OnFrame(const UpdateIteration::Parameters&, double period);
        double GetUpdateFraction() const { return updateFraction; }
    };
}
