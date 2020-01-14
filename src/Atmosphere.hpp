#pragma once

#include <vector>
#include "util/VertexArray.hpp"
#include "util/Buffer.hpp"
#include "util/Texture.hpp"
#include "util/Shader.hpp"
#include "Octree.hpp"

namespace Mulen {
    class Camera;

    class Atmosphere
    {
        Octree octree;
        NodeIndex rootGroupIndex;

        // Render:
        Util::Buffer gpuNodes;
        Util::Texture brickTexture;
        Util::VertexArray vao;
        Util::Shader backdropShader, renderShader;
        glm::uvec3 texMap;
        static const auto BrickFormat = GL_RG8; // - maybe needs to be 16 bits. Let's find out
        
        // Update:
        Util::Texture brickUploadTexture;
        size_t maxToUpload; // maximum per frame
        Util::Buffer gpuUpload;
        struct UploadNode
        {
            enum class Type : uint8_t
            {
                Split, Merge, Update
            };
            NodeIndex nodeIndex;
            uint32_t genData; // Type or'ed into the highest byte
            Node node;
        };
        std::vector<UploadNode> nodesToUpload;
        Util::Shader updateShader, updateBricksShader;
        void StageNode(UploadNode::Type, NodeIndex);


    public:
        struct Params
        {
            size_t memBudget, gpuMemBudget;
        };
        bool Init(const Params&);
        bool ReloadShaders(const std::string& shaderPath);

        void Update(const Camera&);
        void Render(const Camera&);
    };
}
