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

        {
            static float f = 0.0f;
            static int counter = 0;

            ImGui::Begin("Atmosphere");

            ImGui::Text("This is some useful text.");

            ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
            ImGui::ColorEdit3("clear color", (float*)&clear_color);

            if (ImGui::Button("Button")) counter++;
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();
        }

        {
            glm::vec3 accel{ 0.0f };
            if (window.IsKeyPressed(GLFW_KEY_A)) accel.x -= 1;
            if (window.IsKeyPressed(GLFW_KEY_D)) accel.x += 1;
            if (window.IsKeyPressed(GLFW_KEY_W)) accel.z -= 1;
            if (window.IsKeyPressed(GLFW_KEY_S)) accel.z += 1;
            if (window.IsKeyPressed(GLFW_KEY_F)) accel.y -= 1;
            if (window.IsKeyPressed(GLFW_KEY_R)) accel.y += 1;
            if (glm::length(accel))
            {
                const float force = 10.0f;
                accel = glm::vec3(camera.GetViewMatrix() * glm::vec4(accel, 0.0f));
                camera.Accelerate(glm::normalize(accel) * force);
            }

            if (window.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT))
            {
                // - to do: camera rotation
            }
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
        }
    }
}
