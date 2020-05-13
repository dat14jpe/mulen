#pragma once
#include "Generator.hpp"

namespace Mulen {
    class FeatureGenerator : public Generator
    {
        // - to do: feature list, possibly some auxiliary data structures
    public:
        FeatureGenerator(const std::string& shaderName) :
            Generator { shaderName }
        {}
    };
}
