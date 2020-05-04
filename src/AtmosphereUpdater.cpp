#include "AtmosphereUpdater.hpp"
#include "Atmosphere.hpp"
#include <functional>
#include <queue>
#include <unordered_set>
#include <iostream>
#include "util/Timer.hpp"
#include <numeric>

namespace Mulen {

    AtmosphereUpdater::AtmosphereUpdater(Atmosphere& atmosphere)
        : atmosphere{ atmosphere }
        , thread(&AtmosphereUpdater::UpdateLoop, this)
    {

    }


    bool AtmosphereUpdater::NodeInAtmosphere(const Iteration& it, const glm::dvec4& childPos)
    {
        auto& a = atmosphere;

        // - simple test to only split those in spherical atmosphere shell:
        auto p = glm::dvec3(childPos) * a.scale;
        const auto size = childPos.w * a.scale;
        const Object::Position sphereCenter{ 0.0 };
        const auto height = 0.005, radius = 1.0; // - to do: check/correct these values
        const auto atmRadius2 = (radius + height) * (radius + height);
        const auto innerRadius = radius; // - to do: modify, as "safety" margin

        auto bmin = p - size, bmax = p + size;
        const auto dist2 = glm::distance2(glm::clamp(sphereCenter, bmin, bmax), sphereCenter);
        if (dist2 > atmRadius2) return false; // outside
        bool anyOutside = false;
        for (int z = -1; z <= 1; z += 2)
        {
            for (int y = -1; y <= 1; y += 2)
            {
                for (int x = -1; x <= 1; x += 2)
                {
                    if (glm::distance2(sphereCenter, p + glm::dvec3(x, y, z) * size) > innerRadius * innerRadius) return true;
                }
            }
        }
        return false; // wholly inside planet
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
                if (!NodeInAtmosphere(it, childPos)) continue;

                if (!a.octree.nodes.GetNumFree()) return;
                const auto& children = a.octree.GetNode(ni).children;
                if (InvalidIndex == children)
                {
                    a.octree.Split(ni);
                    StageSplit(it, children, childPos);
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

        StageSplit(it, a.rootGroupIndex, { 0.0, 0.0, 0.0, 1.0 });
        const auto testDepth = 6u;
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
        shader.Uniform1f("time", glm::vec1{ (float)atmosphere.lightTime });
        return shader;
    }

    void AtmosphereUpdater::UpdateMap(Util::Texture& octreeMap, glm::vec3 pos, glm::vec3 scale, unsigned depthOffset)
    {
        //auto t = timer.Begin("Map");
        auto& shader = SetShader(atmosphere.updateOctreeMapShader);
        const glm::uvec3 resolution{ octreeMap.GetWidth(), octreeMap.GetHeight(), octreeMap.GetDepth() };
        shader.Uniform3f("resolution", glm::vec3(resolution));
        shader.Uniform3f("mapPosition", pos);
        shader.Uniform3f("mapScale", scale / glm::vec3(resolution));
        shader.Uniform1u("maxDepth", glm::uvec1(static_cast<unsigned>(log2(static_cast<float>(resolution.x)) - 1u + depthOffset)));
        glBindImageTexture(0u, octreeMap.GetId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);
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
        glBindImageTexture(0u, state.brickTexture.GetId(), 0, GL_TRUE, 0, GL_READ_WRITE, BrickFormat);
        {
            //auto t = timer.Begin("Generation");
            auto& shader = SetShader(atmosphere.updateBricksShader);
            shader.Uniform1u("brickUploadOffset", glm::uvec1{ (unsigned)first });
            glDispatchCompute((GLuint)num, 1u, 1u);
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        }
        { // "optimisation" pass (compute constancy flags, possibly more)
            auto& shader = SetShader(atmosphere.updateFlagsShader);
            shader.Uniform1u("brickUploadOffset", glm::uvec1{ (unsigned)first });
            glDispatchCompute((GLuint)num, 1u, 1u);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
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
        //glBindImageTexture(0u, state.brickLightTexture.GetId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, BrickLightFormat);
        glBindImageTexture(0u, state.brickTexture.GetId(), 0, GL_TRUE, 0, GL_READ_WRITE, BrickFormat);
        auto& shader = SetShader(atmosphere.lightFilterShader);
        shader.Uniform1u("brickUploadOffset", glm::uvec1{ (unsigned)first });
        atmosphere.brickLightTextureTemp.Bind(1u);
        glDispatchCompute((GLuint)num, 1u, 1u);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    void AtmosphereUpdater::OnFrame(const IterationParameters& params, double period)
    {
        auto& a = atmosphere;
        const auto fps = 60.0; // - to do: measure/adjust
        const auto rate = period / fps;
        const auto dt = 1.0 / 60.0; // - to do: use actual time (though maybe not *directly*)

        if (stages.empty())
        {
            // - these are just hardcoded estimates for now, but they should really
            // be continuously measured and estimated by the program

            stages.push_back({ Stage::Id::Init,     0.1 });
            stages.push_back({ Stage::Id::Generate, 20.0 });
            stages.push_back({ Stage::Id::Map,      1.0 });
            stages.push_back({ Stage::Id::Light,    60.0 });
            stages.push_back({ Stage::Id::Filter,   20.0 });
        }

        auto totalStagesTime = 0.0;
        for (auto& stage : stages) totalStagesTime += stage.cost;
        auto frameCost = 0.0;
        const auto maxFrameCost = dt / period;
        //std::cout << std::endl << "Beginning update loop" << std::endl << std::endl;
        while (frameCost < maxFrameCost)
        {
            auto& it = GetRenderIteration();
            auto& state = a.gpuStates[updateStateIndex];
            state.gpuNodes.BindBase(GL_SHADER_STORAGE_BUFFER, 0u);
            state.brickTexture.Bind(0u);
            state.octreeMap.Bind(2u);

            auto& stage = stages[updateStage];
            const auto relativeStageCost = stage.cost / totalStagesTime;
            uint64_t totalItems = 0u, numToDo = 0u;
            const auto maxFraction = (maxFrameCost - frameCost) / relativeStageCost;
            auto& last = updateStageIndex0;

            auto computeWorkSize = [&](size_t total)
            {
                /*std::cout << "Starting from " << last << std::endl;
                std::cout << "relative stage cost: " << stage.cost / totalStagesTime << ", remaining spend: " << maxFrameCost - frameCost << std::endl;
                std::cout << "Computing work size for a total of " << total << " (maxFraction: " << maxFraction << ")" << std::endl;*/
                totalItems = total;
                numToDo = totalItems - last;
                numToDo = glm::min(numToDo, glm::max((size_t)1u, size_t(glm::ceil(totalItems * maxFraction))));
            };

            switch (stage.id)
            {
            case Stage::Id::Init:
            {
                // - to do: update stage costs based on measured GPU times
                // (possibly, at least. Note that V-synced values aren't fully accurate)
                // (but they should at least be enough while everything else is also V-synced, no?)

                std::unique_lock<std::mutex> lk{ mutex };
                if (nextUpdateReady) // has the worker thread completed its iteration?
                {
                    nextUpdateReady = false;
                    updateIteration = (updateIteration + 1u) % std::extent<decltype(iterations)>::value;
                    // - actually wrong time (to do: compute correct one-second-into-the-future-from-last-iteration)
                    GetUpdateIteration().params = params;
                    lk.unlock();
                    cv.notify_one();
                }
                else
                {
                    return; // nothing to do (update-wise) until the worker thread is done
                }

                updateStateIndex = (updateStateIndex + 1u) % std::extent<decltype(a.gpuStates)>::value;
                updateFraction = 0.0;
                totalItems = numToDo = 1u;
                break;
            }
            case Stage::Id::Generate:
            {
                computeWorkSize(it.nodesToUpload.size()); // assuming num bricks = num node groups * NodeArity (which should always be true)
                if (numToDo)
                {
                    a.gpuUploadNodes.Upload(0, sizeof(UploadNodeGroup) * numToDo, it.nodesToUpload.data() + last);
                    UpdateNodes(numToDo);
                    const auto bricksOffset = last * NodeArity, numBricks = numToDo * NodeArity;
                    a.gpuUploadBricks.Upload(sizeof(UploadBrick) * bricksOffset, sizeof(UploadBrick) * numBricks, it.bricksToUpload.data() + bricksOffset);
                    GenerateBricks(state, bricksOffset, numBricks);
                }
                break;
            }
            case Stage::Id::Map:
            {
                UpdateMap(state.octreeMap);
                totalItems = numToDo = 1u;
                break;
            }
            case Stage::Id::Light:
            {
                computeWorkSize(it.bricksToUpload.size());
                if (numToDo) LightBricks(state, last, numToDo);
                break;
            }
            case Stage::Id::Filter:
            {
                computeWorkSize(it.bricksToUpload.size());
                if (numToDo) FilterLighting(state, last, numToDo);
                break;
            }
            }
            if (!numToDo) return; // - feels... hacky. Think about this
            last += numToDo;

            // Update stage state.
            const auto fraction = double(numToDo) / double(totalItems);
            if (updateStageIndex0 >= totalItems) // are we done with this stage?
            {
                updateStage = (updateStage + 1) % stages.size();
                updateStageIndex0 = updateStageIndex1 = 0;
            }
            frameCost += fraction * relativeStageCost;
        }
        updateFraction += maxFrameCost; // - to do: think this over
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

    void AtmosphereUpdater::StageBrick(Iteration& it, UploadType type, NodeIndex nodeIndex, const glm::vec4& nodePos)
    {
        // - to do: check that we don't exceed maxNumUpload here, or leave that to the caller?
        const auto brickIndex = nodeIndex;
        uint32_t genData = 0u; // - to do: also add upload brick data index, when applicable
        genData |= uint32_t(type) << 24u;

        glm::vec3 nodeCenter{ 0.0, 0.0, 0.0 };
        float nodeSize = 1.0;
        /*for (auto ni = nodeIndex; ni != InvalidIndex; ni = atmosphere.octree.GetGroup(Octree::NodeToGroup(ni)).parent)
        {
            glm::ivec3 ioffs = glm::ivec3(ni & 1u, (ni >> 1u) & 1u, (ni >> 2u) & 1u) * 2 - 1;
            nodeCenter += glm::vec3(ioffs) * nodeSize;
            nodeSize *= 2.0;
        }
        nodeCenter /= nodeSize;
        nodeSize = 1.0f / nodeSize;*/

        nodeCenter = glm::vec3(nodePos);
        nodeSize = nodePos.w;

        UploadBrick upload{};
        upload.nodeIndex = nodeIndex;
        upload.brickIndex = brickIndex;
        upload.genData = genData;
        upload.nodeLocation = glm::vec4(glm::vec3(nodeCenter), (float)nodeSize);

        // - to do: add various generation parameters (e.g. wind vector/temperature/humidity)
        it.bricksToUpload.push_back(upload);
    }

    void AtmosphereUpdater::StageSplit(Iteration& it, NodeIndex gi, const glm::vec4& nodePos)
    {
        StageNodeGroup(it, UploadType::Split, gi);
        for (NodeIndex ci = 0u; ci < NodeArity; ++ci)
        {
            auto childPos = nodePos; // - to do: modify for child
            childPos.w *= 0.5;
            childPos += glm::vec4(glm::vec3(glm::ivec3(ci & 1u, (ci >> 1u) & 1u, (ci >> 2u) & 1u) * 2 - 1) * childPos.w, 0.0);
            const auto ni = Octree::GroupAndChildToNode(gi, ci);
            StageBrick(it, UploadType::Split, ni, childPos);
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
        const auto startTime = Util::Timer::Clock::now();

        auto& a = atmosphere;
        it.nodesToUpload.resize(0u);
        it.bricksToUpload.resize(0u);
        it.maxDepth = 0u;

        struct PriorityNode
        {
            NodeIndex index;
            double priority;
            // - maybe also add depth/location/size
        };
        auto cmpSplit = [](const PriorityNode& a, const PriorityNode& b) { return a.priority < b.priority; };
        auto cmpMerge = [](const PriorityNode& a, const PriorityNode& b) { return a.priority > b.priority; };
        std::priority_queue<PriorityNode, std::vector<PriorityNode>, decltype(cmpSplit)> splitPrio(cmpSplit);
        std::priority_queue<PriorityNode, std::vector<PriorityNode>, decltype(cmpMerge)> mergePrio(cmpMerge);

        const auto MaxDepth = it.params.depthLimit;
        const auto camPos = it.params.cameraPosition * a.scale;
        const auto h = glm::length(camPos) - 1.0;
        const auto r = 1.0;
        const auto cloudTop = 0.005; // - to do: retrieve from somewhere else
        // - to try: only use ground horizon here, and test the angle for nodes beyond that
        const auto horizonDist = 
            sqrt(h * (h + 2.0 * r)) // distance to ground horizon
            + sqrt(cloudTop * (cloudTop + 2.0 * r)) // distance to cloud horizon
            ;

        // - to do: update octree (eventually in a separate copy so that the render thread may do intersections/lookups in its own copy)
        // Traverse all, compute split and merge priorities:
        std::function<bool(NodeIndex, unsigned, glm::dvec4)> computePriority = [&](NodeIndex gi, unsigned depth, glm::dvec4 pos)
        {
            it.maxDepth = glm::max(it.maxDepth, depth);
            auto hasGrandchildren = false;
            for (NodeIndex ci = 0u; ci < NodeArity; ++ci)
            {
                auto childPos = pos;
                childPos.w *= 0.5;
                childPos += (glm::dvec4(glm::uvec3(ci, ci >> 1u, ci >> 2u) & 1u, 0.5) * 2.0 - 1.0) * childPos.w;
                const auto nodePos = Object::Position(childPos) * a.scale;
                const auto nodeSize = childPos.w * a.scale;
                const auto nodeMin = nodePos - nodeSize, nodeMax = nodePos + nodeSize;
                if (!NodeInAtmosphere(it, childPos)) continue; // - do we also need to see if this can merge? To do

                const auto ni = Octree::GroupAndChildToNode(gi, ci);
                const auto children = a.octree.GetNode(ni).children;
                hasGrandchildren = hasGrandchildren || InvalidIndex != children;
                //const auto insideNode = glm::all(glm::lessThan(glm::abs(camPos - nodePos) / nodeSize, glm::dvec3{ 1.0 }));
                const auto distanceToNode = glm::length(glm::max(glm::dvec3(0.0), glm::max(nodeMin - camPos, camPos - nodeMax)));
                const auto insideNode = distanceToNode == 0.0;

                // - to do: check if inside (cloud) horizon
                // (which is true if either inside distance-to-ground-horizon or sufficiently low angle for nodes beyond)
                const auto margin = sqrt(3 * (2 * nodeSize) * (2 * nodeSize));
                if (!insideNode && distanceToNode - margin > horizonDist)
                {
                    // - to do: check angle, continue'ing if the check fails

                    if (InvalidIndex != children)
                    {
                        if (!computePriority(children, depth + 1u, childPos)) // - to let children insert themselves in the merge priority
                        mergePrio.push({ ni, 0.0 }); // - experimental
                    }
                    continue; // - testing (should only happen if the angle check fails)
                }
                // - to do: also check for shadowing parts of the atmosphere, somehow, eventually

                // - to do: tune priority computation (though maybe a simple one works well enough)
                auto priority = nodeSize / glm::max(1e-10, distanceToNode);
                if (insideNode) priority = 1e20;

                if (InvalidIndex == children)
                {
                    if (depth >= MaxDepth) continue;
                    // - to do: check for splittability more thoroughly
                    splitPrio.push({ ni, priority });
                    continue;
                }
                if (!computePriority(children, depth + 1u, childPos) && !insideNode)
                {
                    // No grandchildren, which means this node is eligible for merging.
                    mergePrio.push({ ni, priority });
                }
            }
            return hasGrandchildren;
        };
        computePriority(a.rootGroupIndex, 0u, {0, 0, 0, 1});

        // - to do: compute merge threshold (below which nodes won't be merged unless needed)
        auto mergeThreshold = 1e20; // - arbitrary (but it shouldn't be)
        auto doSplits = [&]()
        {
            while (!splitPrio.empty())
            {
                auto toSplit = splitPrio.top();
                splitPrio.pop();

                if (!a.octree.nodes.GetNumFree()) // are we out of octree memory?
                {
                    while (true)
                    {
                        if (mergePrio.empty()) return; // no more to merge
                        auto toMerge = mergePrio.top();
                        if (toMerge.priority > toSplit.priority) return; // all merge candidates are of higher priority than split candidates
                        mergePrio.pop();

                        auto hasGrandchildren = false;
                        const auto children = a.octree.GetNode(toMerge.index).children;
                        for (auto ci = 0u; ci < NodeArity; ++ci)
                        {
                            if (a.octree.GetNode(Octree::GroupAndChildToNode(children, ci)).children != InvalidIndex)
                            {
                                hasGrandchildren = true;
                                break;
                            }
                        }
                        if (hasGrandchildren) continue; // this node can no longer be merged (because a child was split)

                        a.octree.Merge(toMerge.index);
                        break;
                    }
                }
                
                a.octree.Split(toSplit.index);
            }
        };
        doSplits();
        
        while (!mergePrio.empty())
        {
            // - to do: merge remaining merge candidates, if possible (and not below merge threshold)
            break; // - placeholder
        }

        // Update upload buffers:
        std::function<void(NodeIndex, unsigned, glm::dvec4)> stageGroup = [&](NodeIndex gi, unsigned depth, glm::dvec4 pos)
        {
            StageSplit(it, gi, pos);
            for (NodeIndex ci = 0u; ci < NodeArity; ++ci)
            {
                auto childPos = pos;
                childPos.w *= 0.5;
                childPos += (glm::dvec4(glm::uvec3(ci, ci >> 1u, ci >> 2u) & 1u, 0.5) * 2.0 - 1.0)* childPos.w;
                const auto ni = Octree::GroupAndChildToNode(gi, ci);
                const auto children = a.octree.GetNode(ni).children;
                if (InvalidIndex == children) continue;
                stageGroup(children, depth + 1u, childPos);
            }
        };
        stageGroup(a.rootGroupIndex, 0u, {0, 0, 0, 1});

        // - to do: better way to time
        auto endTime = Util::Timer::Clock::now();
        auto time = endTime - startTime;
        auto duration = time / std::chrono::milliseconds(1);
        //std::cout << "Atmosphere update in " << duration << " ms\n";
    }
}
