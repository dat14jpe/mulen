#pragma once
#include "util/Screenshotter.hpp"

class Window;

namespace Mulen {
    class Camera;
    class Atmosphere;

    class Screenshotter
    {
        Util::Screenshotter screenshotter;

    public:
        void TakeScreenshot(Window&, const Camera&, const Atmosphere&);
        void ReceiveScreenshot(std::string filename, Camera&, Atmosphere&);
    };
}
