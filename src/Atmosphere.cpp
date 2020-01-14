#include "Atmosphere.hpp"
#include "Camera.hpp"

namespace Mulen {
    bool Atmosphere::Init(const Atmosphere::Params& p)
    {
        vao.Create();

        // - to do: calculate actual number of nodes and bricks allowed/preferred from params
        const size_t numNodeGroups = 16384u;
        const size_t numBricks = numNodeGroups * NodeArity;
        octree.Init(numNodeGroups, numBricks);
        gpuNodes.Create(sizeof(Node) * numNodeGroups, 0u);

        Util::Texture::Dim maxWidth, brickRes, width, height, depth;
        maxWidth = 4096u; // - to do: retrieve actual system-dependent limit programmatically
        brickRes = BrickRes;
        width = maxWidth - (maxWidth % brickRes);
        texMap.x = width / brickRes;
        texMap.y = (numBricks + texMap.x - 1u) / texMap.x;
        texMap.z = 1u;
        height = texMap.y * brickRes;
        depth = brickRes; // - to do: overflow into this when one layer of bricks is not enough
        std::cout << "Atmosphere texture size: " << texMap.x << "*" << texMap.y << " bricks, " 
            << width << "*" << height << "*" << depth << " texels (multiple of "
            << (width * height * depth / (1024 * 1024)) << " MB)\n";
        brickTexture.Create(GL_TEXTURE_3D, 1u, BrickFormat, width, height, depth);
        // - to do: set up filtering (via sampler object?)
        // - to do: also create second texture (for double-buffering updates)

        maxToUpload = numNodeGroups; // - arbitrary (to do: take care to make it high enough for timely updates)
        nodesToUpload.reserve(maxToUpload);
        gpuUpload.Create(sizeof(UploadNode) * maxToUpload, GL_DYNAMIC_STORAGE_BIT);
        // - to do: also create brick upload buffer/texture

        // For this particular atmosphere:
        rootGroupIndex = octree.RequestRoot();
        for (auto ci = 0u; ci < NodeArity; ++ci)
        {
            StageNode(UploadNode::Type::Split, rootGroupIndex * NodeArity + ci);
        }

        //std::cout << "glGetError: " << glGetError() << "\n";
        return true;
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
        if (!loadShader(backdropShader, "backdrop", false)) return false;
        if (!loadShader(updateShader, "update_nodes", true)) return false;
        if (!loadShader(updateBricksShader, "update_bricks", true)) return false;
        if (!loadShader(renderShader, "render", false)) return false;
        return true;
    }

    void Atmosphere::Update(const Camera& camera)
    {
        // Update atmosphere:
        // - to do: split/merge based on camera

        // Update GPU data:
        if (nodesToUpload.size())
        {
            gpuUpload.Upload(0, sizeof(UploadNode) * nodesToUpload.size(), nodesToUpload.data());

            // - to do: upload brick data

            auto ssboIndex = 0u;
            gpuNodes.BindBase(GL_SHADER_STORAGE_BUFFER, ssboIndex++);
            gpuUpload.BindBase(GL_SHADER_STORAGE_BUFFER, ssboIndex++);
            // - to do: bind the old texture for reading and the new one for writing
            // (initially just testing writing directly to one)
            glBindImageTexture(0u, brickTexture.GetId(), 0, true, 0, GL_WRITE_ONLY, BrickFormat);
            updateShader.Bind();
            glDispatchCompute((GLuint)nodesToUpload.size(), 1u, 1u);
            updateBricksShader.Bind();
            // - to do: compute dispatch for brick uploads
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

            nodesToUpload.resize(0u);
        }
    }

    void Atmosphere::Render(const Camera& camera)
    {
        const auto viewMat = camera.GetViewMatrix();
        const auto invViewMat = glm::inverse(viewMat);
        const auto invViewProjMat = glm::inverse(camera.GetProjectionMatrix() * viewMat);

        auto setUpShader = [&](Util::Shader& shader) -> Util::Shader&
        {
            shader.Bind();
            shader.UniformMat4("invView", invViewMat);
            shader.UniformMat4("invViewProj", invViewProjMat);
            // - to do: object transform
            return shader;
        };

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        { // "planet" background (to do: spruce this up, maybe move elsewhere)
            // (and also make it render to a logarithmic depth buffer so the atmosphere can test against that)
            vao.Bind();
            auto& shader = setUpShader(backdropShader);
            glDrawArrays(GL_TRIANGLES, 0, 2u * 3u);
        }
        
        { // atmosphere render
            vao.Bind();
            auto& shader = setUpShader(renderShader);
            shader.Uniform1u("rootGroupIndex", glm::uvec1{ rootGroupIndex });
            auto ssboIndex = 0u;
            gpuNodes.BindBase(GL_SHADER_STORAGE_BUFFER, ssboIndex++);
            glDrawArrays(GL_TRIANGLES, 0, 2u * 3u);
        }
    }

    void Atmosphere::StageNode(UploadNode::Type type, NodeIndex nodeIndex)
    {
        // - to do: check that we don't exceed maxNumUpload here, or leave that to the caller?
        // - to do: generate brick data here?
        uint32_t genData = 0u; // - to do: upload brick index for SplitChild
        genData |= uint32_t(type) << 24u;
        nodesToUpload.push_back({ nodeIndex, genData, octree.GetNode(nodeIndex) });
        // - to do: also push brick... or maybe just do that on the update loop?
    }
}
