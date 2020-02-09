#pragma once

#include <vector>
#include "util/VertexArray.hpp"
#include "util/Buffer.hpp"
#include "util/Texture.hpp"
#include "util/Shader.hpp"
#include "util/Framebuffer.hpp"
#include "Octree.hpp"
#include "Object.hpp"

namespace Util {
    class Timer;
}

namespace Mulen {
    class Camera;

    class Atmosphere : public Object
    {
        double planetRadius = 6371000.0;
        double height = 50000.0; // - to do: correct
        double scale = 1.1;

        double HR = 8.0e3; // Rayleigh scale height
        glm::dvec3 betaR = { 5.8e-6, 1.35e-5, 3.31e-5 };
        double HM = 1.2e3; // Mie scale height
        double mieG = 0.8;
        double betaMSca = 2e-5;
        double betaMEx = betaMSca / 0.9;

        double sunDistance = 1.5e11, sunRadius = 6.957e8, sunIntensity = 1e1; // - to do: make intensity physically based


        Octree octree;
        NodeIndex rootGroupIndex;

        // Render:
        Util::Shader postShader;
        Util::Framebuffer fbos[2];
        Util::Texture depthTexture, lightTextures[2];

        Util::Texture octreeMap;
        Util::Buffer gpuNodes;
        Util::Texture brickTexture, brickLightTexture;
        Util::VertexArray vao;
        Util::Shader backdropShader, renderShader;
        glm::uvec3 texMap;
        // Two channels since bricks store both last and next values, to let rendering interpolate between them.
        static const auto
            BrickFormat = GL_RG16, // - visible banding if only 8 bits per channel. Maybe can be resolved with generation dithering?
            BrickLightFormat = GL_RGBA16F;
        
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
        Util::Shader updateShader, updateBricksShader, updateLightShader, updateOctreeMapShader;
        void StageNodeGroup(UploadType, NodeIndex ni);
        void StageBrick(UploadType, NodeIndex ni); // - to do: also brick data (at least optionally, if/when generating on GPU)

        void SetUniforms(Util::Shader&);


        Util::Timer& timer;


    public:
        Atmosphere(Util::Timer& timer) : timer{ timer } {}

        struct Params
        {
            size_t memBudget, gpuMemBudget;
        };
        bool Init(const Params&);
        bool ReloadShaders(const std::string& shaderPath);

        void Update(const Camera&);
        void Render(const glm::ivec2& res, double time, const Camera&);

        void RecomputeLighting();

        double GetPlanetRadius() const { return planetRadius; }
    };
}
