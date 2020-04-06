#include "Atmosphere.hpp"
#include "Camera.hpp"
#include <math.h>
#include <functional>
#include "util/Timer.hpp"


namespace Mulen {
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
        auto lightDir = glm::normalize(glm::dvec3(1, 0.6, 0.4));
        // - light rotation test:
        auto lightSpeed = 1e-3;
        auto lightRot = glm::angleAxis(lightTime * 3.141592653589793 * 2.0 * lightSpeed, glm::dvec3(0, -1, 0));
        lightDir = glm::rotate(lightRot, lightDir);

        // - to do: use a UBO instead
        shader.Uniform1u("rootGroupIndex", glm::uvec1{ rootGroupIndex });
        shader.Uniform3u("uBricksRes", glm::uvec3{ texMap });
        shader.Uniform3f("bricksRes", glm::vec3{ texMap });
        shader.Uniform1f("stepSize", glm::vec1{ 1 });
        shader.Uniform1f("planetRadius", glm::vec1{ (float)planetRadius });
        shader.Uniform1f("atmosphereRadius", glm::vec1{ (float)(planetRadius * scale) });
        shader.Uniform1f("atmosphereScale", glm::vec1{ (float)scale });
        shader.Uniform1f("atmosphereHeight", glm::vec1{ (float)height });
        shader.Uniform3f("lightDir", lightDir);
        shader.Uniform3f("sun", glm::vec3{ sunDistance, sunRadius, sunIntensity });

        shader.Uniform1f("HR", glm::vec1((float)HR));
        shader.Uniform3f("betaR", betaR);
        shader.Uniform1f("HM", glm::vec1((float)HM));
        shader.Uniform1f("betaMEx", glm::vec1((float)betaMEx));
        shader.Uniform1f("betaMSca", glm::vec1((float)betaMSca));
        shader.Uniform1f("mieG", glm::vec1((float)mieG));
        shader.Uniform1f("Rg", glm::vec1{ (float)planetRadius });
        shader.Uniform1f("Rt", glm::vec1{ (float)(planetRadius + height * 2.0) });

        // - tuning these is important to avoid visual banding/clamping
        shader.Uniform1f("offsetR", glm::vec1{ 2.0f });
        shader.Uniform1f("scaleR", glm::vec1{ -20.0f });
        shader.Uniform1f("offsetM", glm::vec1{ 20.0f });
        shader.Uniform1f("scaleM", glm::vec1{ -80.0f });

