#pragma once
#include "util/Screenshotter.hpp"

class Window;

namespace Mulen {
    class Camera;

    class Screenshotter
    {
        Util::Screenshotter screenshotter;

    public:
        void TakeScreenshot(Window&, Camera&);
        void ReceiveScreenshot(std::string filename, Camera&);
    };
}
