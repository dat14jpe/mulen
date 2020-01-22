#pragma once
#include "util/Window.hpp"
#include "util/Buffer.hpp"
#include "util/Framebuffer.hpp"
#include "util/Shader.hpp"
#include "util/Texture.hpp"
#include "util/VertexArray.hpp"
#include <vector>
#include "Atmosphere.hpp"
#include "Camera.hpp"

namespace Mulen {
    class App : public Window::App {
    public:
        App(Window&);

        const std::string shaderPath = "shaders/";

        bool showGui = true, vsync = true;
        float mouseSensitivity = 0.25f;
        ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

        glm::dvec2 lastCursorPos;
        Camera camera;
        Atmosphere atmosphere;

        void SetVSync(bool);
        bool Reload();
        void OnFrame() override;
        void OnKey(int key, int scancode, int action, int mods) override;

        bool draggingView = false;
    };
}