        // https://outerra.blogspot.com/2013/07/logarithmic-depth-buffer-optimizations.html
        // - to do: use actual far plane (parameter from outside the Atmosphere class?)
        const double farplane = 1e8;
        const double Fcoef = 2.0 / log2(farplane + 1.0);
        shader.Uniform1f("Fcoef", glm::vec1{ float(Fcoef) });
        shader.Uniform1f("Fcoef_half", glm::vec1{ float(0.5 * Fcoef) });
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
        if (!loadShader(updateLightShader, "update_lighting", true)) return false;
        if (!loadShader(lightFilterShader, "filter_lighting", true)) return false;
        if (!loadShader(updateOctreeMapShader, "update_octree_map", true)) return false;
        //if (!loadShader(renderShader, "render", false)) return false;
        if (!loadShader(renderShader, "render", true)) return false;
        return true;
    }

    void Atmosphere::Update(bool update, const Camera& camera, unsigned depthLimit)
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

            auto& state = gpuStates[0];
            state.gpuNodes.BindBase(GL_SHADER_STORAGE_BUFFER, 0u);
            state.brickTexture.Bind(0u);
            state.octreeMap.Bind(2u);

            u.UpdateNodes(it.nodesToUpload.size());
            u.UpdateMap(state.octreeMap);
            u.GenerateBricks(state, 0u, it.bricksToUpload.size());
            u.LightBricks(state, 0u, it.bricksToUpload.size());
            u.FilterLighting(state, 0u, it.bricksToUpload.size());
        }

        if (update) // are we doing continuous updates?
        {
            const auto period = 1.0; // - to do: make this configurable
            auto cameraPos = camera.GetPosition() - GetPosition();
            cameraPos /= planetRadius * scale;

            AtmosphereUpdater::IterationParameters params;
            params.time = time;
            params.cameraPosition = cameraPos;

            // - depth 10 gives voxel resolution circa 1 km
            // (to do: try to *selectively* reach at least depth 13, to enable smaller clouds. Around 16 would be perfect, but likely much too expensive)
            params.depthLimit = depthLimit;

            updater.OnFrame(params, period);
        }
    }

    void Atmosphere::Render(const glm::ivec2& res, double time, const Camera& camera)
    {
        auto& u = updater;

        if (rotateLight) lightTime += time - this->time;
        this->time = time;

        // Resize the render targets if resolution has changed.
        if (depthTexture.GetWidth() != res.x || depthTexture.GetHeight() != res.y)
        {
            depthTexture.Create(GL_TEXTURE_2D, 1u, GL_DEPTH24_STENCIL8, res.x, res.y);
            for (auto i = 0u; i < 2u; ++i)
            {
                lightTextures[i].Create(GL_TEXTURE_2D, 1u, GL_RGBA16F, res.x, res.y);
                fbos[i].Create();
                fbos[i].SetDepthBuffer(depthTexture, 0u);
                fbos[i].SetColorBuffer(0u, lightTextures[i], 0u);
            }
        }

        const auto worldMat = glm::translate(Object::Mat4{ 1.0 }, position - camera.GetPosition());
        const auto viewMat = camera.GetOrientationMatrix();
        const auto projMat = camera.GetProjectionMatrix();
        const auto viewProjMat = projMat * viewMat;
        const auto invWorldMat = glm::inverse(worldMat);
        const auto invViewProjMat = glm::inverse(projMat * viewMat);

        auto setUpShader = [&](Util::Shader& shader) -> Util::Shader&
        {
            shader.Bind();
            shader.Uniform1f("time", glm::vec1(static_cast<float>(time)));
            shader.UniformMat4("invViewProjMat", invViewProjMat);
            shader.UniformMat4("invViewMat", glm::inverse(viewMat));
            shader.UniformMat4("invProjMat", glm::inverse(projMat));
            shader.UniformMat4("invWorldMat", invWorldMat);
            shader.UniformMat4("viewProjMat", viewProjMat);
            shader.Uniform3f("planetLocation", GetPosition() - camera.GetPosition());
            SetUniforms(shader);
            return shader;
        };

        fbos[0].Bind();
        auto& state = gpuStates[(u.updateStateIndex + 1u) % std::extent<decltype(gpuStates)>::value];
        state.gpuNodes.BindBase(GL_SHADER_STORAGE_BUFFER, 0u);
        state.brickTexture.Bind(0u);
        state.brickLightTexture.Bind(1u);
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
            const float octreeScale = static_cast<double>(planetRadius * scale);
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

                texel1 = texel0 + decltype(texel0)(res);
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
        
        { // atmosphere
            lightTextures[0].Bind(4u);
            auto& shader = setUpShader(renderShader);
            shader.Uniform3f("mapPosition", mapPosition);
            shader.Uniform3f("mapScale", mapScale);
            auto& tex = lightTextures[1];
            glBindImageTexture(0u, tex.GetId(), 0, GL_FALSE, 0, GL_READ_WRITE, tex.GetFormat());
            const glm::uvec3 workGroupSize{ 8u, 8u, 1u };
            glDispatchCompute((tex.GetWidth() + workGroupSize.x - 1u) / workGroupSize.x, (tex.GetHeight() + workGroupSize.y - 1u) / workGroupSize.y, 1u);
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
        }

        { // postprocessing
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            Util::Framebuffer::BindBackbuffer();
            auto& shader = postShader;
            shader.Bind();
            lightTextures[1].Bind(0u);
            glDrawArrays(GL_TRIANGLES, 0, 2u * 3u);
        }
    }
}
