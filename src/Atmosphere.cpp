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

        // - to do: calculate actual number of nodes and bricks allowed/preferred from params
        const size_t numNodeGroups = 16384u * (moreMemory ? 3u : 1u);
        const size_t numBricks = numNodeGroups * NodeArity;
        octree.Init(numNodeGroups, numBricks);
        gpuNodes.Create(sizeof(NodeGroup) * numNodeGroups, 0u);

        Util::Texture::Dim maxWidth, brickRes, width, height, depth;
        maxWidth = 4096u; // - to do: retrieve actual system-dependent limit programmatically
        brickRes = BrickRes;
        width = maxWidth - (maxWidth % brickRes);
        texMap.x = width / brickRes;
        texMap.y = glm::min(maxWidth / brickRes, unsigned(numBricks + texMap.x - 1u) / texMap.x);
        texMap.z = (unsigned(numBricks) + texMap.x * texMap.y - 1u) / (texMap.x * texMap.y);
        height = texMap.y * brickRes;
        depth = texMap.z * brickRes;
        std::cout << "Atmosphere texture size: " << texMap.x << "*" << texMap.y << "*" << texMap.z << " bricks, " 
            << width << "*" << height << "*" << depth << " texels (multiple of "
            << (width * height * depth / (1024 * 1024)) << " MB)\n";
        auto setUpTexture = [&](Util::Texture& tex, GLenum internalFormat)
        {
            const auto filter = GL_LINEAR;
            tex.Create(GL_TEXTURE_3D, 1u, internalFormat, width, height, depth);
            glTextureParameteri(tex.GetId(), GL_TEXTURE_MIN_FILTER, filter);
            glTextureParameteri(tex.GetId(), GL_TEXTURE_MAG_FILTER, filter);
        };
        setUpTexture(brickTexture, BrickFormat);
        setUpTexture(brickLightTexture, BrickLightFormat);

        // - the map *could* be mipmapped. Hmm. Maybe try it, if there's a need
        const auto mapRes = 64u; // - to do: try different values and measure performance
        octreeMap.Create(GL_TEXTURE_3D, 1u, GL_R32UI, mapRes, mapRes, mapRes);
        glTextureParameteri(octreeMap.GetId(), GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(octreeMap.GetId(), GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        // - to do: also create second texture (for double-buffering updates)

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
        const auto testDepth = 5u + (moreMemory ? 1u : 0u); // - can't be higher than 5 with current memory constraints and waste
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

        shader.Uniform1f("HR", glm::vec1((float)HR));
        shader.Uniform3f("betaR", betaR);
        shader.Uniform1f("HM", glm::vec1((float)HM));
        shader.Uniform1f("betaMEx", glm::vec1((float)betaMEx));
        shader.Uniform1f("betaMSca", glm::vec1((float)betaMSca));
        shader.Uniform1f("mieG", glm::vec1((float)mieG));

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
        if (!loadShader(updateShader, "update_nodes", true)) return false;
        if (!loadShader(updateBricksShader, "update_bricks", true)) return false;
        if (!loadShader(updateLightShader, "update_lighting", true)) return false;
        if (!loadShader(updateOctreeMapShader, "update_octree_map", true)) return false;
        if (!loadShader(renderShader, "render", false)) return false;
        return true;
    }

    void Atmosphere::Update(const Camera& camera)
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

        // Update GPU data:
        if (nodesToUpload.size())
        {
            std::cout << "Uploading " << nodesToUpload.size() << " node groups\n";
            std::cout << "Generating " << bricksToUpload.size() << " bricks\n";

            for (auto& uploadGroup : nodesToUpload)
            {
                const auto old = uploadGroup.nodeGroup;
                uploadGroup.nodeGroup = octree.GetGroup(uploadGroup.groupIndex);
            }
            gpuUploadNodes.Upload(0, sizeof(UploadNodeGroup) * nodesToUpload.size(), nodesToUpload.data());
            gpuUploadBricks.Upload(0, sizeof(UploadBrick) * bricksToUpload.size(), bricksToUpload.data());

            // - to do: upload brick data

            auto ssboIndex = 0u;
            gpuNodes.BindBase(GL_SHADER_STORAGE_BUFFER, ssboIndex++);
            gpuUploadNodes.BindBase(GL_SHADER_STORAGE_BUFFER, ssboIndex++);
            gpuUploadBricks.BindBase(GL_SHADER_STORAGE_BUFFER, ssboIndex++);
            // - to do: also bind the old texture for reading
            // (initially just testing writing directly to one)

            auto setShader = [&](Util::Shader& shader)
            {
                shader.Bind();
                SetUniforms(shader);
            };

            {
                auto t = timer.Begin("Nodes");
                setShader(updateShader);
                glDispatchCompute((GLuint)nodesToUpload.size(), 1u, 1u);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            }

            {
                auto t = timer.Begin("Map");
                setShader(updateOctreeMapShader);
                const glm::uvec3 resolution{ octreeMap.GetWidth(), octreeMap.GetHeight(), octreeMap.GetDepth() };
                updateOctreeMapShader.Uniform3f("resolution", glm::vec3(resolution));
                glBindImageTexture(0u, octreeMap.GetId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);
                const auto groups = resolution / 8u;
                glDispatchCompute(groups.x, groups.y, groups.z);
                glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
            }

            {
                auto t = timer.Begin("Generation");
                glBindImageTexture(0u, brickTexture.GetId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, BrickFormat);
                setShader(updateBricksShader);
                glDispatchCompute((GLuint)bricksToUpload.size(), 1u, 1u);
                glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
            }

            {
                auto t = timer.Begin("Lighting");
                glBindImageTexture(0u, brickLightTexture.GetId(), 0, GL_TRUE, 0, GL_WRITE_ONLY, BrickLightFormat);
                brickTexture.Bind(0u);
                octreeMap.Bind(2u);
                setShader(updateLightShader);
                glDispatchCompute((GLuint)bricksToUpload.size(), 1u, 1u);
                glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
            }

            nodesToUpload.resize(0u);
            bricksToUpload.resize(0u);
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
        auto ssboIndex = 0u, texUnit = 0u;
        gpuNodes.BindBase(GL_SHADER_STORAGE_BUFFER, ssboIndex++);
        brickTexture.Bind(0u);
        brickLightTexture.Bind(1u);
        octreeMap.Bind(2u);
        depthTexture.Bind(3u);
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
