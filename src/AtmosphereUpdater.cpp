#include "AtmosphereUpdater.hpp"
#include "Atmosphere.hpp"
#include <functional>
#include <iostream>

namespace Mulen {

    AtmosphereUpdater::AtmosphereUpdater(Atmosphere& atmosphere)
        : atmosphere{ atmosphere }
        , thread(&AtmosphereUpdater::UpdateLoop, this)
    {

    }

    void AtmosphereUpdater::InitialSetup()
    {
        auto& a = atmosphere;
        auto& it = GetUpdateIteration();

        // - test: "manual" splits, indiscriminately to a chosen level
        std::function<void(NodeIndex, unsigned, glm::dvec4)> testSplit = [&](NodeIndex gi, unsigned depth, glm::dvec4 pos)
        {
            if (!depth) return;
            for (NodeIndex i = 0u; i < NodeArity; ++i)
            {
                const auto ci = NodeArity - 1u - i;
                const auto ni = Octree::GroupAndChildToNode(gi, ci);

                auto childPos = pos;
                childPos.w *= 0.5;
                childPos += (glm::dvec4(ci & 1u, (ci >> 1u) & 1u, (ci >> 2u) & 1u, 0.5) * 2.0 - 1.0)* childPos.w;
                //if (false)
                {
                    // - simple test to only split those in spherical atmosphere shell:
                    auto p = glm::dvec3(childPos) * a.scale;
                    const auto size = childPos.w * a.scale;
                    const Object::Position sphereCenter{ 0.0 };
                    const auto height = 0.05, radius = 1.0; // - to do: check/correct these values
                    const auto atmRadius2 = (radius + height) * (radius + height);

                    auto bmin = p - size, bmax = p + size;
                    const auto dist2 = glm::distance2(glm::clamp(sphereCenter, bmin, bmax), sphereCenter);
                    if (dist2 > atmRadius2) continue; // outside
                    bool anyOutside = false;
                    for (int z = -1; z <= 1; z += 2)
                    {
                        for (int y = -1; y <= 1; y += 2)
                        {
                            for (int x = -1; x <= 1; x += 2)
                            {
                                if (glm::distance2(sphereCenter, p + glm::dvec3(x, y, z) * size) > radius* radius) anyOutside = true;
                            }
                        }
                    }
                    if (!anyOutside) continue; // inside

                }

                if (!a.octree.nodes.GetNumFree()) return;
                const auto& children = a.octree.GetNode(ni).children;
                if (InvalidIndex == children)
                {
                    a.octree.Split(ni);
                    StageSplit(it, children);
                }
                testSplit(children, depth - 1u, childPos);
            }
        };
        auto testSplitRoot = [&](unsigned depth)
        {
            for (auto i = 1u; i <= depth; ++i)
            {
                testSplit(a.rootGroupIndex, i, glm::dvec4{ 0, 0, 0, 1 });
            }
        };

        StageSplit(it, a.rootGroupIndex);
        const auto testDepth = 7u; // - to do: probably lower to 6 (or even just 5) when lowering total memory use
        testSplitRoot(testDepth);
        const auto res = (2u << testDepth) * (BrickRes - 1u);
        std::cout << "Voxel resolution: " << res << " (" << 2e-3 * a.planetRadius * a.scale / res << " km/voxel)\n";
    }

    AtmosphereUpdater::~AtmosphereUpdater()
    {
        std::unique_lock<std::mutex> lk{ mutex };
        done = true;
        lk.unlock();
        cv.notify_one();
        thread.join();
    }

    Util::Shader& AtmosphereUpdater::SetShader(Util::Shader& shader)
    {
        shader.Bind();
        atmosphere.SetUniforms(shader);
        return shader;
    }

