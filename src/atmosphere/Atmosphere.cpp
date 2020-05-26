#include "Atmosphere.hpp"
#include "Camera.hpp"
#include <math.h>
#include <functional>
#include "util/Timer.hpp"
#include "LightSource.hpp"
#include "Model.hpp"


namespace Mulen::Atmosphere {

    struct Uniforms
    {
        // Observe the padding required by OpenGL std140 layout:

        glm::mat4 viewProjMat, invViewMat, invProjMat, invViewProjMat, worldMat, prevViewProjMat;

        glm::vec4 planetLocation, lightDir, sun;
        glm::vec4 betaR, absorptionExtinction;

        unsigned rootGroupIndex;
        float time, animationTime, animationAlpha,
            Fcoef_half, stepSize, 
            atmosphereRadius, planetRadius, atmosphereScale, atmosphereHeight;
        float Rt, Rg, cloudRadius;

        float HR, HM, absorptionMiddle, absorptionExtent;
        float betaMEx, betaMSca, mieG;

        float offsetR, scaleR, offsetM, scaleM;
    };

    Atmosphere::Atmosphere(Util::Timer& timer) 
        : timer{ timer }
        , updater{ *this }
    {
        // - to do: make use of model
        Model model{};
    }

    bool Atmosphere::Init(const Atmosphere::Params& p)
    {
        // - to do: make sure to discard old/ongoing computations from the other thread
        updater.WaitForUpdateReady(); // - is this enough? Be sure not to cause data races here...

        initUpdate = true;
        vao.Create();

        hasTransmittance = false;

        auto setTextureFilter = [](Util::Texture& tex, GLenum filter)
        {
            glTextureParameteri(tex.GetId(), GL_TEXTURE_MIN_FILTER, filter);
            glTextureParameteri(tex.GetId(), GL_TEXTURE_MAG_FILTER, filter);
        };

        // - to do: maybe consider CPU budget too, or just remove it altogether since this is now mostly GPU-bound
        const size_t numStates = 3u;
        const size_t voxelSize = 2u; // - to do: depend on format, if format becomes configurable (which it ought to)
        const size_t lightVoxelSize = 4u; // temporary lighting texture // - to do: also make this format-aware

        const size_t voxelsPerGroup = BrickRes3 * NodeArity;
        size_t gpuMemPerGroup = 0u; 
        gpuMemPerGroup += voxelSize* numStates* voxelsPerGroup; // render voxel stores
        gpuMemPerGroup += voxelsPerGroup * lightVoxelSize;      // temporary lighting
        gpuMemPerGroup += LightPerGroupRes * LightPerGroupRes * lightVoxelSize; // per-group shadow maps
        gpuMemPerGroup += sizeof(NodeGroup);                    // node store
        // - to do: add more terms

        const size_t numNodeGroups = p.gpuMemBudget / gpuMemPerGroup;//16384u * 3u; // - to do: make this controllable
        const size_t numBricks = numNodeGroups * NodeArity;
        octree.Init(numNodeGroups, numBricks);

        Util::Texture::Dim maxWidth, brickRes, width, height, depth;
        maxWidth = 4096u; // - to do: retrieve actual system-dependent limit programmatically
        brickRes = BrickRes;
        const auto cellsPerBrick = (BrickRes - 1u) * (BrickRes - 1u) * (BrickRes - 1u);
        width = maxWidth - (maxWidth % brickRes);
        texMap.x = width / brickRes;
        texMap.y = glm::min(maxWidth / brickRes, unsigned(numBricks + texMap.x - 1u) / texMap.x);
        texMap.z = (unsigned(numBricks) + texMap.x * texMap.y - 1u) / (texMap.x * texMap.y);

        auto computeRoot = [](size_t value, size_t n)
        {
            return static_cast<size_t>(std::ceil(std::pow(double(value), 1.0 / double(n))));
        };
        texMap.x = texMap.y = texMap.z = static_cast<decltype(texMap.x)>(computeRoot(numBricks, 3));
        width = texMap.x * brickRes;
        height = texMap.y * brickRes;
        depth = texMap.z * brickRes;
        std::cout << numNodeGroups << " node groups (" << numBricks << " bricks, " << (cellsPerBrick * numBricks) / 1000000u << " M voxel cells)\n";
        std::cout << "Atmosphere texture size: " << texMap.x << "*" << texMap.y << "*" << texMap.z << " bricks, "
            << width << "*" << height << "*" << depth << " texels (multiple of "
            << width * height * depth / (1024 * 1024) << " MB)\n";

        auto setUpBrickTexture = [&](Util::Texture& tex, GLenum internalFormat, GLenum filter)
        {
            tex.Create(GL_TEXTURE_3D, 1u, internalFormat, width, height, depth);
            setTextureFilter(tex, filter);
        };
        auto setUpBrickLightTexture = [&](Util::Texture& tex)
        {
            setUpBrickTexture(tex, BrickLightFormat, GL_LINEAR);
        };
        auto setUpBrickLightPerGroupTexture = [&](Util::Texture& tex)
        {
            auto res = static_cast<unsigned>(LightPerGroupRes * computeRoot(numNodeGroups, 2));
            tex.Create(GL_TEXTURE_2D, 1u, GL_R16, res, res);
            setTextureFilter(tex, GL_LINEAR);
            std::cout << "Brick light per group texture size: " << tex.GetWidth() << "*" << tex.GetHeight() << " = " << tex.GetWidth() * tex.GetHeight() << std::endl;
        };
        auto setUpMapTexture = [&](Util::Texture& tex)
        {
            // - the map *could* be mipmapped. Hmm. Maybe try it, if there's a need
            const auto mapRes = 64u; // - to do: try different values and measure performance
            tex.Create(GL_TEXTURE_3D, 1u, GL_R32UI, mapRes, mapRes, mapRes);
            setTextureFilter(tex, GL_NEAREST);
        };
        setUpMapTexture(octreeMap);

        for (auto i = 0u; i < std::extent<decltype(gpuStates)>::value; ++i)
        {
            auto& state = gpuStates[i];
            state.gpuNodes.Create(sizeof(NodeGroup) * numNodeGroups, 0u);
            setUpBrickTexture(state.brickTexture, BrickFormat, GL_LINEAR);
            setUpMapTexture(state.octreeMap);
        }
        setUpBrickLightTexture(brickLightTextureTemp);
        setUpBrickLightPerGroupTexture(brickLightPerGroupTexture);

        transmittanceTexture.Create(GL_TEXTURE_2D, 1u, GL_RGBA16F, 256, 64);
        setTextureFilter(transmittanceTexture, GL_LINEAR);
        glTextureParameteri(transmittanceTexture.GetId(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(transmittanceTexture.GetId(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        const auto ScatterRSize = 32u, ScatterMuSize = 128u, ScatterMuSSize = 32u, ScatterNuSize = 16u;
        scatterTexture.Create(GL_TEXTURE_3D, 1u, GL_RGBA16F, ScatterNuSize * ScatterMuSSize, ScatterMuSize, ScatterRSize);
        setTextureFilter(scatterTexture, GL_LINEAR);
        glTextureParameteri(scatterTexture.GetId(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(scatterTexture.GetId(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(scatterTexture.GetId(), GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        maxToUpload = numNodeGroups;// / 8; // - arbitrary (to do: take care to make it high enough for timely updates)
        gpuUploadNodes.Create(sizeof(UploadNodeGroup) * maxToUpload * NodeArity, GL_DYNAMIC_STORAGE_BIT);
        gpuUploadBricks.Create(sizeof(UploadBrick) * maxToUpload * NodeArity, GL_DYNAMIC_STORAGE_BIT);
        gpuGenData.Create(sizeof(uint32_t) * maxToUpload * NodeArity * 16u, GL_DYNAMIC_STORAGE_BIT); // - fairly arbitrary. Maybe look into trying to determine a good size?

        auto t = timer.Begin("Initial atmosphere splits");

        // For this particular atmosphere:
        octree.rootGroupIndex = octree.RequestRoot();
        updater.InitialSetup(*this);

        //std::cout << "glGetError:" << __LINE__ << ": " << glGetError() << "\n";
        return true;
    }

    void Atmosphere::SetUniforms(Util::Shader& shader)
    {
        shader.Uniform3u("uBricksRes", glm::uvec3{ texMap });
        shader.Uniform3f("bricksRes", glm::vec3{ texMap });
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
        auto loadGenerator = [&](Generator& gen)
        {
            return loadShader(gen.GetShader(), "generation/" + gen.GetShaderName(), true);
        };

        if (!loadShader(postShader, "../post", false)) return false;
        if (!loadShader(finalShader, "../final", false)) return false;
        if (!loadShader(backdropShader, "../misc/backdrop", false)) return false;
        if (!loadShader(transmittanceShader, "transmittance", true)) return false;
        if (!loadShader(inscatterFirstShader, "inscatter_first", true)) return false;
        if (!loadShader(updateShader, "update_nodes", true)) return false;

        if (!loadGenerator(updater.generator)) return false;
        if (!loadGenerator(updater.featureGenerator)) return false;

        if (!loadShader(initSplitsShader, "init_splits", true)) return false;
        if (!loadShader(updateFlagsShader, "update_flags", true)) return false;
        if (!loadShader(updateLightPerGroupShader, "update_light_group", true)) return false;
        if (!loadShader(updateLightShader, "update_lighting", true)) return false;
        if (!loadShader(lightFilterShader, "filter_lighting", true)) return false;
        if (!loadShader(updateOctreeMapShader, "update_octree_map", true)) return false;
        //if (!loadShader(renderShader, "render", true)) return false;
        if (!loadShader(renderInterpShader, "render_with_animation_interpolation", true)) return false;
        if (!loadShader(renderNoInterpShader, "render_without_animation_interpolation", true)) return false;
        if (!loadShader(resolveShader, "resolve", true)) return false;
        return true;
    }

    void Atmosphere::UpdateUniforms(const Camera& camera, const LightSource& light)
    {
        // - to do: light direction also from LightSource object
        lightDir = glm::normalize(glm::dvec3(1, 0.6, 0.4));
        // - light rotation test:
        auto lightSpeed = 1.0 / 1000.0;
        auto lightRot = glm::angleAxis(GetLightTime() * glm::pi<double>() * 2.0 * lightSpeed, glm::dvec3(0, -1, 0));
        lightDir = glm::rotate(lightRot, lightDir);

        const auto worldMat = glm::translate(Object::Mat4{ 1.0 }, position - camera.GetPosition());
        const auto viewMat = camera.GetOrientationMatrix();
        const auto projMat = camera.GetProjectionMatrix();
        const auto viewProjMat = projMat * viewMat;
        const auto invWorldMat = glm::inverse(worldMat);
        const auto invViewProjMat = glm::inverse(viewProjMat);
        const auto prevViewProjMat = this->prevViewProjMat;
        this->prevViewProjMat = viewProjMat;
        this->viewProjMat = projMat * viewMat;

        Uniforms uniforms = {};

        uniforms.viewProjMat = viewProjMat;
        uniforms.invViewMat = glm::inverse(viewMat);
        uniforms.invProjMat = glm::inverse(projMat);
        uniforms.invViewProjMat = invViewProjMat;
        uniforms.worldMat = worldMat;
        uniforms.prevViewProjMat = prevViewProjMat;
        uniforms.time = static_cast<float>(renderTime);
        uniforms.animationTime = static_cast<float>(GetAnimationTime());
        uniforms.animationAlpha = static_cast<float>(updater.GetUpdateFraction());
        uniforms.planetLocation = glm::vec4(GetPosition() - camera.GetPosition(), 0.0);

        uniforms.rootGroupIndex = octree.rootGroupIndex;
        uniforms.stepSize = 1;
        uniforms.planetRadius = (float)planetRadius;
        uniforms.atmosphereRadius = (float)(planetRadius * scale);
        uniforms.atmosphereScale = (float)scale;
        uniforms.atmosphereHeight = (float)height;
        uniforms.lightDir = glm::vec4(lightDir, 0.0);
        uniforms.sun = glm::vec4{ light.distance, light.radius, light.intensity, 0.0 };

        uniforms.HR = (float)HR;
        uniforms.betaR = glm::vec4(betaR, 0.0);
        uniforms.HM = (float)HM;
        uniforms.betaMEx = (float)betaMEx;
        uniforms.betaMSca = (float)betaMSca;
        uniforms.mieG = (float)mieG;
        uniforms.absorptionExtinction = glm::vec4(absorptionExtinction, 0.0);
        uniforms.absorptionMiddle = static_cast<float>(absorptionMiddle);
        uniforms.absorptionExtent = static_cast<float>(absorptionExtent);
        uniforms.Rg = (float)planetRadius;
        uniforms.Rt = (float)(planetRadius + height * 2.0);
        uniforms.cloudRadius = (float)(planetRadius + cloudMaxHeight);

        // - tuning these is important to avoid visual banding/clamping
        uniforms.offsetR = 2.0f;
        uniforms.scaleR = -20.0f;
        auto offsetM = 20.0f, scaleM = -80.0f;
        offsetM = 0.0; scaleM = 1.0; // - testing
        uniforms.offsetM = offsetM;
        uniforms.scaleM = scaleM;

        // https://outerra.blogspot.com/2013/07/logarithmic-depth-buffer-optimizations.html
        // - to do: use actual far plane (parameter from outside the Atmosphere class?)
        const double farplane = 1e8;
        const double Fcoef = 2.0 / log2(farplane + 1.0);
        //uniforms.Fcoef = float(Fcoef);
        uniforms.Fcoef_half = float(0.5 * Fcoef);


        if (uniformBuffer.GetSize() < sizeof(Uniforms))
        {
            uniformBuffer.Create(sizeof(Uniforms), GL_DYNAMIC_STORAGE_BIT);
        }
        uniformBuffer.Upload(0u, sizeof(Uniforms), &uniforms);
        uniformBuffer.BindBase(GL_UNIFORM_BUFFER, 0u);
    }

    void Atmosphere::Update(double dt, const UpdateParams& params, const Camera& camera, const LightSource& light)
    {
        auto t = timer.Begin("Atmosphere::Update");

        animate = params.animate;
        renderTime += dt;
        if (params.rotateLight) lightTime += dt;
        if (params.animate) time += dt;

        // - to do: make it clear that the UBO update is in here
        UpdateUniforms(camera, light);


        // - to do: divide this over multiple frames (while two past states are being interpolated)
        {
            // Update atmosphere:
            // - to do: split/merge based on camera and nodes being not fully opaque or transparent

            // Full node structure update:
            // (to replace partial updates, or just for animation? To decide upon)
            //gpuNodes.Upload(0, sizeof(NodeGroup) * octree.nodes.GetSize(), &octree.GetGroup(0u));

            // - to do: run animation update
        }

        auto& u = updater;
        auto& it = u.GetRenderIteration();
        gpuUploadNodes.BindBase(GL_SHADER_STORAGE_BUFFER, 1u);
        gpuUploadBricks.BindBase(GL_SHADER_STORAGE_BUFFER, 2u);
        // - to do: also bind the old texture for reading
        // (initially just testing writing directly to one)

        if (!hasTransmittance)
        {
            hasTransmittance = true;

            {
                auto t = timer.Begin("Transmittance");
                u.SetShader(*this, transmittanceShader);
                const glm::uvec3 workGroupSize{ 32u, 32u, 1u };
                auto& tex = transmittanceTexture;
                glBindImageTexture(0u, tex.GetId(), 0, GL_FALSE, 0, GL_WRITE_ONLY, tex.GetFormat());
                glDispatchCompute(tex.GetWidth() / workGroupSize.x, tex.GetHeight() / workGroupSize.y, tex.GetDepth() / workGroupSize.z);
                glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
            }
            {
                transmittanceTexture.Bind(5u);
                auto t = timer.Begin("Inscatter");
                u.SetShader(*this, inscatterFirstShader);
                const glm::uvec3 workGroupSize{ 8u, 8u, 8u };
                auto& tex = scatterTexture;
                glBindImageTexture(0u, tex.GetId(), 0, GL_FALSE, 0, GL_WRITE_ONLY, tex.GetFormat());
                glDispatchCompute(tex.GetWidth() / workGroupSize.x, tex.GetHeight() / workGroupSize.y, tex.GetDepth() / workGroupSize.z);
                glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
            }
        }

        auto& defaultGenerator = updater.generator;

        // - to do: probably remove this, eventually, in favour of always loading continuously
        // Update GPU data:
        if (initUpdate && u.GetUpdateIteration().nodesToUpload.size())
        {
            auto& it = u.GetUpdateIteration();
            initUpdate = false;
            std::cout << "Uploading " << it.nodesToUpload.size() << " node groups\n";
            std::cout << "Generating " << it.bricksToUpload.size() << " bricks\n";

            for (auto& uploadGroup : it.nodesToUpload)
            {
                const auto old = uploadGroup.nodeGroup;
                uploadGroup.nodeGroup = octree.GetGroup(uploadGroup.groupIndex);
            }
            gpuUploadNodes.Upload(0, sizeof(UploadNodeGroup) * it.nodesToUpload.size(), it.nodesToUpload.data());
            gpuUploadBricks.Upload(0, sizeof(UploadBrick) * it.bricksToUpload.size(), it.bricksToUpload.data());

            // - to do: smarter (distributed/copying) initial setup
            for (size_t i = 0u; i < std::extent<decltype(gpuStates)>::value; ++i)
            {
                auto& state = gpuStates[(1 + i) % std::extent<decltype(gpuStates)>::value];
                state.gpuNodes.BindBase(GL_SHADER_STORAGE_BUFFER, 0u);
                state.brickTexture.Bind(0u);
                state.octreeMap.Bind(2u);

                u.UpdateNodes(*this, it.nodesToUpload.size());
                u.UpdateMap(*this, state.octreeMap);
                u.GenerateBricks(*this, state, defaultGenerator, 0u, it.bricksToUpload.size());
                u.LightBricks(*this, state, 0u, it.bricksToUpload.size(), lightDir, Util::Timer::DurationMeta{1.0});
                u.FilterLighting(*this, state, 0u, it.bricksToUpload.size());
            }
        }

        if (params.update) // are we doing continuous updates?
        {
            const auto period = 1.0; // - to do: make this configurable
            auto cameraPos = camera.GetPosition() - GetPosition(); // - to do: support orientation (so transform this into atmosphere space)
            cameraPos /= planetRadius * scale;

            UpdateIteration::Parameters updaterParams;
            updaterParams.time = time;
            updaterParams.cameraPosition = cameraPos;
            updaterParams.lightDirection = lightDir;
            updaterParams.depthLimit = params.depthLimit;
            updaterParams.generator = params.useFeatureGenerator ? &updater.featureGenerator : &updater.generator;
            updaterParams.scale = scale;
            updaterParams.height = height;
            updaterParams.planetRadius = planetRadius;
            updaterParams.doFrustumCulling = params.frustumCull;
            updaterParams.viewFrustum.FromMatrix(viewProjMat);

            updater.OnFrame(*this, updaterParams, period);
        }
    }

    void Atmosphere::Render(const glm::ivec2& windowRes, const glm::ivec2& res, const Camera& camera, const LightSource& light)
    {
        auto& u = updater;
        auto t = timer.Begin("Atmosphere::Render");

        // Resize the render targets if resolution has changed.
        if (depthTexture.GetWidth() != res.x || depthTexture.GetHeight() != res.y)
        {
            depthTexture.Create(GL_TEXTURE_2D, 1u, GL_DEPTH24_STENCIL8, res.x, res.y);

            lightTexture.Create(GL_TEXTURE_2D, 1u, GL_RGBA16F, res.x, res.y);
            fbo.Create();
            fbo.SetDepthBuffer(depthTexture, 0u);
            fbo.SetColorBuffer(0u, lightTexture, 0u);

            for (auto i = 0u; i < std::extent<decltype(frameTextures)>::value; ++i)
            {
                auto& t = frameTextures[i];
                auto setTextureClamp = [](Util::Texture& tex, GLenum clamp)
                {
                    glTextureParameteri(tex.GetId(), GL_TEXTURE_WRAP_S, clamp);
                    glTextureParameteri(tex.GetId(), GL_TEXTURE_WRAP_T, clamp);
                };
                // - to do: depth too
                t.light.Create(GL_TEXTURE_2D, 1u, GL_RGBA16F, res.x, res.y);
                setTextureClamp(t.light, GL_CLAMP_TO_EDGE);
                t.transmittance.Create(GL_TEXTURE_2D, 1u, GL_RGBA16F, res.x, res.y);
                setTextureClamp(t.transmittance, GL_CLAMP_TO_EDGE);
            }

            postTexture.Create(GL_TEXTURE_2D, 1u, GL_RGB8, res.x, res.y);
            postFbo.Create();
            postFbo.SetColorBuffer(0u, postTexture, 0u);
        }


        auto computeFragOffset = [&](unsigned i)
        {
            // - to do: better pattern
            // (possibly Bayer matrix, though that might not be easily accomplished for 3) https://en.wikipedia.org/wiki/Ordered_dithering

            if (downscaleFactor == 2u) // - for testing
            {
                glm::uvec2 offsets[] = { {0u, 0u}, {1u, 1u}, {1u, 0u}, {0u, 1u} };
                return offsets[i];
            }

            return glm::uvec2(i % downscaleFactor, i / downscaleFactor);
        };
        const auto fragOffset = computeFragOffset(frame % (downscaleFactor * downscaleFactor));
        ++frame;

        auto setUpShader = [&](Util::Shader& shader) -> Util::Shader&
        {
            shader.Bind();
            shader.Uniform2u("fragOffset", fragOffset);
            shader.Uniform2u("fragFactor", glm::uvec2{ downscaleFactor });
            SetUniforms(shader);
            return shader;
        };

        fbo.Bind();
        const auto numStates = std::extent<decltype(gpuStates)>::value;
        auto& prevState = gpuStates[(u.updateStateIndex + 1u) % numStates];
        auto& state = gpuStates[(u.updateStateIndex + 2u) % numStates];
        state.gpuNodes.BindBase(GL_SHADER_STORAGE_BUFFER, 0u);
        prevState.brickTexture.Bind(0u);
        state.brickTexture.Bind(1u);
        state.octreeMap.Bind(2u);
        depthTexture.Bind(3u);
        transmittanceTexture.Bind(5u);
        scatterTexture.Bind(6u);
        octreeMap.Bind(7u);
        vao.Bind();

        glm::vec3 mapPosition, mapScale;
        { // per-frame octree acceleration map aligned to view frustum


            auto outvec3 = [&](glm::vec3 v)
            {
                std::cout << "(" << v.x << ", " << v.y << ", " << v.z;
                return ")";
            };

            const auto planetLocation = GetPosition() - camera.GetPosition(); // - or negated? Hmm...
            const auto R = GetPlanetRadius();
            const auto cloudHeight = GetCloudMaxHeight();
            const auto h = glm::max(0.0, glm::length(planetLocation) - R);
            const auto planetHorizon = std::sqrt(h * (h + 2 * R));
            const auto cloudHorizon = planetHorizon + std::sqrt(cloudHeight * (cloudHeight + 2 * R));

            const auto viewMatrix = camera.GetViewMatrix();
            // - to do: also inverse planet matrix, later on (to support orientation)
            const auto mat = viewMatrix;
            const auto invMat = glm::inverse(mat);
            const auto invProjMat = glm::inverse(camera.GetProjectionMatrix() * mat);


            // Compute atmosphere-space AABB of view frustum (extended to camera distance-to-cloud-layer-horizon):
            const auto origin = -glm::vec3(planetLocation);
            //std::cout << "origin: " << outvec3(origin) << " (cloudHorizon: " << cloudHorizon << ")\n";
            const auto forward = glm::normalize(glm::vec3(invProjMat * glm::vec4(0.0, 0.0, 1.0, 1.0)));
            auto min = origin, max = origin;
            for (float y = -1.0; y < 2.0; y += 2.0)
            {
                for (float x = -1.0; x < 2.0; x += 2.0)
                {
                    auto d = glm::normalize(glm::vec3(invProjMat * glm::vec4(x, y, 1.0, 1.0)));
                    //std::cout << outvec3(d) << "\n";
                    d /= glm::dot(d, forward); // compensate for directions to the side not covering the spherical cap
                    d *= cloudHorizon;
                    d += origin;
                    min = glm::min(min, d);
                    max = glm::max(max, d);
                }
            }
            // - to do: also consider the roughly hemispherical cap of the frustum
            // (not taking that into account is making the map coverage far too small)

            // Transform min/max to "octree space" (i.e. on [-1, 1]) and clamp:
            const auto octreeMin = glm::vec3(-1.0), octreeMax = glm::vec3(1.0);
            const auto octreeScale = static_cast<float>(planetRadius * scale);
            min = glm::clamp(min / octreeScale, octreeMin, octreeMax);
            max = glm::clamp(max / octreeScale, octreeMin, octreeMax);

            // - to do: find correct (negative) power of two in relation to full octree size (round up)
            auto extents = max - min;
            const auto maxExtent = glm::max(extents.x, glm::max(extents.y, extents.z));
            auto exponent = static_cast<int>(floor(log2(2.0 / maxExtent)));
            // - to do: see if the AABB can be fit at this level, with regards to the map resolution
            // (and if not, increment exponent once and fit at that level instead)

            glm::vec3 mapMin, mapMax;
            auto fitAabb = [&]()
            {
                const float res = static_cast<float>(state.octreeMap.GetWidth());
                const float mapSize = 2.0f / static_cast<float>(exp2(exponent));
                const float texelSize = mapSize / res;

                // Extend correctly with regards to map resolution, so map texel boundaries line up with node boundaries
                auto texel0 = floor((min + 1.0f) / texelSize),
                    texel1 = ceil((max + 1.0f) / texelSize);
                auto texelDiff = texel1 - texel0;
                if (texelDiff.x > res || texelDiff.y > res || texelDiff.z > res)
                {
                    return false; // the AABB doesn't fit in this level; we need to go bigger sizes
                }

                texel1 = texel0 + static_cast<decltype(texel0)>(res);
                mapMin = texel0 * texelSize - 1.0f;
                mapMax = texel1 * texelSize - 1.0f;
                return true; // the AABB can be mapped at this level
            };
            if (!fitAabb())
            {
                exponent -= 1;
                if (!fitAabb())
                {
                    std::cout << "Error: could not fit view frustum AABB\n"; // - to do: better message
                }
            }

            mapPosition = mapMin;
            mapScale = mapMax - mapMin;
            updater.UpdateMap(*this, octreeMap, mapPosition, mapScale, exponent);


            if (false)
            { // - debugging
                static auto count = 0u;
                if (count++ % 60u == 0u)
                {
                    std::cout << "AABB fit with exponent " << exponent;
                    std::cout << " (min: " << outvec3(min) << ", max: " << outvec3(max) << ")\n";
                    std::cout << " (position: " << outvec3(mapPosition) << ", scale: " << outvec3(mapScale) << ")";
                    std::cout << "\n";
                }
            }

            // - to do, in the future: experiment with a handful of maps for different distances/{sections of the frustum}
            // (and correspondingly splitting the rendering into a handful of passes)
        }

        { // "planet" background (to do: spruce this up, maybe move elsewhere)
            glViewport(0, 0, res.x, res.y);
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glClearDepth(1.0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            auto& shader = setUpShader(backdropShader);
            glDrawArrays(GL_TRIANGLES, 0, 2u * 3u);
        }

        // - to do: correct (and rotating indices)
        auto& previous = frameTextures[frame % 2u];
        auto& current = frameTextures[(frame + 1u) % 2u];

        auto bindImage = [](Util::Texture& tex, unsigned unit) 
        { 
            glBindImageTexture(unit, tex.GetId(), 0, GL_FALSE, 0, GL_READ_WRITE, tex.GetFormat()); 
        };
        bindImage(current.light, 0u);
        bindImage(current.transmittance, 1u);
        bindImage(lightTexture, 2u);

        { // atmosphere
            //lightTexture.Bind(4u);
            auto& shader = setUpShader(animate ? renderInterpShader : renderNoInterpShader);
            shader.Uniform3f("mapPosition", mapPosition);
            shader.Uniform3f("mapScale", mapScale);
            const glm::uvec3 workGroupSize{ 8u, 8u, 1u };
            // - to do: correctly downscale resolution
            auto downscaleRes = (res + glm::ivec2(downscaleFactor - 1)) / glm::ivec2(downscaleFactor);
            glDispatchCompute((downscaleRes.x + workGroupSize.x - 1u) / workGroupSize.x, (downscaleRes.y + workGroupSize.y - 1u) / workGroupSize.y, 1u);
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
        }

        { // combine old and current frame atmosphere renders, and apply to prior lighting
            auto& shader = setUpShader(resolveShader);
            previous.light.Bind(4);
            previous.transmittance.Bind(5);
            const glm::uvec3 workGroupSize{ 8u, 8u, 1u };
            glDispatchCompute((res.x + workGroupSize.x - 1u) / workGroupSize.x, (res.y + workGroupSize.y - 1u) / workGroupSize.y, 1u);
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
        }

        { // postprocessing
            glViewport(0, 0, res.x, res.y);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            postFbo.Bind();
            postShader.Bind();
            lightTexture.Bind(0u);
            glDrawArrays(GL_TRIANGLES, 0, 2u * 3u);
        }
    }

    void Atmosphere::Finalise(const glm::ivec2& windowRes, const glm::ivec2& res)
    {
        { // back-buffer blend
            glViewport(0, 0, windowRes.x, windowRes.y);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            Util::Framebuffer::BindBackbuffer();
            finalShader.Bind();
            postTexture.Bind(0u);
            glDrawArrays(GL_TRIANGLES, 0, 2u * 3u);
        }
    }
}
