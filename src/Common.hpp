#pragma once
#include "Octree.hpp"
#include <glm/glm.hpp>
#include "util/Buffer.hpp"
#include "util/Texture.hpp"
#include "util/Shader.hpp"

namespace Mulen {
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
        uint32_t genData;
        uint32_t padding0;
        glm::vec4 nodeLocation;
    };

    struct GpuState
    {
        Util::Buffer gpuNodes;
        Util::Texture brickTexture, brickLightTexture;
        Util::Texture octreeMap;
    };

    // - to do: consider two channels so bricks store both last and next values, to let rendering interpolate between
    static const auto
        BrickFormat = GL_R16, // - visible banding if only 8 bits per channel. Maybe can be resolved with generation dithering?
        BrickLightFormat = GL_R16F;
}
