#pragma once
#include "Common.hpp"
#include <vector>
#include "util/VertexArray.hpp"
#include "util/Framebuffer.hpp"
#include "Updater.hpp"

namespace Util {
    class Timer;
}

namespace Mulen {
    class Camera;
    class LightSource;
}

namespace Mulen::Atmosphere {

    class Atmosphere : public Object
    {
        friend class Updater;

        double planetRadius = 6371e3, height = 50e3, cloudMaxHeight = 25e3;
        double scale = 1.1; // - should this also be configurable? Perhaps

        // - to do: all of these managed by model instead (and maybe the dimensions above as well)
        double HR = 8.0e3; // Rayleigh scale height
        glm::dvec3 betaR = { 5.8e-6, 1.35e-5, 3.31e-5 };
        double HM = 1.2e3; // Mie scale height
        double mieG = 0.8;
        double betaMSca = 2e-5 / 5.0; // Mie scattering
        double betaMEx = betaMSca / 0.9; // Mie extinction
        glm::dvec3 absorptionExtinction = { 6.497e-7, 1.881e-6, 8.502e-8 };
        double absorptionMiddle = 25.0e3, absorptionExtent = 15.0e3;


        Octree octree; // - to do: multiple octrees (2 or 3?) for multithreaded updates
        NodeIndex rootGroupIndex;

        // Render:
        Util::Buffer uniformBuffer;
        Util::Shader postShader;
        Util::Framebuffer fbo;
        Util::Texture depthTexture, lightTexture;
        struct FrameTextures
        {
            // - to do: probably also depth texture (to find the closest old pixel where needed)
            Util::Texture light, transmittance;
        } frameTextures[2]; // previous and current
        unsigned frame = 0u;
        unsigned downscaleFactor = 1u;

        GpuState gpuStates[3];
        Util::Texture brickLightTextureTemp, brickLightPerGroupTexture;
        Util::VertexArray vao;
        Util::Shader backdropShader, renderShader, resolveShader;
        glm::uvec3 texMap;
        Util::Texture octreeMap; // per-frame frustum-encompassing octree map
        Object::Mat4 prevViewProjMat;
        
        // Update:
        Util::Texture brickUploadTexture;
        size_t maxToUpload; // maximum per frame
        Util::Buffer gpuUploadNodes, gpuUploadBricks, gpuGenData;
        Util::Shader initSplitsShader, updateShader, updateFlagsShader, updateLightPerGroupShader, updateLightShader, updateOctreeMapShader, lightFilterShader;

        // Prepass:
        Util::Shader transmittanceShader, inscatterFirstShader;
        Util::Texture transmittanceTexture, scatterTexture;
        bool hasTransmittance = false; // - to do: per-atmosphere

        void SetUniforms(Util::Shader&);
        Object::Position lightDir;


        Util::Timer& timer;

        double renderTime = 0.0;
        double time = 0.0, lightTime = 0.0; // *animation* time and *light* time

        Updater updater;
        bool initUpdate = true;

        void UpdateUniforms(const Camera&, const LightSource&);

    public:
        Atmosphere(Util::Timer&);

        // - maybe to do: separate this into technical and physical parameters?
        struct Params
        {
            // Technical:
            size_t memBudget, gpuMemBudget;

            // Physical:

        };
        bool Init(const Params&);
        bool ReloadShaders(const std::string& shaderPath);


        struct UpdateParams
        {
            bool update, animate, rotateLight;
            int depthLimit;
            bool useFeatureGenerator = false;
        };
        void Update(double dt, const UpdateParams&, const Camera&, const LightSource&);
        void Render(const glm::ivec2& res, const Camera&, const LightSource&);

        double GetPlanetRadius() const { return planetRadius; }
        double GetHeight() const { return height; }
        double GetCloudMaxHeight() const { return cloudMaxHeight; }
        unsigned GetMaxDepth()
        {
            return updater.GetRenderIteration().maxDepth;
        }
        double ComputeVoxelSizeAtDepth(unsigned depth) const
        {
            const auto res = (2u << depth) * (BrickRes - 1u);
            return 2.0 * GetPlanetRadius() * scale / res;
        }

        double GetAnimationTime() const { return time; }
        double GetLightTime() const { return lightTime; }
        void SetAnimationTime(double t) { time = t; }
        void SetLightTime(double t) { lightTime = t; }

        void SetDownscaleFactor(unsigned f) { downscaleFactor = f; }
    };
}
