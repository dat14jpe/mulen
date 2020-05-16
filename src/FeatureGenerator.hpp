#pragma once
#include "Generator.hpp"

namespace Mulen {
    class FeatureGenerator : public Generator
    {
        struct Feature
        {
            typedef uint32_t Index;
            Index parent;

            enum class Type : uint8_t
            {
                Cumulus,
                Stratus,
                Cirrus,
                // - maybe split these complex ones into smaller constituents?
                Cumulonimbus,
                Cyclone
            } type;

            glm::vec4 location; // size in w
            // - maybe add "noise location"? Maybe, yes
            // - to do: add velocity
        };
        // - to do: more advanced structure? Tree, yes
        std::vector<Feature> features;

    public:
        FeatureGenerator(const std::string& shaderName) :
            Generator { shaderName }
        {}

        void Generate(UpdateIteration&) override;
    };
}
