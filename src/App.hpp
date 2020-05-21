#pragma once
#include "util/Window.hpp"
#include "util/Buffer.hpp"
#include "util/Framebuffer.hpp"
#include "util/Shader.hpp"
#include "util/Texture.hpp"
#include "util/VertexArray.hpp"
#include <vector>
#include "atmosphere/Atmosphere.hpp"
#include "Camera.hpp"
#include "util/Timer.hpp"
#include "Screenshotter.hpp"
#include "LightSource.hpp"

namespace Mulen {
    class App : public Window::App {
    public:
        App(Window&);

        const std::string shaderPath = "shaders/";

        bool showGui = true;
        float mouseSensitivity = 0.25f;
        ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

        glm::dvec2 lastCursorPos;
        Camera camera;
        LightSource light;
        Atmosphere::Atmosphere atmosphere;

        bool fpsMode = false;
        bool collision = true, keepLevel = false, inertial = false;
        Atmosphere::Atmosphere::UpdateParams atmUpdateParams;
        int downscaleFactor = 4u;
        const int maxDepthLimit = 16u;
        double lastTime;

        int gpuMemBudgetMiB = 2048; // - kind of arbitrary, but we've got to start with something
        Atmosphere::Atmosphere::Params atmInitParams;
        bool InitializeAtmosphere();
        bool Reload();
        void OnFrame() override;
        void OnKey(int key, int scancode, int action, int mods) override;
        void OnScroll(double xoffset, double yoffset) override;
        void OnDrop(int count, const char** paths) override;

        bool draggingView = false;

        Screenshotter screenshotter;
    };
}
