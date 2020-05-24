#include "Updater.hpp"
#include "Atmosphere.hpp"
#include <functional>
#include <queue>
#include <unordered_set>
#include <iostream>
#include "util/Timer.hpp"
#include <numeric>

namespace Mulen::Atmosphere {

    Updater::Updater(Atmosphere& atmosphere)
        : generator{ "generator" }
        , featureGenerator{ "feature_generator" }
        , thread(&Updater::UpdateLoop, this)
        , octree{ atmosphere.octree }
    {

    }

    bool Updater::NodeInAtmosphere(const UpdateIteration& it, const glm::dvec4& childPos)
    {
        // - simple test to only split those in spherical atmosphere shell:
        auto p = glm::dvec3(childPos) * it.params.scale;
        const auto size = childPos.w * it.params.scale;
        const Object::Position sphereCenter{ 0.0 };
        const auto height = 0.005, radius = 1.0; // - to do: check/correct these values
        const auto atmRadius2 = (radius + height) * (radius + height);
        const auto innerRadius = radius 
            - it.params.height * 5e-4 / it.params.planetRadius // - to do: tune this (to always avoid problems near the surface)
            ;

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

    void Updater::InitialSetup(Atmosphere& atmosphere)
    {
        // First we have to ensure no data races by waiting for the other thread.
        std::unique_lock<std::mutex> lk{ mutex };
        cv.wait(lk, [&] { return nextUpdateReady; });
        lk.unlock();

        // Then split to a predefined depth.
        // - to do: enable splitting to a determined depth and location, especially for use in benchmarking)
        auto& it = GetUpdateIteration();
        it.params.scale = atmosphere.scale;
        it.params.planetRadius = atmosphere.planetRadius;

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

                if (!atmosphere.octree.nodes.GetNumFree()) return;
                const auto& children = atmosphere.octree.GetNode(ni).children;
                if (InvalidIndex == children)
                {
                    atmosphere.octree.Split(ni);
                    StageSplit(it, children, childPos);
                }
                testSplit(children, depth - 1u, childPos);
            }
        };
        auto testSplitRoot = [&](unsigned depth)
        {
            for (auto i = 1u; i <= depth; ++i)
            {
                testSplit(atmosphere.octree.rootGroupIndex, i, glm::dvec4{ 0, 0, 0, 1 });
            }
        };

