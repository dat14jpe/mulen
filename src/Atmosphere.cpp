#include "Atmosphere.hpp"
#include "Camera.hpp"
#include <math.h>
#include <functional>
#include "util/Timer.hpp"


namespace Mulen {
    bool Atmosphere::Init(const Atmosphere::Params& p)
    {
        vao.Create();

        const bool moreMemory = true; // - for quick switching during development
        hasTransmittance = false;

        auto setTextureFilter = [](Util::Texture& tex, GLenum filter)
        {
            glTextureParameteri(tex.GetId(), GL_TEXTURE_MIN_FILTER, filter);
            glTextureParameteri(tex.GetId(), GL_TEXTURE_MAG_FILTER, filter);
        };

        // - to do: calculate actual number of nodes and bricks allowed/preferred from params
        const size_t numNodeGroups = 16384u * (moreMemory ? 5u : 1u); // - to do: just multiply by 3, or even 1 (though that last 1 is quite optimistic...)
        const size_t numBricks = numNodeGroups * NodeArity;
        octree.Init(numNodeGroups, numBricks);

        Util::Texture::Dim maxWidth, brickRes, width, height, depth;
        maxWidth = 4096u; // - to do: retrieve actual system-dependent limit programmatically
        brickRes = BrickRes;
        const auto cellsPerBrick = (BrickRes - 1u) * (BrickRes - 1u) * (BrickRes - 1u);
        width = maxWidth - (maxWidth % brickRes);
        texMap.x = width / brickRes;
        texMap.y = glm::min(maxWidth / brickRes, unsigned(numBricks + texMap.x - 1u) / texMap.x);
        texMap.z = (unsigned(numBricks) + texMap.x * texMap.y - 1u) / (texMap.x * texMap.y);
        width = texMap.x * brickRes;
        height = texMap.y * brickRes;
        depth = texMap.z * brickRes;
        std::cout << numNodeGroups << " node groups (" << numBricks << " bricks, " << (cellsPerBrick * numBricks) / 1000000u << " M voxel cells)\n";
        std::cout << "Atmosphere texture size: " << texMap.x << "*" << texMap.y << "*" << texMap.z << " bricks, "
            << width << "*" << height << "*" << depth << " texels (multiple of "
            << width * height * depth / (1024 * 1024) << " MB)\n";

        auto setUpBrickTexture = [&](Util::Texture& tex, GLenum internalFormat, GLenum filter)
        {
            tex.Create(GL_TEXTURE_3D, 1u, internalFormat, width, height, depth);
            setTextureFilter(tex, filter);
        };
        auto setUpBrickLightTexture = [&](Util::Texture& tex)
        {
            setUpBrickTexture(tex, BrickLightFormat, GL_LINEAR);
        };

        for (auto i = 0u; i < std::extent<decltype(gpuStates)>::value; ++i)
        {
            auto& state = gpuStates[i];
            state.gpuNodes.Create(sizeof(NodeGroup) * numNodeGroups, 0u);
            setUpBrickTexture(state.brickTexture, BrickFormat, GL_LINEAR);
            setUpBrickLightTexture(state.brickLightTexture);

            // - the map *could* be mipmapped. Hmm. Maybe try it, if there's a need
            const auto mapRes = 64u; // - to do: try different values and measure performance
            state.octreeMap.Create(GL_TEXTURE_3D, 1u, GL_R32UI, mapRes, mapRes, mapRes);
            setTextureFilter(state.octreeMap, GL_NEAREST);
        }
        setUpBrickLightTexture(brickLightTextureTemp);

        transmittanceTexture.Create(GL_TEXTURE_2D, 1u, GL_RGBA16F, 256, 64);
        setTextureFilter(transmittanceTexture, GL_LINEAR);
        glTextureParameteri(transmittanceTexture.GetId(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(transmittanceTexture.GetId(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        const auto ScatterRSize = 32u, ScatterMuSize = 128u, ScatterMuSSize = 32u, ScatterNuSize = 8u;
        scatterTexture.Create(GL_TEXTURE_3D, 1u, GL_RGBA16F, ScatterNuSize * ScatterMuSSize, ScatterMuSize, ScatterRSize);
        setTextureFilter(scatterTexture, GL_LINEAR);
        glTextureParameteri(scatterTexture.GetId(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(scatterTexture.GetId(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(scatterTexture.GetId(), GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        maxToUpload = numNodeGroups;// / 8; // - arbitrary (to do: take care to make it high enough for timely updates)
        nodesToUpload.reserve(maxToUpload);
        gpuUploadNodes.Create(sizeof(UploadNodeGroup) * maxToUpload * NodeArity, GL_DYNAMIC_STORAGE_BIT);
        gpuUploadBricks.Create(sizeof(UploadBrick) * maxToUpload * NodeArity, GL_DYNAMIC_STORAGE_BIT);
        // - to do: also create brick upload buffer/texture

        auto stageSplit = [&](NodeIndex gi)
        {
            StageNodeGroup(UploadType::Split, gi);
            for (NodeIndex ci = 0u; ci < NodeArity; ++ci)
            {
                const auto ni = Octree::GroupAndChildToNode(gi, ci);
                StageBrick(UploadType::Split, ni);
            }
        };

        auto t = timer.Begin("Initial atmosphere splits");

        // For this particular atmosphere:
        rootGroupIndex = octree.RequestRoot();
        stageSplit(rootGroupIndex);

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
                    auto p = glm::dvec3(childPos) * scale;
                    const auto size = childPos.w * scale;
                    const Object::Position sphereCenter{ 0.0 };
                    const auto height = 0.05, radius = 1.0; // - to do: check/correct these values
                    const auto atmRadius2 = (radius + height) * (radius + height);

                    auto bmin = p - size, bmax = p + size;
                    const auto dist2 = glm::distance2(glm::clamp(sphereCenter, bmin, bmax), sphereCenter);
                    if (dist2 > atmRadius2) continue; // outside
                    bool anyOutside = false;
                    for (int z = -1; z <= 1; z += 2)
                        for (int y = -1; y <= 1; y += 2)
                            for (int x = -1; x <= 1; x += 2)
                            {
                                if (glm::distance2(sphereCenter, p + glm::dvec3(x, y, z) * size) > radius* radius) anyOutside = true;
                            }
                    if (!anyOutside) continue; // inside

                }

                if (!octree.nodes.GetNumFree()) return;
                const auto& children = octree.GetNode(ni).children;
                if (InvalidIndex == children)
                {
                    octree.Split(ni);
                    stageSplit(children);
                }
                testSplit(children, depth - 1u, childPos);
            }
        };
        auto testSplitRoot = [&](unsigned depth)
        {
            for (auto i = 1u; i <= depth; ++i)
            {
                testSplit(rootGroupIndex, i, glm::dvec4{ 0, 0, 0, 1 });
            }
        };
        const auto testDepth = 6u + (moreMemory ? 1u : 0u); // - can't be higher than 5 with current memory constraints and waste
        testSplitRoot(testDepth);
        const auto res = (2u << testDepth) * (BrickRes - 1u);
        std::cout << "Voxel resolution: " << res << " (" << 2e-3 * planetRadius * scale / res << " km/voxel)\n";

        //std::cout << "glGetError:" << __LINE__ << ": " << glGetError() << "\n";
        return true;
    }

    void Atmosphere::SetUniforms(Util::Shader& shader)
    {
        const auto lightDir = glm::normalize(glm::vec3(1, 0.6, 0.4));

        // - to do: use a UBO instead
        shader.Uniform1u("rootGroupIndex", glm::uvec1{ rootGroupIndex });
        shader.Uniform3u("uBricksRes", glm::uvec3{ texMap });
        shader.Uniform3f("bricksRes", glm::vec3{ texMap });
        shader.Uniform1f("stepSize", glm::vec1{ 1 });
        shader.Uniform1f("planetRadius", glm::vec1{ (float)planetRadius });
        shader.Uniform1f("atmosphereRadius", glm::vec1{ (float)(planetRadius * scale) });
        shader.Uniform1f("atmosphereScale", glm::vec1{ (float)scale });
        shader.Uniform1f("atmosphereHeight", glm::vec1{ (float)height });
        shader.Uniform3f("lightDir", lightDir);
        shader.Uniform3f("sun", glm::vec3{ sunDistance, sunRadius, sunIntensity });

        shader.Uniform1f("HR", glm::vec1((float)HR));
        shader.Uniform3f("betaR", betaR);
        shader.Uniform1f("HM", glm::vec1((float)HM));
        shader.Uniform1f("betaMEx", glm::vec1((float)betaMEx));
        shader.Uniform1f("betaMSca", glm::vec1((float)betaMSca));
        shader.Uniform1f("mieG", glm::vec1((float)mieG));
        shader.Uniform1f("Rg", glm::vec1{ (float)planetRadius });
        shader.Uniform1f("Rt", glm::vec1{ (float)(planetRadius + height * 2.0) });

        // - tuning these is important to avoid visual banding/clamping
        shader.Uniform1f("offsetR", glm::vec1{ 2.0f });
        shader.Uniform1f("scaleR", glm::vec1{ -20.0f });
        shader.Uniform1f("offsetM", glm::vec1{ 20.0f });
        shader.Uniform1f("scaleM", glm::vec1{ -80.0f });

        // https://outerra.blogspot.com/2013/07/logarithmic-depth-buffer-optimizations.html
        // - to do: use actual far plane (parameter from outside the Atmosphere class?)
        const double farplane = 1e8;
        const double Fcoef = 2.0 / log2(farplane + 1.0);
        shader.Uniform1f("Fcoef", glm::vec1{ float(Fcoef) });
        shader.Uniform1f("Fcoef_half", glm::vec1{ float(0.5 * Fcoef) });
    }

    bool Atmosphere::ReloadShaders(const std::string& path)
    {
        const std::string shaderPath = path + "atmosphere/";
        auto loadShader = [&](Util::Shader& shader, const std::string& name, bool compute)
        {
            const auto base = shaderPath + name;
            if (!compute)
                return shader.Create({ base + "_vert.glsl", base + "_frag.glsl" });
            else
                return shader.Create({ "", "", base + ".glsl" });
        };
        if (!loadShader(postShader, "../post", false)) return false;
        if (!loadShader(backdropShader, "backdrop", false)) return false;
        if (!loadShader(transmittanceShader, "transmittance", true)) return false;
        if (!loadShader(inscatterFirstShader, "inscatter_first", true)) return false;
        if (!loadShader(updateShader, "update_nodes", true)) return false;
        if (!loadShader(updateBricksShader, "update_bricks", true)) return false;
        if (!loadShader(updateLightShader, "update_lighting", true)) return false;
        if (!loadShader(lightFilterShader, "filter_lighting", true)) return false;
        if (!loadShader(updateOctreeMapShader, "update_octree_map", true)) return false;
        if (!loadShader(renderShader, "render", false)) return false;
        return true;
    }

    void Atmosphere::Update(bool update, const Camera& camera)
    {
        // - to do: divide this over multiple frames (while two past states are being interpolated)
        {
            // Update atmosphere:
            // - to do: split/merge based on camera and nodes being not fully opaque or transparent

            // Full node structure update:
            // (to replace partial updates, or just for animation? To decide upon)
            //gpuNodes.Upload(0, sizeof(NodeGroup) * octree.nodes.GetSize(), &octree.GetGroup(0u));

            // - to do: run animation update
        }


        auto& state = gpuStates[0]; // - to do: correct index
        auto ssboIndex = 0u;
        state.gpuNodes.BindBase(GL_SHADER_STORAGE_BUFFER, ssboIndex++);
        gpuUploadNodes.BindBase(GL_SHADER_STORAGE_BUFFER, ssboIndex++);
        gpuUploadBricks.BindBase(GL_SHADER_STORAGE_BUFFER, ssboIndex++);
        // - to do: also bind the old texture for reading
        // (initially just testing writing directly to one)

        auto setShader = [&](Util::Shader& shader) -> Util::Shader&
        {
            shader.Bind();
            SetUniforms(shader);
            return shader;
        };

        if (!hasTransmittance)
        {
            hasTransmittance = true;

            {
                auto t = timer.Begin("Transmittance");
                setShader(transmittanceShader);
                const glm::uvec3 workGroupSize{ 32u, 32u, 1u };
                auto& tex = transmittanceTexture;
                glBindImageTexture(0u, tex.GetId(), 0, GL_FALSE, 0, GL_WRITE_ONLY, tex.GetFormat());
                glDispatchCompute(tex.GetWidth() / workGroupSize.x, tex.GetHeight() / workGroupSize.y, tex.GetDepth() / workGroupSize.z);
                glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
            }
            {
                transmittanceTexture.Bind(5u);
                auto t = timer.Begin("Inscatter");
                setShader(inscatterFirstShader);
                const glm::uvec3 workGroupSize{ 8u, 8u, 8u };
                auto& tex = scatterTexture;
                glBindImageTexture(0u, tex.GetId(), 0, GL_FALSE, 0, GL_WRITE_ONLY, tex.GetFormat());
                glDispatchCompute(tex.GetWidth() / workGroupSize.x, tex.GetHeight() / workGroupSize.y, tex.GetDepth() / workGroupSize.z);
                glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
            }
        }

        auto updateMap = [&](GpuState& state)
        {
            auto t = timer.Begin("Map");
            setShader(updateOctreeMapShader);
            const glm::uvec3 resolution{ state.octreeMap.GetWidth(), state.octreeMap.GetHeight(), state.octreeMap.GetDepth() };
            updateOctreeMapShader.Uniform3f("resolution", glm::vec3(resolution));
            glBindImageTexture(0u, state.octreeMap.GetId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);
            const auto groups = resolution / 8u;
            glDispatchCompute(groups.x, groups.y, groups.z);
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
        };
        auto updateNodes = [&](uint64_t num)
        {
            //auto t = timer.Begin("Nodes");
            setShader(updateShader);
            glDispatchCompute((GLuint)num, 1u, 1u);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        };
        auto generateBricks = [&](GpuState& state, uint64_t first, uint64_t num)
        {
            //auto t = timer.Begin("Generation");
            glBindImageTexture(0u, state.brickTexture.GetId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, BrickFormat);
            setShader(updateBricksShader);
            updateBricksShader.Uniform1u("brickUploadOffset", glm::uvec1{ (unsigned)first });
            glDispatchCompute((GLuint)num, 1u, 1u);
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
        };

        // - to do: probably remove this, eventually, in favour of always loading continuously
        // Update GPU data:
        static bool firstUpdate = true; // - temporary ugliness, before this is replaced by continuous updates
        if (nodesToUpload.size() && firstUpdate)
        {
            firstUpdate = false;
            std::cout << "Uploading " << nodesToUpload.size() << " node groups\n";
            std::cout << "Generating " << bricksToUpload.size() << " bricks\n";

            for (auto& uploadGroup : nodesToUpload)
            {
                const auto old = uploadGroup.nodeGroup;
                uploadGroup.nodeGroup = octree.GetGroup(uploadGroup.groupIndex);
            }
            gpuUploadNodes.Upload(0, sizeof(UploadNodeGroup) * nodesToUpload.size(), nodesToUpload.data());
            gpuUploadBricks.Upload(0, sizeof(UploadBrick) * bricksToUpload.size(), bricksToUpload.data());

            auto& state = gpuStates[0]; // - to do: choose correct index when swapping for continuous updates
            state.brickTexture.Bind(0u);
            state.octreeMap.Bind(2u);

            // - to do: upload brick data

            updateNodes(nodesToUpload.size());
            updateMap(state);
            generateBricks(state, 0u, bricksToUpload.size());

            {
                auto t = timer.Begin("Lighting");
                glBindImageTexture(0u, brickLightTextureTemp.GetId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, BrickLightFormat);
                setShader(updateLightShader);
                glDispatchCompute((GLuint)bricksToUpload.size(), 1u, 1u);
                glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
            }

            {
                auto t = timer.Begin("Light filter");
                glBindImageTexture(0u, state.brickLightTexture.GetId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, BrickLightFormat);
                setShader(lightFilterShader);
                brickLightTextureTemp.Bind(1u);
                glDispatchCompute((GLuint)bricksToUpload.size(), 1u, 1u);
                glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
            }

            /*nodesToUpload.resize(0u);
            bricksToUpload.resize(0u);*/
        }

        // - prototype continuous update:
        if (update)
        {
            auto setStage = [&](unsigned i)
            {
                updateStage = (UpdateStage)i;
                updateFraction = 0.0;
                updateStageIndex0 = updateStageIndex1 = 0u;
            };
            if (updateStage == UpdateStage::Finished) // start a new iteration?
            {
                setStage(0);
                // - to do: state index swap
                // - to do: get new octree version from other thread (... which is also to do)
                // (and update upload vectors accordingly)
            }

            auto& state = gpuStates[0]; // - to do: correct index
            state.brickTexture.Bind(0u);
            state.octreeMap.Bind(2u);

            const auto rate = 1.0 / 60.0; // - to do: make configurable
            auto fraction = 0.0;

            auto computeNum = [&](size_t total, uint64_t done)
            {
                if (total <= done) return 0u; // all done
                return glm::min(unsigned(total - done), glm::max(unsigned(1u), unsigned(fraction * total)));
            };

            switch (updateStage)
            {
            case UpdateStage::UploadAndGenerate:
            {
                fraction = (1.0 / 0.4) * rate; // - to do: try to automatically adjust via measurements of time taken? Maybe

                // - to do: make sure to take care of lower-depth nodes first, as children may want to access parent data

                const auto numNodes = computeNum(nodesToUpload.size(), updateStageIndex0),
                    numBricks = computeNum(bricksToUpload.size(), updateStageIndex1);

                if (numNodes)
                {
                    auto& last = updateStageIndex0;
                    gpuUploadNodes.Upload(0, sizeof(UploadNodeGroup) * numNodes, nodesToUpload.data() + last);
                    updateNodes(numNodes);
                    last += numNodes;
                }

                if (numBricks)
                {
                    auto& last = updateStageIndex1;
                    gpuUploadBricks.Upload(sizeof(UploadBrick) * last, sizeof(UploadBrick) * numBricks, bricksToUpload.data() + last);
                    generateBricks(state, last, numBricks);
                    std::cout << "Generating brick uploads " << last << " through " << last + numBricks << "\n";
                    last += numBricks;
                }

                break;
            }

            case UpdateStage::Map:
                fraction = 1.0;
                updateMap(state);
                break;

            case UpdateStage::Lighting:
            {
                fraction = (1.0 / 0.4) * rate;
                // - to do
                break;
            }

            case UpdateStage::LightFilter:
            {
                fraction = (1.0 / 0.2) * rate;
                // - to do
                break;
            }
            }

            updateFraction += fraction;
            if (updateFraction >= 1.0) setStage(unsigned(updateStage) + 1u);
        }
    }

    void Atmosphere::Render(const glm::ivec2& res, double time, const Camera& camera)
    {
        // Resize the render targets if resolution has changed.
        if (depthTexture.GetWidth() != res.x || depthTexture.GetHeight() != res.y)
        {
            depthTexture.Create(GL_TEXTURE_2D, 1u, GL_DEPTH24_STENCIL8, res.x, res.y);
            for (auto i = 0u; i < 2u; ++i)
            {
                lightTextures[i].Create(GL_TEXTURE_2D, 1u, GL_RGB16F, res.x, res.y);
                fbos[i].Create();
                fbos[i].SetDepthBuffer(depthTexture, 0u);
                fbos[i].SetColorBuffer(0u, lightTextures[i], 0u);
            }
        }

        const auto worldMat = glm::translate(Object::Mat4{ 1.0 }, position - camera.GetPosition());
        const auto viewMat = camera.GetOrientationMatrix();
        const auto projMat = camera.GetProjectionMatrix();
        const auto viewProjMat = projMat * viewMat;
        const auto invWorldMat = glm::inverse(worldMat);
        const auto invViewProjMat = glm::inverse(projMat * viewMat);

        auto setUpShader = [&](Util::Shader& shader) -> Util::Shader&
        {
            shader.Bind();
            shader.Uniform1f("time", glm::vec1(static_cast<float>(time)));
            shader.UniformMat4("invViewProjMat", invViewProjMat);
            shader.UniformMat4("invViewMat", glm::inverse(viewMat));
            shader.UniformMat4("invProjMat", glm::inverse(projMat));
            shader.UniformMat4("invWorldMat", invWorldMat);
            shader.UniformMat4("viewProjMat", viewProjMat);
            shader.Uniform3f("planetLocation", GetPosition() - camera.GetPosition());
            SetUniforms(shader);
            return shader;
        };

        fbos[0].Bind();
        auto& state = gpuStates[0]; // - to do: correct index
        state.gpuNodes.BindBase(GL_SHADER_STORAGE_BUFFER, 0u);
        state.brickTexture.Bind(0u);
        state.brickLightTexture.Bind(1u);
        state.octreeMap.Bind(2u);
        depthTexture.Bind(3u);
        transmittanceTexture.Bind(5u);
        scatterTexture.Bind(6u);
        vao.Bind();

        { // "planet" background (to do: spruce this up, maybe move elsewhere)
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glClearDepth(1.0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            auto& shader = setUpShader(backdropShader);
            glDrawArrays(GL_TRIANGLES, 0, 2u * 3u);
        }
        
        { // atmosphere
            fbos[1].Bind();
            glClear(GL_COLOR_BUFFER_BIT);
            lightTextures[0].Bind(4u);
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            auto& shader = setUpShader(renderShader);
            glDrawArrays(GL_TRIANGLES, 0, 2u * 3u);
        }

        { // postprocessing
            glDisable(GL_BLEND);
            Util::Framebuffer::BindBackbuffer();
            auto& shader = postShader;
            shader.Bind();
            lightTextures[1].Bind(0u);
            glDrawArrays(GL_TRIANGLES, 0, 2u * 3u);
        }

        timer.EndFrame();
    }

    void Atmosphere::StageNodeGroup(UploadType type, NodeIndex groupIndex)
    {
        // - to do: check that we don't exceed maxNumUpload here, or leave that to the caller?
        uint32_t genData = 0u;
        genData |= uint32_t(type) << 24u;
        UploadNodeGroup upload{};
        upload.groupIndex = groupIndex;
        upload.genData = genData;
        upload.nodeGroup = octree.GetGroup(groupIndex);
        nodesToUpload.push_back(upload);
    }

    void Atmosphere::StageBrick(UploadType type, NodeIndex nodeIndex)
    {
        // - to do: check that we don't exceed maxNumUpload here, or leave that to the caller?
        const auto brickIndex = nodeIndex;
        uint32_t genData = 0u; // - to do: also add upload brick data index, when applicable
        genData |= uint32_t(type) << 24u;

        glm::vec3 nodeCenter{ 0.0, 0.0, 0.0 };
        float nodeSize = 1.0;
        for (auto ni = nodeIndex; ni != InvalidIndex; ni = octree.GetGroup(Octree::NodeToGroup(ni)).parent)
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
        bricksToUpload.push_back(upload);
    }
}
