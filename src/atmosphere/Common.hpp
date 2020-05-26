#pragma once
#include "Octree.hpp"
#include <glm/glm.hpp>
#include "util/Buffer.hpp"
#include "util/Texture.hpp"
#include "util/Shader.hpp"
#include "Object.hpp"

namespace Mulen {

    struct Frustum
    {
        // Planes will be in the space before the space of the input matrix.
        // (so pass the viewProjection matrix to get the frustum in world space, for example)
        Frustum& FromMatrix(const glm::dmat4& inMat, bool normalize = true)
        {
            auto mat = glm::transpose(inMat);
            planes[0] = mat[3] + mat[0]; // left
            planes[1] = mat[3] - mat[0]; // right
            planes[2] = mat[3] - mat[1]; // top
            planes[3] = mat[3] + mat[1]; // bottom
            planes[4] = mat[3] + mat[2]; // near
            planes[5] = mat[3] - mat[2]; // far

            if (normalize)
            {
                for (auto i = 0u; i < 6u; ++i)
                {
                    planes[i] = -glm::normalize(planes[i]);
                }
            }

            return *this;
        }
        glm::dvec4 planes[6]; // normals pointing into frustum
    };


    namespace Atmosphere {
        class Generator;
    }

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
    struct UploadBrick
    {
        NodeIndex nodeIndex, brickIndex;
        uint32_t genDataOffset, genDataSize;
        glm::vec4 nodeLocation;
    };

    struct GpuState
    {
        Util::Buffer gpuNodes;
        Util::Texture brickTexture;
        Util::Texture octreeMap;
    };

    // - to do: consider two channels so bricks store both last and next values, to let rendering interpolate between
    static const auto
        BrickFormat = GL_RG8, // - visible banding if only 8 bits per channel. Maybe can be resolved with generation dithering?
        BrickLightFormat = GL_R8;

    static const auto LightPerGroupRes = BrickRes * 2u;

    static const std::string
        Profiler_UpdateInit = "Update::Init",
        Profiler_UpdateInitSplits = "Update::InitSplits",
        Profiler_UpdateGenerate = "Update::Generate",
        Profiler_UpdateMap = "Update::Map",
        Profiler_UpdateLight = "Update::Light",
        Profiler_UpdateLightPerGroup = "Update::LightPerGroup",
        Profiler_UpdateLightPerVoxel = "Update::LightPerVoxel",
        Profiler_UpdateFilter = "Update::Filter"
        ;

    struct Structure
    {
        Octree octree;
        NodeIndex rootGroupIndex;
    };

    struct UpdateIteration
    {
        struct Parameters
        {
            double time;
            Object::Position cameraPosition;
            bool doFrustumCulling;
            Frustum viewFrustum;
            Object::Position lightDirection;
            unsigned depthLimit;

            double scale, height, planetRadius; // atmosphere scale, height, and planet radius

            Atmosphere::Generator* generator; // - to do: don't use a raw pointer
        } params;

        std::vector<UploadNodeGroup> nodesToUpload;
        std::vector<UploadBrick> bricksToUpload;
        std::vector<NodeIndex> splitGroups; // indices of groups resulting from splits in this update
        std::vector<uint32_t> genData;

        unsigned maxDepth;
        // - to do: full depth distribution? Assuming 32 as max depth should be plenty
        // - to do: more stats


        void Reset()
        {
            nodesToUpload.resize(0u);
            bricksToUpload.resize(0u);
            splitGroups.resize(0u);
            genData.resize(0u);
            maxDepth = 0u;
        }
    };
}
