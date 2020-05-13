#pragma once
#include "Common.hpp"
#include "util/Shader.hpp"

namespace Mulen {
    // Atmosphere generator.
    class Generator
    {
        const std::string shaderName;
        Util::Shader shader; // brick generation shader

    public:
        const std::string& GetShaderName() { return shaderName; }
        Util::Shader& GetShader() { return shader; }
        Generator(const std::string& shaderName)
            : shaderName{ shaderName } 
        {}

        // Generate data for a new generation pass (likely in a worker thread).
        virtual void Generate(UpdateIteration&);
    };
}
