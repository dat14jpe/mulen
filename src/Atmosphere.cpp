#include "Atmosphere.hpp"
#include "Camera.hpp"
#include <math.h>
#include <functional>
#include "util/Timer.hpp"
#include "LightSource.hpp"


namespace Mulen {

    struct Uniforms
    {
        // Observe the padding required by OpenGL std140 layout:

        glm::mat4 viewProjMat, invViewMat, invProjMat, invViewProjMat, worldMat, prevViewProjMat;

        glm::vec4 planetLocation, lightDir, sun;
        glm::vec4 betaR;

        unsigned rootGroupIndex;
        float time, animationTime,
            Fcoef_half, stepSize, 
            atmosphereRadius, planetRadius, atmosphereScale, atmosphereHeight;
        float Rt, Rg;

        float HR, HM;
        float betaMEx, betaMSca, mieG;

        float offsetR, scaleR, offsetM, scaleM;
    };


    bool Atmosphere::Init(const Atmosphere::Params& p)
    {
        vao.Create();

        const bool moreMemory = true; // - for quick switching during development
        hasTransmittance = false;

        auto setTextureFilter = [](Util::Texture& tex, GLenum filter)
        {
            glTextureParameteri(tex.GetId(), GL_TEXTURE_MIN_FILTER, filter);
            glTextureParameteri(tex.GetId(), GL_TEXTURE_MAG_FILTER, filter);
        };

        // - to do: calculate actual number of nodes and bricks allowed/preferred from params
        const size_t numNodeGroups = 16384u * (moreMemory ? 3u : 1u); // - to do: just multiply by 3, or even 1 (though that last 1 is quite optimistic...)
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
        texMap.x = texMap.y = texMap.z = static_cast<unsigned>(std::ceil(std::pow(double(numBricks), 1.0 / 3.0)));
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
            setUpBrickLightTexture(state.brickLightTexture);
            setUpMapTexture(state.octreeMap);
        }
        setUpBrickLightTexture(brickLightTextureTemp);

