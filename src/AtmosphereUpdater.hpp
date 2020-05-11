#pragma once
#include "Common.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include "util/Timer.hpp"

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
            Object::Position lightDirection;
            unsigned depthLimit;
        };
        struct Iteration
        {
            std::vector<UploadNodeGroup> nodesToUpload;
            std::vector<UploadBrick> bricksToUpload;
            std::vector<NodeIndex> splitGroups; // indices of groups resulting from splits in this update

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
        void UpdateMap(Util::Texture&, glm::vec3 pos = glm::vec3(-1.0f), glm::vec3 scale = glm::vec3(2.0f), unsigned depthOffset = 0u);
        void UpdateNodes(uint64_t num);
        void GenerateBricks(GpuState&, uint64_t first, uint64_t num);
        void LightBricks(GpuState&, uint64_t first, uint64_t num, const Object::Position& lightDir, const Util::Timer::DurationMeta&);
        void FilterLighting(GpuState&, uint64_t first, uint64_t num);
        void ComputeIteration(Iteration&);

        bool NodeInAtmosphere(const Iteration&, const glm::dvec4& nodePosAndScale);

        Iteration& GetRenderIteration() { return iterations[(updateIteration + 1u) % std::extent<decltype(iterations)>::value]; }
        Iteration& GetUpdateIteration() { return iterations[updateIteration]; }

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
        void OnFrame(const IterationParameters&, double period);
        double GetUpdateFraction() const { return updateFraction; }
    };
}
