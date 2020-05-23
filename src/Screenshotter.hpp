#pragma once
#include "util/Screenshotter.hpp"

class Window;

namespace Mulen {
    namespace Atmosphere {
        class Atmosphere;
    }
    class Camera;

    class Screenshotter
    {
        Util::Screenshotter screenshotter;

    public:
        void TakeScreenshot(Window&, const glm::ivec2& resolution, const Camera&, const Atmosphere::Atmosphere&);
        void ReceiveScreenshot(std::string filename, Camera&, Atmosphere::Atmosphere&);
    };
}
