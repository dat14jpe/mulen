#include "Atmosphere.hpp"
#include "Camera.hpp"

namespace Mulen {
    bool Atmosphere::Init(const Atmosphere::Params& p)
    {
        position = glm::vec3(0, 0, -3);

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
        std::cout << "Atmosphere texture size: " << texMap.x << "*" << texMap.y << "*" << texMap.z << " bricks, " 
            << width << "*" << height << "*" << depth << " texels (multiple of "
            << (width * height * depth / (1024 * 1024)) << " MB)\n";
        brickTexture.Create(GL_TEXTURE_3D, 1u, BrickFormat, width, height, depth);
        glTextureParameteri(brickTexture.GetId(), GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(brickTexture.GetId(), GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // - to do: also create second texture (for double-buffering updates)

        maxToUpload = numNodeGroups; // - arbitrary (to do: take care to make it high enough for timely updates)
        nodesToUpload.reserve(maxToUpload);
        gpuUploadNodes.Create(sizeof(UploadNodeGroup) * maxToUpload, GL_DYNAMIC_STORAGE_BIT);
        gpuUploadBricks.Create(sizeof(UploadBrick) * maxToUpload, GL_DYNAMIC_STORAGE_BIT);
        // - to do: also create brick upload buffer/texture

        // For this particular atmosphere:
        rootGroupIndex = octree.RequestRoot();
        StageNode(UploadType::Split, rootGroupIndex);
        for (NodeIndex ci = 0u; ci < NodeArity; ++ci)
        {
            const auto ni = rootGroupIndex * NodeArity + ci;
            StageBrick(UploadType::Split, ni);
        }

        //std::cout << "glGetError:" << __LINE__ << ": " << glGetError() << "\n";
        return true;
    }

    void Atmosphere::SetUniforms(Util::Shader& shader)
    {
        // - to do: use a UBO instead
        shader.Uniform1u("rootGroupIndex", glm::uvec1{ rootGroupIndex });
        shader.Uniform3f("bricksRes", glm::vec3{ texMap });
        shader.Uniform1i("brickTexture", glm::ivec1{ 0 });
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
            glDispatchCompute((GLuint)nodesToUpload.size(), 1u, 1u);

            updateBricksShader.Bind();
            SetUniforms(updateBricksShader);
            glDispatchCompute((GLuint)bricksToUpload.size(), 1u, 1u);

            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

            nodesToUpload.resize(0u);
            bricksToUpload.resize(0u);
        }
    }

    void Atmosphere::Render(double time, const Camera& camera)
    {
        const auto worldMat = glm::translate(glm::mat4{ 1.f }, position);
        const auto worldViewMat = camera.GetViewMatrix() * worldMat;
        const auto invWorldViewMat = glm::inverse(worldViewMat);
        const auto invWorldViewProjMat = glm::inverse(camera.GetProjectionMatrix() * worldViewMat);
        

        auto setUpShader = [&](Util::Shader& shader) -> Util::Shader&
        {
            shader.Bind();
            shader.Uniform1f("time", glm::vec1(static_cast<float>(time)));
            shader.UniformMat4("invWorldViewMat", invWorldViewMat);
            shader.UniformMat4("invWorldViewProjMat", invWorldViewProjMat);
            // - to do: object transform
            SetUniforms(shader);
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
            SetUniforms(shader);
            auto ssboIndex = 0u, texUnit = 0u;
            gpuNodes.BindBase(GL_SHADER_STORAGE_BUFFER, ssboIndex++);
            brickTexture.Bind(texUnit++);
            glDrawArrays(GL_TRIANGLES, 0, 2u * 3u);
        }
    }

    void Atmosphere::StageNode(UploadType type, NodeIndex groupIndex)
    {
        // - to do: check that we don't exceed maxNumUpload here, or leave that to the caller?
        uint32_t genData = 0u;
        genData |= uint32_t(type) << 24u;
        nodesToUpload.push_back({ groupIndex, genData, octree.GetGroup(groupIndex) });
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

        // - to do: add various generation parameters (e.g. wind vector/temperature/humidity)
        bricksToUpload.push_back({ nodeIndex, brickIndex, genData, 0, glm::vec4(glm::vec3(nodeCenter), (float)nodeSize) });
    }
}