    void AtmosphereUpdater::UpdateMap(GpuState& state)
    {
        //auto t = timer.Begin("Map");
        auto& shader = SetShader(atmosphere.updateOctreeMapShader);
        const glm::uvec3 resolution{ state.octreeMap.GetWidth(), state.octreeMap.GetHeight(), state.octreeMap.GetDepth() };
        shader.Uniform3f("resolution", glm::vec3(resolution));
        glBindImageTexture(0u, state.octreeMap.GetId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);
        const auto groups = resolution / 8u;
        glDispatchCompute(groups.x, groups.y, groups.z);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    void AtmosphereUpdater::UpdateNodes(uint64_t num)
    {
        //auto t = timer.Begin("Nodes");
        SetShader(atmosphere.updateShader);
        glDispatchCompute((GLuint)num, 1u, 1u);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    void AtmosphereUpdater::GenerateBricks(GpuState& state, uint64_t first, uint64_t num)
    {
        //auto t = timer.Begin("Generation");
        glBindImageTexture(0u, state.brickTexture.GetId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, BrickFormat);
        auto& shader = SetShader(atmosphere.updateBricksShader);
        shader.Uniform1u("brickUploadOffset", glm::uvec1{ (unsigned)first });
        glDispatchCompute((GLuint)num, 1u, 1u);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    void AtmosphereUpdater::LightBricks(GpuState& state, uint64_t first, uint64_t num)
    {
        //auto t = timer.Begin("Lighting");
        glBindImageTexture(0u, atmosphere.brickLightTextureTemp.GetId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, BrickLightFormat);
        auto& shader = SetShader(atmosphere.updateLightShader);
        shader.Uniform1u("brickUploadOffset", glm::uvec1{ (unsigned)first });
        glDispatchCompute((GLuint)num, 1u, 1u);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    void AtmosphereUpdater::FilterLighting(GpuState& state, uint64_t first, uint64_t num)
    {
        //auto t = timer.Begin("Light filter");
        glBindImageTexture(0u, state.brickLightTexture.GetId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, BrickLightFormat);
        auto& shader = SetShader(atmosphere.lightFilterShader);
        shader.Uniform1u("brickUploadOffset", glm::uvec1{ (unsigned)first });
        atmosphere.brickLightTextureTemp.Bind(1u);
        glDispatchCompute((GLuint)num, 1u, 1u);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    void AtmosphereUpdater::OnFrame(double time, double period)
    {
        auto& a = atmosphere;
        const auto fps = 60.0; // - to do: measure/adjust
        const auto rate = period / fps;

        auto setStage = [&](unsigned i)
        {
            updateStage = (UpdateStage)i;
            updateFraction = 0.0;
            updateStageIndex0 = updateStageIndex1 = 0u;
        };
        if (updateStage == UpdateStage::Finished) // start a new iteration?
        {
            std::unique_lock<std::mutex> lk{ mutex };
            if (nextUpdateReady) // has the worker thread completed its iteration?
            {
                nextUpdateReady = false;
                updateIteration = (updateIteration + 1u) % std::extent<decltype(iterations)>::value;
                GetUpdateIteration().time = time; // - actually wrong (to do: compute correct one-second-into-the-future-from-last-iteration)
                // - to do: also send camera parameters
                lk.unlock();
                cv.notify_one();
            }
            else
            {
                return; // nothing to do (update-wise) until the worker thread is done
            }

            setStage(0);
            updateStateIndex = (updateStateIndex + 1u) % std::extent<decltype(a.gpuStates)>::value;
        }

        auto& it = GetRenderIteration();
        auto& state = a.gpuStates[updateStateIndex];
        state.gpuNodes.BindBase(GL_SHADER_STORAGE_BUFFER, 0u);
        state.brickTexture.Bind(0u);
        state.octreeMap.Bind(2u);

        auto fraction = 0.0;

        auto computeNum = [&](size_t total, uint64_t done)
        {
            if (total <= done) return 0u; // all done
            return glm::min(unsigned(total - done), glm::max(unsigned(1u), unsigned(fraction * total)));
        };

        // - to do: make sure to take care of lower-depth nodes first, as children may want to access parent data
        // - to do: try to automatically adjust relative time use (fractions) via measurements of time taken? Maybe

        switch (updateStage)
        {
        case UpdateStage::UploadAndGenerate:
        {
            fraction = (1.0 / 0.4) * rate;
            const auto numNodes = computeNum(it.nodesToUpload.size(), updateStageIndex0),
                numBricks = computeNum(it.bricksToUpload.size(), updateStageIndex1);

            if (numNodes)
            {
                auto& last = updateStageIndex0;
                a.gpuUploadNodes.Upload(0, sizeof(UploadNodeGroup) * numNodes, it.nodesToUpload.data() + last);
                UpdateNodes(numNodes);
                last += numNodes;
            }

            if (numBricks)
            {
                auto& last = updateStageIndex1;
                a.gpuUploadBricks.Upload(sizeof(UploadBrick) * last, sizeof(UploadBrick) * numBricks, it.bricksToUpload.data() + last);
                GenerateBricks(state, last, numBricks);
                last += numBricks;
            }

            break;
        }

        case UpdateStage::Map:
        {
            fraction = 1.0;
            UpdateMap(state);
            break;
        }

        case UpdateStage::Lighting:
        {
            fraction = (1.0 / 0.4) * rate;
            const auto numBricks = computeNum(it.bricksToUpload.size(), updateStageIndex1);
            if (numBricks)
            {
                auto& last = updateStageIndex1;
                LightBricks(state, last, numBricks);
                last += numBricks;
            }
            break;
        }

        case UpdateStage::LightFilter:
        {
            fraction = (1.0 / 0.2) * rate;
            const auto numBricks = computeNum(it.bricksToUpload.size(), updateStageIndex1);
            if (numBricks)
            {
                auto& last = updateStageIndex1;
                FilterLighting(state, last, numBricks);
                last += numBricks;
            }
            break;
        }
        }

        updateFraction += fraction;
        if (updateFraction >= 1.0) setStage(unsigned(updateStage) + 1u);
    }

    void AtmosphereUpdater::StageNodeGroup(Iteration& it, UploadType type, NodeIndex groupIndex)
    {
        // - to do: check that we don't exceed maxNumUpload here, or leave that to the caller?
        uint32_t genData = 0u;
        genData |= uint32_t(type) << 24u;
        UploadNodeGroup upload{};
        upload.groupIndex = groupIndex;
        upload.genData = genData;
        upload.nodeGroup = atmosphere.octree.GetGroup(groupIndex);
        it.nodesToUpload.push_back(upload);
    }

    void AtmosphereUpdater::StageBrick(Iteration& it, UploadType type, NodeIndex nodeIndex)
    {
        // - to do: check that we don't exceed maxNumUpload here, or leave that to the caller?
        const auto brickIndex = nodeIndex;
        uint32_t genData = 0u; // - to do: also add upload brick data index, when applicable
        genData |= uint32_t(type) << 24u;

        glm::vec3 nodeCenter{ 0.0, 0.0, 0.0 };
        float nodeSize = 1.0;
        for (auto ni = nodeIndex; ni != InvalidIndex; ni = atmosphere.octree.GetGroup(Octree::NodeToGroup(ni)).parent)
        {
            glm::ivec3 ioffs = glm::ivec3(ni & 1u, (ni >> 1u) & 1u, (ni >> 2u) & 1u) * 2 - 1;
            nodeCenter += glm::vec3(ioffs) * nodeSize;
            nodeSize *= 2.0;
        }
        nodeCenter /= nodeSize;
        nodeSize = 1.0f / nodeSize;

        UploadBrick upload{};
        upload.nodeIndex = nodeIndex;
        upload.brickIndex = brickIndex;
        upload.genData = genData;
        upload.nodeLocation = glm::vec4(glm::vec3(nodeCenter), (float)nodeSize);

        // - to do: add various generation parameters (e.g. wind vector/temperature/humidity)
        it.bricksToUpload.push_back(upload);
    }

    void AtmosphereUpdater::StageSplit(Iteration& it, NodeIndex gi)
    {
        StageNodeGroup(it, UploadType::Split, gi);
        for (NodeIndex ci = 0u; ci < NodeArity; ++ci)
        {
            const auto ni = Octree::GroupAndChildToNode(gi, ci);
            StageBrick(it, UploadType::Split, ni);
        }
    }

    void AtmosphereUpdater::UpdateLoop()
    {
        while (true)
        {
            std::unique_lock<std::mutex> lk{ mutex };
            cv.wait(lk, [&] { return done || !nextUpdateReady; });

            if (done) return;
            if (nextUpdateReady) continue;
            lk.unlock();
            ComputeIteration(GetUpdateIteration());
            {
                std::unique_lock<std::mutex> lk{ mutex };
                nextUpdateReady = true;
            }
        }
    }

    void AtmosphereUpdater::ComputeIteration(Iteration& it)
    {
        auto& a = atmosphere;
        it.nodesToUpload.resize(0u);
        it.bricksToUpload.resize(0u);

        // - to do: update octree (eventually in a separate copy so that the render thread may do intersections/lookups in its own copy)
        // - to do: update upload buffers

        // - temporary identity upload:
        std::function<void(NodeIndex, unsigned, glm::dvec4)> stageGroup = [&](NodeIndex gi, unsigned depth, glm::dvec4 pos)
        {
            StageSplit(it, gi);
            for (NodeIndex i = 0u; i < NodeArity; ++i)
            {
                const auto ci = NodeArity - 1u - i;
                const auto ni = Octree::GroupAndChildToNode(gi, ci);

                const auto children = a.octree.GetNode(ni).children;
                if (InvalidIndex == children) continue;
                stageGroup(children, depth + 1u, {});
            }
        };
        stageGroup(a.rootGroupIndex, 0u, {});
    }
}