        StageSplit(it, atmosphere.octree.rootGroupIndex, { 0.0, 0.0, 0.0, 1.0 });
        const auto testDepth = 6u;
        testSplitRoot(testDepth);
        const auto res = (2u << testDepth) * (BrickRes - 1u);
        std::cout << "Voxel resolution: " << res << " (" << 2e-3 * atmosphere.planetRadius * atmosphere.scale / res << " km/voxel)\n";
    }

    Updater::~Updater()
    {
        std::unique_lock<std::mutex> lk{ mutex };
        done = true;
        lk.unlock();
        cv.notify_one();
        thread.join();
    }

    Util::Shader& Updater::SetShader(Atmosphere& atmosphere, Util::Shader& shader)
    {
        shader.Bind();
        atmosphere.SetUniforms(shader);
        return shader;
    }

    void Updater::UpdateMap(Atmosphere& atmosphere, Util::Texture& octreeMap, glm::vec3 pos, glm::vec3 scale, unsigned depthOffset)
    {
        auto& shader = SetShader(atmosphere, atmosphere.updateOctreeMapShader);
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

    void Updater::UpdateNodes(Atmosphere& atmosphere, uint64_t num)
    {
        SetShader(atmosphere, atmosphere.updateShader);
        glDispatchCompute((GLuint)num, 1u, 1u);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    void Updater::GenerateBricks(Atmosphere& atmosphere, GpuState& state, Generator& gen, uint64_t first, uint64_t num)
    {
        glBindImageTexture(0u, state.brickTexture.GetId(), 0, GL_TRUE, 0, GL_READ_WRITE, BrickFormat);
        {
            //auto t = timer.Begin("Generation");
            auto& shader = SetShader(atmosphere, gen.GetShader());
            shader.Uniform1u("brickUploadOffset", glm::uvec1{ (unsigned)first });
            glDispatchCompute((GLuint)num, 1u, 1u);
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        }
        { // "optimisation" pass (compute constancy flags, possibly more)
            auto& shader = SetShader(atmosphere, atmosphere.updateFlagsShader);
            shader.Uniform1u("brickUploadOffset", glm::uvec1{ (unsigned)first });
            glDispatchCompute((GLuint)num, 1u, 1u);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
    }

    void Updater::LightBricks(Atmosphere& atmosphere, GpuState& state, uint64_t first, uint64_t num, const Object::Position& lightDir, const Util::Timer::DurationMeta& timerMeta)
    {
        const auto numGroups = num / NodeArity; // - to do: num groups as parameter instead, to disallow incorrect use
        auto& groupsTex = atmosphere.brickLightPerGroupTexture;

        auto setShader = [&](Util::Shader& shader)
        {
            SetShader(atmosphere, shader);
            shader.Uniform1u("brickUploadOffset", glm::uvec1{ (unsigned)first });

            // Scale to cover the node group, translate outside it, and rotate to face the light.
            auto ori = Object::Position{ 0, 0, 1 }; // original facing
            auto axis = glm::cross(ori, lightDir);
            Object::Orientation q = Object::Orientation(glm::dot(ori, lightDir), axis);
            q.w += glm::length(q);
            q = glm::normalize(q);

            const auto scaleFactor = sqrt(3.0);
            Object::Mat4 mat = glm::scale(Object::Mat4{ 1.0 }, Object::Position(scaleFactor));
            mat = glm::translate(mat, glm::dvec3(0.0, 0.0, sqrt(3.0)));
            mat = glm::toMat4(q) * mat;

            // Translation is unnecessary for the lookup.
            Object::Mat4 invMat = glm::scale(Object::Mat4{ 1.0 }, Object::Position(1.0 / scaleFactor))
                * glm::toMat4(glm::conjugate(q));

            shader.UniformMat4("groupLightMat", mat);
            shader.UniformMat4("invGroupLightMat", invMat);
            shader.Uniform3u("uGroupsRes", glm::uvec3(groupsTex.GetWidth(), groupsTex.GetHeight(), groupsTex.GetDepth()) / LightPerGroupRes);
        };

        // First mini 2D shadow maps per group.
        {
            auto t = atmosphere.timer.Begin(Profiler_UpdateLightPerGroup, timerMeta);
            glBindImageTexture(0u, atmosphere.brickLightPerGroupTexture.GetId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16);
            setShader(atmosphere.updateLightPerGroupShader);
            glDispatchCompute((GLuint)numGroups, 1u, 1u);
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
        }

        // Then per-voxel shadowing, accelerated by the per-group shadow maps.
        {
            auto t = atmosphere.timer.Begin(Profiler_UpdateLightPerVoxel, timerMeta);
            atmosphere.brickLightPerGroupTexture.Bind(3u);
            glBindImageTexture(0u, atmosphere.brickLightTextureTemp.GetId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, BrickLightFormat);
            setShader(atmosphere.updateLightShader);
            glDispatchCompute((GLuint)num, 1u, 1u);
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
        }
    }

    void Updater::FilterLighting(Atmosphere& atmosphere, GpuState& state, uint64_t first, uint64_t num)
    {
        //auto t = timer.Begin("Light filter");
        //glBindImageTexture(0u, state.brickLightTexture.GetId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, BrickLightFormat);
        glBindImageTexture(0u, state.brickTexture.GetId(), 0, GL_TRUE, 0, GL_READ_WRITE, BrickFormat);
        auto& shader = SetShader(atmosphere, atmosphere.lightFilterShader);
        shader.Uniform1u("brickUploadOffset", glm::uvec1{ (unsigned)first });
        atmosphere.brickLightTextureTemp.Bind(1u);
        glDispatchCompute((GLuint)num, 1u, 1u);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    void Updater::OnFrame(Atmosphere& atmosphere, const UpdateIteration::Parameters& params, double period)
    {
        auto& a = atmosphere;
        auto& timer = a.timer;
        const auto fps = 60.0; // - to do: measure/adjust
        const auto rate = period / fps;
        const auto dt = 1.0 / 60.0; // - to do: use actual time (though maybe not *directly*)

        if (stages.empty())
        {
            // - these are just hardcoded estimates for now, but they should really
            // be continuously measured and estimated by the program

            stages.push_back({ Stage::Id::Init,         Profiler_UpdateInit, 0.1 });
            stages.push_back({ Stage::Id::Generate,     Profiler_UpdateGenerate, 40.0 });
            stages.push_back({ Stage::Id::Map,          Profiler_UpdateMap, 1.0 });
            stages.push_back({ Stage::Id::Light,        Profiler_UpdateLight, 200.0 });
            stages.push_back({ Stage::Id::Filter,       Profiler_UpdateFilter, 15.0 });

            for (auto& stage : stages) totalStagesTime += stage.cost;
        }

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

            Util::Timer::DurationMeta timerMeta;
            timerMeta.factor = 1.0;
            auto computeWorkSize = [&](size_t total)
            {
                /*std::cout << "Starting from " << last << std::endl;
                std::cout << "relative stage cost: " << stage.cost / totalStagesTime << ", remaining spend: " << maxFrameCost - frameCost << std::endl;
                std::cout << "Computing work size for a total of " << total << " (maxFraction: " << maxFraction << ")" << std::endl;*/
                totalItems = total;
                numToDo = totalItems - last;
                numToDo = glm::min(numToDo, glm::max((size_t)1u, size_t(glm::ceil(totalItems * maxFraction))));
                timerMeta.factor = double(numToDo) / double(totalItems);
            };

            switch (stage.id)
            {
            case Stage::Id::Init:
            {
                auto t = timer.Begin(stage.str, timerMeta);

                // Update stage costs based on measured GPU times.
                bool allStagesProfiled = true;
                for (auto& stage : stages)
                {
                    if (!timer.GetTimings(stage.str).gpuTimes.Size())
                    {
                        allStagesProfiled = false;
                        break;
                    }
                }
                if (allStagesProfiled)
                {
                    //std::cout << "Profile data available on init." << std::endl;
                    totalStagesTime = 0.0;

                    for (auto& stage : stages)
                    {
                        const auto window = 50ull; // - to do: adjust
                        auto& t = timer.GetTimings(stage.str).gpuTimes;
                        const auto num = static_cast<int>(glm::min(window, t.Size()));
                        auto sum = 0.0;
                        for (auto i = 0; i < num; ++i)
                        {
                            sum += t[-i].duration / t[-i].meta.factor;
                        }
                        auto duration = sum / double(num);
                        duration *= 1e3;
                        
                        //std::cout << stage.str << ": " << duration << " (over " << num << ")" << std::endl;
                        stage.cost = duration;
                        totalStagesTime += duration;
                    }
                }

                // Communicate with the update thread.
                std::unique_lock<std::mutex> lk{ mutex };
                if (nextUpdateReady) // has the worker thread completed its iteration?
                {
                    nextUpdateReady = false;
                    updateIteration = (updateIteration + 1ull) % std::extent<decltype(iterations)>::value;
                    // - actually wrong time (to do: compute correct one-second-into-the-future-from-last-iteration)
                    GetUpdateIteration().params = params;
                    lk.unlock();
                    cv.notify_one();
                }
                else
                {
                    return; // nothing to do (update-wise) until the worker thread is done
                }

                updateStateIndex = (updateStateIndex + 1ull) % std::extent<decltype(a.gpuStates)>::value;
                updateFraction = 0.0;
                totalItems = numToDo = 1u;

                // Interpolate old state for split nodes using their current parents.
                // (to avoid temporal seams in animation interpolation)
                {
                    auto t = timer.Begin(Profiler_UpdateInitSplits);

                    auto& prevState = atmosphere.gpuStates[(updateStateIndex + 1u) % std::extent<decltype(atmosphere.gpuStates)>::value];
                    auto& state = atmosphere.gpuStates[(updateStateIndex + 2u) % std::extent<decltype(atmosphere.gpuStates)>::value];

                    state.gpuNodes.BindBase(GL_SHADER_STORAGE_BUFFER, 0u);
                    prevState.brickTexture.Bind(0u);
                    glBindImageTexture(0u, prevState.brickTexture.GetId(), 0, GL_TRUE, 0, GL_READ_WRITE, BrickFormat);

                    atmosphere.gpuGenData.Upload(0, sizeof(NodeIndex) * priorSplitGroups.size(), priorSplitGroups.data());
                    atmosphere.gpuGenData.BindBase(GL_SHADER_STORAGE_BUFFER, 3u);
                    auto& shader = SetShader(atmosphere, atmosphere.initSplitsShader);
                    glDispatchCompute((GLuint)(priorSplitGroups.size() * NodeArity), 1u, 1u);
                    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

                    priorSplitGroups.swap(GetRenderIteration().splitGroups);
                }

                break;
            }
            case Stage::Id::Generate:
            {
                computeWorkSize(it.nodesToUpload.size()); // assuming num bricks = num node groups * NodeArity (which should always be true)
                if (numToDo)
                {
                    auto t = timer.Begin(stage.str, timerMeta);
                    const auto bricksOffset = last * NodeArity, numBricks = numToDo * NodeArity;

                    // Convert generation data offsets and sizes.
                    const auto startingOffset = it.bricksToUpload[bricksOffset].genDataOffset;
                    uint32_t genDataSize = 0u;
                    for (auto i = last; i < last + numBricks; ++i)
                    {
                        auto& b = it.bricksToUpload[i];
                        b.genDataOffset -= startingOffset;
                        genDataSize += b.genDataSize;
                    }
                    if (genDataSize) // upload generator-specific data, if it exists
                    {
                        a.gpuGenData.Upload(0, genDataSize * sizeof(decltype(it.genData)::value_type), it.genData.data() + startingOffset);
                        // - maybe to do: automatically resize GPU buffer if needed? At least check for overflow and log the error
                    }
                    // - to do: use generator-specific shader for brick generation

                    a.gpuUploadNodes.Upload(0, sizeof(UploadNodeGroup) * numToDo, it.nodesToUpload.data() + last);
                    UpdateNodes(atmosphere, numToDo);
                    a.gpuUploadBricks.Upload(sizeof(UploadBrick) * bricksOffset, sizeof(UploadBrick) * numBricks, it.bricksToUpload.data() + bricksOffset);
                    GenerateBricks(atmosphere, state, *params.generator, bricksOffset, numBricks);
                }
                break;
            }
            case Stage::Id::Map:
            {
                auto t = timer.Begin(stage.str, timerMeta);
                UpdateMap(atmosphere, state.octreeMap);
                totalItems = numToDo = 1u;
                break;
            }
            case Stage::Id::Light:
            {
                computeWorkSize(it.bricksToUpload.size() / NodeArity);
                if (numToDo)
                {
                    auto t = timer.Begin(stage.str, timerMeta);
                    LightBricks(atmosphere, state, last * NodeArity, numToDo * NodeArity, params.lightDirection, timerMeta);
                }
                break;
            }
            case Stage::Id::Filter:
            {
                computeWorkSize(it.bricksToUpload.size());
                if (numToDo)
                {
                    auto t = timer.Begin(stage.str, timerMeta);
                    FilterLighting(atmosphere, state, last, numToDo);
                }
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

    void Updater::StageNodeGroup(UpdateIteration& it, UploadType type, NodeIndex groupIndex)
    {
        // - to do: check that we don't exceed maxNumUpload here, or leave that to the caller?
        uint32_t genData = 0u;
        genData |= uint32_t(type) << 24u;
        UploadNodeGroup upload{};
        upload.groupIndex = groupIndex;
        upload.genData = genData;
        upload.nodeGroup = octree.GetGroup(groupIndex);
        it.nodesToUpload.push_back(upload);
    }

    void Updater::StageBrick(UpdateIteration& it, UploadType type, NodeIndex nodeIndex, const glm::vec4& nodePos, uint32_t genDataOffset, uint32_t genDataSize)
    {
        // - to do: check that we don't exceed maxNumUpload here, or leave that to the caller?
        const auto brickIndex = nodeIndex;

        UploadBrick upload{};
        upload.nodeIndex = nodeIndex;
        upload.brickIndex = brickIndex;
        upload.genDataOffset = genDataOffset;
        upload.genDataSize = genDataSize;
        upload.nodeLocation = nodePos;

        it.bricksToUpload.push_back(upload);
    }

    void Updater::StageSplit(UpdateIteration& it, NodeIndex gi, const glm::vec4& nodePos)
    {
        StageNodeGroup(it, UploadType::Split, gi);
        for (NodeIndex ci = 0u; ci < NodeArity; ++ci)
        {
            auto childPos = nodePos; // - to do: modify for child
            childPos.w *= 0.5;
            childPos += glm::vec4(glm::vec3(glm::ivec3(ci & 1u, (ci >> 1u) & 1u, (ci >> 2u) & 1u) * 2 - 1) * childPos.w, 0.0);
            const auto ni = Octree::GroupAndChildToNode(gi, ci);
            StageBrick(it, UploadType::Split, ni, childPos, 0u, 0u); // - to do: actual generation data values
        }
    }

    void Updater::UpdateLoop()
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

    void Updater::ComputeIteration(UpdateIteration& it)
    {
        const auto startTime = Util::Timer::Clock::now();

        it.nodesToUpload.resize(0u);
        it.bricksToUpload.resize(0u);
        it.splitGroups.resize(0u);
        it.genData.resize(0u);
        it.maxDepth = 0u;

        // - to do: reset generator-specific data? Or it can do that itself
        generator.Generate(it);

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
        const auto camPos = it.params.cameraPosition * it.params.scale;
        const auto h = glm::length(camPos) - 1.0;
        const auto r = 1.0;
        const auto cloudTop = 0.005; // - to do: retrieve from somewhere else
        // - to try: only use ground horizon here, and test the angle for nodes beyond that
        const auto horizonDist = 
            sqrt(h * (h + 2.0 * r)) // distance to ground horizon
            + sqrt(cloudTop * (cloudTop + 2.0 * r)) // distance to cloud horizon
            ;

        // Update octree (eventually in a separate copy so that the render thread may do intersections/lookups in its own copy)
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
                const auto nodePos = Object::Position(childPos) * it.params.scale;
                const auto nodeSize = childPos.w * it.params.scale;
                const auto nodeMin = nodePos - nodeSize, nodeMax = nodePos + nodeSize;
                if (!NodeInAtmosphere(it, childPos)) continue; // - do we also need to see if this can merge? To do

                const auto ni = Octree::GroupAndChildToNode(gi, ci);
                const auto children = octree.GetNode(ni).children;
                hasGrandchildren = hasGrandchildren || InvalidIndex != children;
                //const auto insideNode = glm::all(glm::lessThan(glm::abs(camPos - nodePos) / nodeSize, glm::dvec3{ 1.0 }));
                const auto distanceToNode = glm::length(glm::max(glm::dvec3(0.0), glm::max(nodeMin - camPos, camPos - nodeMax)));
                const auto insideNode = distanceToNode == 0.0;

                // Frustum culling (prototypical):
                if (it.params.doFrustumCulling)
                {
                    // - to do: make sure not to cull potentially shadowing parts of the atmosphere

                    auto scale = it.params.planetRadius;
                    auto aabbPos = scale * (nodePos - camPos);
                    auto aabbSize = nodeSize * scale;

                    auto getNegativeVertex = [&](const glm::dvec3& normal)
                    {
                        auto pv = glm::dvec3(aabbSize);
                        if (normal.x >= 0.0) pv.x *= -1.0;
                        if (normal.y >= 0.0) pv.y *= -1.0;
                        if (normal.z >= 0.0) pv.z *= -1.0;
                        return aabbPos + pv;
                    };
                    auto insideFrustum = [&]()
                    {
                        for (auto i = 0u; i < 5u; ++i)
                        {
                            auto pi = it.params.viewFrustum.planes[i];
                            auto pos = pi.w;
                            auto normal = glm::dvec3(pi);

                            if (glm::dot(normal, getNegativeVertex(normal)) + pos > 0.0) return false; // outside
                            // - to do, possibly: can test for intersection (not necessarily wholly inside) with positive vertex
                        }
                        return true; // inside
                    };

                    // - prototypical:
                    if (!insideNode && !insideFrustum())
                    {
                        // - to do: unify with cloud horizon check below
                        if (InvalidIndex != children)
                        {
                            if (!computePriority(children, depth + 1u, childPos)) // - to let children insert themselves in the merge priority
                                mergePrio.push({ ni, 0.0 }); // - experimental
                        }
                        continue;
                    }
                }

                // Check if inside (cloud) horizon
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
                if (!computePriority(children, depth + 1u, childPos) && (!insideNode || depth >= MaxDepth))
                {
                    // No grandchildren, which means this node is eligible for merging.
                    if (depth >= MaxDepth) priority = 0.0; // ensure too detailed nodes are merged
                    mergePrio.push({ ni, priority });
                }
            }
            return hasGrandchildren;
        };
        computePriority(octree.rootGroupIndex, 0u, {0, 0, 0, 1});

        const auto maxSplits = octree.GetNodeGroupCapacity() / 10u; // - this is fairly arbitrary. Maybe make it configurable?
        auto numSplits = 0ull, numMerges = 0ull;
        // - to do: compute merge threshold (below which nodes won't be merged unless needed)
        auto mergeThreshold = 1e20; // - arbitrary (but it shouldn't be)
        auto doSplits = [&]()
        {
            while (!splitPrio.empty())
            {
                auto toSplit = splitPrio.top();
                splitPrio.pop();

                if (!octree.nodes.GetNumFree()) // are we out of octree memory?
                {
                    while (true)
                    {
                        if (mergePrio.empty()) return; // no more to merge
                        auto toMerge = mergePrio.top();
                        if (toMerge.priority > toSplit.priority) return; // all merge candidates are of higher priority than split candidates
                        mergePrio.pop();

                        auto hasGrandchildren = false;
                        const auto children = octree.GetNode(toMerge.index).children;
                        for (auto ci = 0u; ci < NodeArity; ++ci)
                        {
                            if (octree.GetNode(Octree::GroupAndChildToNode(children, ci)).children != InvalidIndex)
                            {
                                hasGrandchildren = true;
                                break;
                            }
                        }
                        if (hasGrandchildren) continue; // this node can no longer be merged (because a child was split)

                        ++numMerges;
                        octree.Merge(toMerge.index);
                        break;
                    }
                }
                
                octree.Split(toSplit.index);
                it.splitGroups.push_back(octree.GetNode(toSplit.index).children);
                if (++numSplits >= maxSplits) break;
            }
        };
        doSplits();

        //std::cout << "Splits: " << numSplits << ", merges: " << numMerges << std::endl;
        
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
                const auto children = octree.GetNode(ni).children;
                if (InvalidIndex == children) continue;
                stageGroup(children, depth + 1u, childPos);
            }
        };
        stageGroup(octree.rootGroupIndex, 0u, {0, 0, 0, 1});

        // - to do: better way to time
        auto endTime = Util::Timer::Clock::now();
        auto time = endTime - startTime;
        auto duration = time / std::chrono::milliseconds(1);
        //std::cout << "Atmosphere update in " << duration << " ms\n";
    }
}
