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
        Util::Buffer gpuUploadNodes, gpuUploadBricks;
        enum class UploadType
        {
            Split, Merge, Update
        };
        struct UploadNodeGroup
        {
            NodeIndex groupIndex;
            uint32_t genData;
            NodeGroup nodeGroup;
        };
        std::vector<UploadNodeGroup> nodesToUpload;
        struct UploadBrick
        {
            NodeIndex nodeIndex, brickIndex;
            uint32_t genData;
            uint32_t padding0;
            glm::vec4 nodeLocation;
        };
        std::vector<UploadBrick> bricksToUpload;
        Util::Shader updateShader, updateBricksShader;
        void StageNode(UploadType, NodeIndex ni);
        void StageBrick(UploadType, NodeIndex ni); // - to do: also brick data (at least optionally, if/when generating on GPU)


        // - to do: position and orientation management in some general "object" class
        glm::vec3 position;

        void SetUniforms(Util::Shader&);


    public:
        struct Params
        {
            size_t memBudget, gpuMemBudget;
        };
        bool Init(const Params&);
        bool ReloadShaders(const std::string& shaderPath);

        void Update(const Camera&);
        void Render(double time, const Camera&);
    };
}
