#pragma once

#include <vector>
#include "util/VertexArray.hpp"
#include "util/Buffer.hpp"
#include "util/Texture.hpp"
#include "util/Shader.hpp"
#include "util/Framebuffer.hpp"
#include "Octree.hpp"
#include "Object.hpp"

namespace Mulen {
    class Camera;

    class Atmosphere : public Object
    {
        // - to do: make all values physically plausible (for Earth, initially)
        double planetRadius = 10.0;
        double height = 0.2;
        double scale = 1.1;


        Octree octree;
        NodeIndex rootGroupIndex;

        // Render:
        Util::Shader postShader;
        Util::Framebuffer fbo;
        Util::Texture depthTexture, lightTexture; // - to do: maybe go full deferred (i.e. add at least colour)

        Util::Buffer gpuNodes;
        Util::Texture brickTexture;
        Util::VertexArray vao;
        Util::Shader backdropShader, renderShader;
        glm::uvec3 texMap;
        // Two channels since bricks store both last and next values, to let rendering interpolate between them.
        static const auto BrickFormat = GL_RG8; // - might need to be 16 bits. Let's find out
        
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
            //uint32_t padding[2];
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
        void StageNodeGroup(UploadType, NodeIndex ni);
        void StageBrick(UploadType, NodeIndex ni); // - to do: also brick data (at least optionally, if/when generating on GPU)

        void SetUniforms(Util::Shader&);


    public:
        struct Params
        {
            size_t memBudget, gpuMemBudget;
        };
        bool Init(const Params&);
        bool ReloadShaders(const std::string& shaderPath);

        void Update(const Camera&);
        void Render(const glm::ivec2& res, double time, const Camera&);

        double GetPlanetRadius() const { return planetRadius; }
    };
}
