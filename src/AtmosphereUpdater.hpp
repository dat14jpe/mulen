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

        void StageNodeGroup(UploadType, NodeIndex ni);
        void StageBrick(UploadType, NodeIndex ni); // - to do: also brick data (at least optionally, if/when generating on GPU)

        Util::Shader& SetShader(Util::Shader& shader);
        void UpdateMap(GpuState& state);
        void UpdateNodes(uint64_t num);
        void GenerateBricks(GpuState& state, uint64_t first, uint64_t num);
        void LightBricks(GpuState& state, uint64_t first, uint64_t num);
        void FilterLighting(GpuState& state, uint64_t first, uint64_t num);



        // - to do: multiple update iterations' worth of these (and of the octree!)
        std::vector<UploadNodeGroup> nodesToUpload;
        std::vector<UploadBrick> bricksToUpload;

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

        // - maybe also move GPU update logic here, eventually. Maybe

    public:
        AtmosphereUpdater(Atmosphere&);
        ~AtmosphereUpdater();
        void UpdateLoop();

        void InitialSetup();
        void OnFrame(double time, double period);
        void EndIteration(); // - to do: return data, somehow
    };
}
