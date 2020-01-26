#include "Atmosphere.hpp"
#include "Camera.hpp"
#include <math.h>
#include <functional>
#include "util/Timer.hpp"


namespace Mulen {
    bool Atmosphere::Init(const Atmosphere::Params& p)
    {
        vao.Create();

        // - to do: calculate actual number of nodes and bricks allowed/preferred from params
        const size_t numNodeGroups = 16384u;
        const size_t numBricks = numNodeGroups * NodeArity;
        octree.Init(numNodeGroups, numBricks);
        gpuNodes.Create(sizeof(NodeGroup) * numNodeGroups, 0u);

        Util::Texture::Dim maxWidth, brickRes, width, height, depth;
        maxWidth = 4096u; // - to do: retrieve actual system-dependent limit programmatically
        brickRes = BrickRes;
        width = maxWidth - (maxWidth % brickRes);
        texMap.x = width / brickRes;
        texMap.y = (numBricks + texMap.x - 1u) / texMap.x;
        texMap.z = 1u;
        height = texMap.y * brickRes;
        depth = brickRes; // - to do: overflow into this when one layer of bricks is not enough
        std::cout << "Atmosphere texture size: " << texMap.x << "*" << texMap.y << "*" << texMap.z << " bricks, " 
            << width << "*" << height << "*" << depth << " texels (multiple of "
            << (width * height * depth / (1024 * 1024)) << " MB)\n";
        brickTexture.Create(GL_TEXTURE_3D, 1u, BrickFormat, width, height, depth);
        glTextureParameteri(brickTexture.GetId(), GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(brickTexture.GetId(), GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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

        auto timer = Util::Timer{ "Initial atmosphere splits" };

        // For this particular atmosphere:
        rootGroupIndex = octree.RequestRoot();
        stageSplit(rootGroupIndex);

        // - test: "manual" splits, indiscriminately to a chosen level
        std::function<void(NodeIndex, unsigned, glm::dvec4)> testSplit = [&](NodeIndex gi, unsigned depth, glm::dvec4 pos)
        {
            if (!depth) return;
            for (NodeIndex ci = 0u; ci < NodeArity; ++ci)
            {
                const auto ni = Octree::GroupAndChildToNode(gi, ci);

                auto childPos = pos;
                childPos.w *= 0.5;
                childPos += (glm::dvec4(ci & 1u, (ci >> 1u) & 1u, (ci >> 2u) & 1u, 0.5) * 2.0 - 1.0) * childPos.w;
                //if (false)
                {
                    // - simple test to only split those in spherical atmosphere shell:
                    auto p = glm::dvec3(childPos) * scale;
                    const auto size = childPos.w * scale;
                    const Object::Position sphereCenter{ 0.0 };
                    const auto height = 0.05, radius = 1.0; // - to do: check/correct these values
                    const auto atmRadius2 = (radius + height) * (radius + height);

                    /*const auto dist = glm::length(p - sphereCenter);
                    const auto margin = sqrt(3) * size;
                    if (dist - margin - (radius + height) > 0.0) continue; // outside
                    if (dist + margin - radius < 0.0) continue; // inside*/

                    // - alternative:
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
                    if (!anyOutside) continue;
                }

                if (!octree.nodes.GetNumFree()) return;
                octree.Split(ni);
                const auto children = octree.GetNode(ni).children;
                stageSplit(children);
                testSplit(children, depth - 1u, childPos);
            }
        };
        auto testSplitRoot = [&](unsigned depth)
        {
            testSplit(rootGroupIndex, depth, glm::dvec4{ 0, 0, 0, 1 });
        };
        testSplitRoot(5u); // - can't be higher than 5 with current memory constraints and waste

        //std::cout << "glGetError:" << __LINE__ << ": " << glGetError() << "\n";
        return true;
    }

    void Atmosphere::SetUniforms(Util::Shader& shader)
    {
        // - to do: use a UBO instead
        shader.Uniform1u("rootGroupIndex", glm::uvec1{ rootGroupIndex });
        shader.Uniform3u("uBricksRes", glm::uvec3{ texMap });
        shader.Uniform3f("bricksRes", glm::vec3{ texMap });
        shader.Uniform1i("brickTexture", glm::ivec1{ 0 });
        shader.Uniform1f("stepSize", glm::vec1{ 1 });
        shader.Uniform1f("planetRadius", glm::vec1{ (float)planetRadius });
        shader.Uniform1f("atmosphereRadius", glm::vec1{ (float)(planetRadius * scale) });
        shader.Uniform1f("atmosphereScale", glm::vec1{ (float)scale });

        // https://outerra.blogspot.com/2013/07/logarithmic-depth-buffer-optimizations.html
        // - to do: use actual far plane (parameter from outside the Atmosphere class?)
        const double farplane = 1e4;
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
            GLuint imgUnit = 0u;
            glBindImageTexture(imgUnit++, brickTexture.GetId(), 0, false, 0, GL_WRITE_ONLY, BrickFormat);

            updateShader.Bind();
            SetUniforms(updateShader);
            glDispatchCompute((GLuint)nodesToUpload.size(), 1u, 1u);
            std::cout << "Uploading " << nodesToUpload.size() << " node groups\n";

            updateBricksShader.Bind();
            SetUniforms(updateBricksShader);
            glDispatchCompute((GLuint)bricksToUpload.size(), 1u, 1u);
            std::cout << "Generating " << bricksToUpload.size() << " bricks\n";

            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

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
            lightTexture.Create(GL_TEXTURE_2D, 1u, GL_RGB16F, res.x, res.y);
            fbo.Create();
            fbo.SetDepthBuffer(depthTexture, 0u);
            fbo.SetColorBuffer(0u, lightTexture, 0u);
        }

        const auto worldMat = glm::translate(Object::Mat4{ 1.0 }, position);
        const auto viewMat = camera.GetViewMatrix();
        const auto projMat = camera.GetProjectionMatrix();
        const auto worldViewMat = viewMat * worldMat;
        const auto worldViewProjMat = projMat * worldViewMat;
        const auto invWorldViewMat = glm::inverse(worldViewMat);
        const auto invWorldViewProjMat = glm::inverse(worldViewProjMat);
        const auto invViewProjMat = glm::inverse(projMat * viewMat);

        auto setUpShader = [&](Util::Shader& shader) -> Util::Shader&
        {
            shader.Bind();
            shader.Uniform1f("time", glm::vec1(static_cast<float>(time)));
            shader.UniformMat4("invWorldViewMat", invWorldViewMat);
            shader.UniformMat4("invWorldViewProjMat", invWorldViewProjMat);
            shader.UniformMat4("invViewProjMat", invViewProjMat);
            shader.UniformMat4("worldViewProjMat", worldViewProjMat);
            shader.UniformMat4("invViewMat", glm::inverse(viewMat));
            shader.UniformMat4("invProjMat", glm::inverse(projMat));
            SetUniforms(shader);
            return shader;
        };

        fbo.Bind();

        { // "planet" background (to do: spruce this up, maybe move elsewhere)
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glClearDepth(1.0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            vao.Bind();
            auto& shader = setUpShader(backdropShader);
            glDrawArrays(GL_TRIANGLES, 0, 2u * 3u);
        }
        
        { // atmosphere
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            vao.Bind();
            auto& shader = setUpShader(renderShader);
            SetUniforms(shader);
            auto ssboIndex = 0u, texUnit = 0u;
            gpuNodes.BindBase(GL_SHADER_STORAGE_BUFFER, ssboIndex++);
            brickTexture.Bind(texUnit++);
            shader.Uniform1i("depthTexture", glm::ivec1{ int(texUnit) });
            depthTexture.Bind(texUnit++);
            glDrawArrays(GL_TRIANGLES, 0, 2u * 3u);
        }

        { // postprocessing
            Util::Framebuffer::BindBackbuffer();
            auto& shader = postShader;
            shader.Bind();
            lightTexture.Bind(0u);
            glDrawArrays(GL_TRIANGLES, 0, 2u * 3u);
        }
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
