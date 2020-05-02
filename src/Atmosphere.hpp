#pragma once
#include "Common.hpp"
#include <vector>
#include "util/VertexArray.hpp"
#include "util/Framebuffer.hpp"
#include "AtmosphereUpdater.hpp"

namespace Util {
    class Timer;
}

namespace Mulen {
    class Camera;
    class LightSource;

    class Atmosphere : public Object
    {
        friend class AtmosphereUpdater;

        double planetRadius = 6371e3, height = 50e3, cloudMaxHeight = 25e3;
        double scale = 1.1;

        double HR = 8.0e3; // Rayleigh scale height
        glm::dvec3 betaR = { 5.8e-6, 1.35e-5, 3.31e-5 };
        double HM = 1.2e3; // Mie scale height
        double mieG = 0.8;
        double betaMSca = 2e-5; // Mie scattering
        double betaMEx = betaMSca / 0.9; // Mie extinction


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

        GpuState gpuStates[2];
        Util::Texture brickLightTextureTemp;
        Util::VertexArray vao;
        Util::Shader backdropShader, renderShader, resolveShader;
        glm::uvec3 texMap;
        Util::Texture octreeMap; // per-frame frustum-encompassing octree map
        Object::Mat4 prevViewProjMat;
        
        // Update:
        Util::Texture brickUploadTexture;
        size_t maxToUpload; // maximum per frame
        Util::Buffer gpuUploadNodes, gpuUploadBricks;
        Util::Shader updateShader, updateBricksShader, updateFlagsShader, updateLightShader, updateOctreeMapShader, lightFilterShader;

        // Prepass:
        Util::Shader transmittanceShader, inscatterFirstShader;
        Util::Texture transmittanceTexture, scatterTexture;
        bool hasTransmittance = false; // - to do: per-atmosphere

        void SetUniforms(Util::Shader&);


        Util::Timer& timer;

        double renderTime = 0.0;
        double time = 0.0, lightTime = 0.0; // *animation* time and *light* time

        AtmosphereUpdater updater;

        void UpdateUniforms(const Camera&, const LightSource&);

    public:
        Atmosphere(Util::Timer& timer) : timer{ timer }, updater{ *this } {}

        struct Params
        {
            size_t memBudget, gpuMemBudget;
        };
        bool Init(const Params&);
        bool ReloadShaders(const std::string& shaderPath);


        struct UpdateParams
        {
            bool update, animate, rotateLight;
            int depthLimit;
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

        double GetAnimationTime() const { return time; }
        double GetLightTime() const { return lightTime; }
        void SetAnimationTime(double t) { time = t; }
        void SetLightTime(double t) { lightTime = t; }

        void SetDownscaleFactor(unsigned f) { downscaleFactor = f; }
    };
}
