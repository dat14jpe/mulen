#include "App.hpp"

// - for testing:
std::ostream& operator<<(std::ostream& os, const glm::vec4& m) {
    return os << "(" << m.x << ", " << m.y << ", " << m.z << ", " << m.w << ")";
}

namespace Mulen {

    App::App(Window& window)
        : Window::App{ window }
    {
        Reload();
        SetVSync(vsync);

        // - to do: configurable values, not hardcoded
        const size_t budget = 256u * (1u << 20u);
        atmosphere.Init({ budget, budget });
    }

    void App::SetVSync(bool vsync)
    {
        this->vsync = vsync;
        glfwSwapInterval(vsync ? 1 : 0);
    }

    bool App::Reload()
    {
        return atmosphere.ReloadShaders(shaderPath);
    }

    void App::OnFrame()
    {
        const auto dt = 1.0f / ImGui::GetIO().Framerate;
        const auto size = window.GetSize();
        const float aspect = float(size.x) / float(size.y);
        const float fovy = 45.0f;
        const float near = 0.01f, far = 1e3f;
        camera.SetPerspectiveProjection(fovy, aspect, 0.01f, 1e3f);
        glViewport(0, 0, size.x, size.y);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);

        if (showGui)
        {
            ImGui::Begin("Atmosphere");

            /*ImGui::Text("This is some useful text.");
            ImGui::SliderFloat("float", &f, 0.0f, 1.0f);*/
            ImGui::ColorEdit3("clear color", (float*)&clear_color);

            /*if (ImGui::Button("Button")) counter++;
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);*/

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();
        }

        // Camera controls
        {
            glm::vec3 accel{ 0.0f };
            if (window.IsKeyPressed(GLFW_KEY_A)) accel.x -= 1;
            if (window.IsKeyPressed(GLFW_KEY_D)) accel.x += 1;
            if (window.IsKeyPressed(GLFW_KEY_W)) accel.z -= 1;
            if (window.IsKeyPressed(GLFW_KEY_S)) accel.z += 1;
            if (window.IsKeyPressed(GLFW_KEY_F)) accel.y -= 1;
            if (window.IsKeyPressed(GLFW_KEY_R)) accel.y += 1;
            if (accel != glm::vec3(0.0f))
            {
                const float force = 10.0f; // - to do: make configurable
                accel = glm::vec3(glm::inverse(camera.GetViewMatrix()) * glm::vec4(accel, 0.0f));
                camera.Accelerate(glm::normalize(accel) * force);
            }

            auto cursorPos = glm::vec2(window.GetCursorPosition());
            cursorPos = cursorPos / glm::vec2(size) * 2.0f - 1.0f;
            cursorPos.x *= aspect;
            const bool
                rotateView = window.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT),
                rotateOrbit = window.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT);
            if (rotateView || rotateOrbit)
            {
                // - to do: enable smooth rotation

                auto arcballPosition = [&](glm::vec2 p)
                {
                    p *= mouseSensitivity;
                    auto length = glm::length(p);
                    p.y = -p.y;
                    if (length >= 1.0f) return glm::vec3{1.0f * p / sqrt(length), 0.0f};
                    else                return glm::vec3{p, sqrt(1.0f - length)};
                };
                auto c0 = glm::vec2(0.0f), c1 = cursorPos - lastCursorPos;
                auto a0 = arcballPosition(c0), a1 = arcballPosition(c1);
                auto cross = glm::cross(a0, a1);
                if (glm::length(cross) > 1e-5) // needs to be non-zero
                {
                    if (rotateView)
                    {
                        camera.ApplyRotation(glm::quat{ dot(a0, a1), cross });
                    }
                    if (rotateOrbit)
                    {
                        // - to do
                    }
                }
            }
            lastCursorPos = cursorPos;
        }
        camera.Update(dt);

        atmosphere.Update(camera);
        atmosphere.Render(size, glfwGetTime(), camera);
    }

    void App::OnKey(int key, int scancode, int action, int mods)
    {
        if (action != GLFW_PRESS) return;
        switch (key)
        {
        case GLFW_KEY_ESCAPE:
            window.Close();
            break;
        case GLFW_KEY_F5:
            if (Reload()) std::cout << "Reloaded\n";
            break;
        case GLFW_KEY_F4:
            SetVSync(!vsync);
            break;
        case GLFW_KEY_F11:
            window.IsMaximized() ? window.Maximize() : window.Restore();
            break;
        case GLFW_KEY_F6:
            showGui = !showGui;
            break;
        }
    }
}