        transmittanceTexture.Create(GL_TEXTURE_2D, 1u, GL_RGBA16F, 256, 64);
        setTextureFilter(transmittanceTexture, GL_LINEAR);
        glTextureParameteri(transmittanceTexture.GetId(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(transmittanceTexture.GetId(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        const auto ScatterRSize = 32u, ScatterMuSize = 128u, ScatterMuSSize = 32u, ScatterNuSize = 8u;
        scatterTexture.Create(GL_TEXTURE_3D, 1u, GL_RGBA16F, ScatterNuSize * ScatterMuSSize, ScatterMuSize, ScatterRSize);
        setTextureFilter(scatterTexture, GL_LINEAR);
        glTextureParameteri(scatterTexture.GetId(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(scatterTexture.GetId(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(scatterTexture.GetId(), GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        maxToUpload = numNodeGroups;// / 8; // - arbitrary (to do: take care to make it high enough for timely updates)
        gpuUploadNodes.Create(sizeof(UploadNodeGroup) * maxToUpload * NodeArity, GL_DYNAMIC_STORAGE_BIT);
        gpuUploadBricks.Create(sizeof(UploadBrick) * maxToUpload * NodeArity, GL_DYNAMIC_STORAGE_BIT);
        // - to do: also create brick upload buffer/texture

        auto t = timer.Begin("Initial atmosphere splits");

        // For this particular atmosphere:
        rootGroupIndex = octree.RequestRoot();
        updater.InitialSetup();

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
        if (!loadShader(postShader, "../post", false)) return false;
        if (!loadShader(backdropShader, "backdrop", false)) return false;
        if (!loadShader(transmittanceShader, "transmittance", true)) return false;
        if (!loadShader(inscatterFirstShader, "inscatter_first", true)) return false;
        if (!loadShader(updateShader, "update_nodes", true)) return false;
        if (!loadShader(updateBricksShader, "update_bricks", true)) return false;
        if (!loadShader(updateFlagsShader, "update_flags", true)) return false;
        if (!loadShader(updateLightShader, "update_lighting", true)) return false;
        if (!loadShader(lightFilterShader, "filter_lighting", true)) return false;
        if (!loadShader(updateOctreeMapShader, "update_octree_map", true)) return false;
        if (!loadShader(renderShader, "render", true)) return false;
        if (!loadShader(resolveShader, "resolve", true)) return false;
        return true;
    }

    void Atmosphere::UpdateUniforms(const Camera& camera, const LightSource& light)
    {
        // - to do: light direction also from LightSource object
        auto lightDir = glm::normalize(glm::dvec3(1, 0.6, 0.4));
        // - light rotation test:
        auto lightSpeed = 1.0 / 1000.0;
        auto lightRot = glm::angleAxis(lightTime * 3.141592653589793 * 2.0 * lightSpeed, glm::dvec3(0, -1, 0));
        lightDir = glm::rotate(lightRot, lightDir);

        const auto worldMat = glm::translate(Object::Mat4{ 1.0 }, position - camera.GetPosition());
        const auto viewMat = camera.GetOrientationMatrix();
        const auto projMat = camera.GetProjectionMatrix();
        const auto viewProjMat = projMat * viewMat;
        const auto invWorldMat = glm::inverse(worldMat);
        const auto invViewProjMat = glm::inverse(projMat * viewMat);
        const auto prevViewProjMat = this->prevViewProjMat;
        this->prevViewProjMat = viewProjMat;

        Uniforms uniforms = {};

        uniforms.viewProjMat = viewProjMat;
        uniforms.invViewMat = glm::inverse(viewMat);
        uniforms.invProjMat = glm::inverse(projMat);
        uniforms.invViewProjMat = invViewProjMat;
        uniforms.worldMat = worldMat;
        uniforms.prevViewProjMat = prevViewProjMat;
        uniforms.time = static_cast<float>(renderTime);
        uniforms.animationTime = static_cast<float>(time);
        uniforms.planetLocation = glm::vec4(GetPosition() - camera.GetPosition(), 0.0);

        uniforms.rootGroupIndex = rootGroupIndex;
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
        uniforms.Rg = (float)planetRadius;
        uniforms.Rt = (float)(planetRadius + height * 2.0);

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
                u.SetShader(transmittanceShader);
                const glm::uvec3 workGroupSize{ 32u, 32u, 1u };
                auto& tex = transmittanceTexture;
                glBindImageTexture(0u, tex.GetId(), 0, GL_FALSE, 0, GL_WRITE_ONLY, tex.GetFormat());
                glDispatchCompute(tex.GetWidth() / workGroupSize.x, tex.GetHeight() / workGroupSize.y, tex.GetDepth() / workGroupSize.z);
                glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
            }
            {
                transmittanceTexture.Bind(5u);
                auto t = timer.Begin("Inscatter");
                u.SetShader(inscatterFirstShader);
                const glm::uvec3 workGroupSize{ 8u, 8u, 8u };
                auto& tex = scatterTexture;
                glBindImageTexture(0u, tex.GetId(), 0, GL_FALSE, 0, GL_WRITE_ONLY, tex.GetFormat());
                glDispatchCompute(tex.GetWidth() / workGroupSize.x, tex.GetHeight() / workGroupSize.y, tex.GetDepth() / workGroupSize.z);
                glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
            }
        }


        // - to do: probably remove this, eventually, in favour of always loading continuously
        // Update GPU data:
        static bool firstUpdate = true; // - temporary ugliness, before this is replaced by continuous updates
        if (firstUpdate && u.GetUpdateIteration().nodesToUpload.size() && firstUpdate)
        {
            auto& it = u.GetUpdateIteration();
            firstUpdate = false;
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

                u.UpdateNodes(it.nodesToUpload.size());
                u.UpdateMap(state.octreeMap);
                u.GenerateBricks(state, 0u, it.bricksToUpload.size());
                u.LightBricks(state, 0u, it.bricksToUpload.size());
                u.FilterLighting(state, 0u, it.bricksToUpload.size());
            }
        }

        if (params.update) // are we doing continuous updates?
        {
            const auto period = 1.0; // - to do: make this configurable
            auto cameraPos = camera.GetPosition() - GetPosition();
            cameraPos /= planetRadius * scale;

            AtmosphereUpdater::IterationParameters updaterParams;
            updaterParams.time = time;
            updaterParams.cameraPosition = cameraPos;
            updaterParams.depthLimit = params.depthLimit;

            updater.OnFrame(updaterParams, period);
        }
    }

    void Atmosphere::Render(const glm::ivec2& res, const Camera& camera, const LightSource& light)
    {
        auto& u = updater;

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
        const auto prevStateIndex = (u.updateStateIndex + 1u) % numStates;
        const auto nextStateIndex = (u.updateStateIndex + 2u) % numStates;
        auto& state = gpuStates[prevStateIndex];
        auto& nextState = gpuStates[nextStateIndex];
        state.gpuNodes.BindBase(GL_SHADER_STORAGE_BUFFER, 0u);
        state.brickTexture.Bind(0u);
        state.brickLightTexture.Bind(1u);
        state.octreeMap.Bind(2u);
        depthTexture.Bind(3u);
        transmittanceTexture.Bind(5u);
        scatterTexture.Bind(6u);
        octreeMap.Bind(7u);
        nextState.brickTexture.Bind(8u);
        nextState.brickLightTexture.Bind(9u);
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
                const auto res = state.octreeMap.GetWidth();
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
            updater.UpdateMap(octreeMap, mapPosition, mapScale, exponent);


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
            auto& shader = setUpShader(renderShader);
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
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            Util::Framebuffer::BindBackbuffer();
            auto& shader = postShader;
            shader.Bind();
            lightTexture.Bind(0u);
            glDrawArrays(GL_TRIANGLES, 0, 2u * 3u);
        }
    }
}
