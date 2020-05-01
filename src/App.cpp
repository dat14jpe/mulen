#include "App.hpp"
#include <glm/gtx/quaternion.hpp>
#include "util/lodepng.h"

// - for testing:
std::ostream& operator<<(std::ostream& os, const glm::vec4& m) {
    return os << "(" << m.x << ", " << m.y << ", " << m.z << ", " << m.w << ")";
}

namespace Mulen {

    App::App(Window& window)
        : Window::App{ window }
        , atmosphere{ timer }
    {
        Reload();
        window.SetVSync(true);

        InitializeAtmosphere();
        camera.SetPosition(Object::Position(0, 0, atmosphere.GetPlanetRadius() * 2.25));
        camera.radius = glm::distance(camera.GetPosition(), atmosphere.GetPosition());
    }

    bool App::InitializeAtmosphere()
    {
        // - to do: configurable values, not hardcoded
        const size_t budget = 256u * (1u << 20u);
        atmosphere.Init({ budget, budget });
        return true;
    }

    bool App::Reload()
    {
        return atmosphere.ReloadShaders(shaderPath);
    }

    void App::OnFrame()
    {
        const auto dt = 1.0f / ImGui::GetIO().Framerate;
        const auto size = glm::max(glm::ivec2(1), window.GetSize());
        const float aspect = float(size.x) / float(size.y);
        const float fovy = 45.0f;
        const float near = 1.0f, far = 1e8f;
        camera.SetPerspectiveProjection(fovy, aspect, near, far);
        glViewport(0, 0, size.x, size.y);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);

        if (showGui)
        {
            ImGui::Begin("Atmosphere");

            ImGui::Text("Altitude: %.3f km", 1e-3 * (glm::distance(atmosphere.GetPosition(), camera.GetPosition()) - atmosphere.GetPlanetRadius()));
            ImGui::Checkbox("Update", &update);
            ImGui::Checkbox("Rotate light", &rotateLight);
            ImGui::Checkbox("Upright", &upright);
            ImGui::Checkbox("Collision", &collision);
            ImGui::Checkbox("Fly", &fly);
            ImGui::SliderInt("Depth", &depthLimit, 1u, maxDepthLimit);
            ImGui::SliderInt("Downscale", &downscaleFactor, 1u, 4u);
            if (ImGui::Button("Re-init"))
            {
                atmosphere.ReloadShaders(shaderPath);
                InitializeAtmosphere();
            }

            { // distance to planet or atmosphere cloud shell
                auto cursorPos = glm::dvec2(window.GetCursorPosition());
                cursorPos = cursorPos / glm::dvec2(size) * 2.0 - 1.0;
                cursorPos.y = -cursorPos.y;
                auto viewMat = camera.GetOrientationMatrix();
                auto projMat = camera.GetProjectionMatrix();
                auto ori = glm::dvec3(glm::inverse(viewMat) * glm::dvec4(0, 0, 0, 1));
                auto dir = glm::normalize(glm::dvec3(glm::inverse(projMat * viewMat) * glm::dvec4(cursorPos, 1, 1)));
                auto planetLocation = atmosphere.GetPosition() - camera.GetPosition();
                auto R = atmosphere.GetPlanetRadius();

                auto IntersectSphere = [](glm::dvec3 ori, glm::dvec3 dir, glm::dvec3 center, double radius, double& t0, double& t1)
                {
                    const auto radius2 = radius * radius;

                    auto L = center - ori;
                    auto tca = glm::dot(L, dir);
                    auto d2 = glm::dot(L, L) - tca * tca;
                    if (d2 > radius2) return false;
                    auto thc = sqrt(radius2 - d2);
                    t0 = tca - thc;
                    t1 = tca + thc;
                    t0 = glm::max(0.0, t0);
                    if (t1 < t0) return false;

                    return true;
                };
                double t0, t1;
                if (!IntersectSphere(ori, dir, planetLocation, R, t0, t1)) t0 = -1e3;
                auto planetT = t0;
                //ImGui::Text("Planet distance: %.3f km", 1e-3 * t0);
                const auto cloudHeight = atmosphere.GetCloudMaxHeight();
                if (!IntersectSphere(ori, dir, planetLocation, R + cloudHeight, t0, t1)) t1 = -1e3;
                else if (planetT > 0.0 && t1 > planetT) t1 = planetT;
                ImGui::Text("Farthest cloud layer distance: %.3f km", 1e-3 * t1);
                const auto h = glm::max(0.0, glm::length(planetLocation) - R);
                const auto planetHorizon = std::sqrt(h * (h + 2 * R));
                ImGui::Text("Horizon distance: %.3f km", 1e-3 * planetHorizon);
                ImGui::Text("Cloud layer horizon distance: %.3f km", 1e-3 * (planetHorizon + std::sqrt(cloudHeight * (cloudHeight + 2 * R))));
            }

            ImGui::Text("Max depth: %d", atmosphere.GetMaxDepth());

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();
        }

        // Camera controls
        if (!ImGui::IsAnyItemActive())
        {
            Object::Position accel{ 0.0 };
            if (window.IsKeyPressed(GLFW_KEY_A)) accel.x -= 1;
            if (window.IsKeyPressed(GLFW_KEY_D)) accel.x += 1;
            if (window.IsKeyPressed(GLFW_KEY_W)) accel.z -= 1;
            if (window.IsKeyPressed(GLFW_KEY_S)) accel.z += 1;
            if (window.IsKeyPressed(GLFW_KEY_F)) accel.y -= 1;
            if (window.IsKeyPressed(GLFW_KEY_R)) accel.y += 1;
            if (accel != Object::Position(0.0))
            {
                const auto r = atmosphere.GetPlanetRadius();
                auto force = 10.0; // - to do: make configurable?
                force *= r;
                const auto dist = glm::distance(atmosphere.GetPosition(), camera.GetPosition());
                //force *= glm::min(1.0, glm::pow(dist / (r * 1.3), 16.0)); // - to do: find nicer speed profile
                force *= log(dist / r);
                //force *= glm::clamp(pow((dist - r) / r, 1.5), 1e-2, 1.0);
                accel = Object::Position(glm::inverse(camera.GetViewMatrix()) * glm::dvec4(accel, 0.0f));
                // - to do: only perpendicular to planet normal, if not flying
                camera.Accelerate(glm::normalize(accel) * force);
            }

            auto cursorPos = glm::dvec2(window.GetCursorPosition());
            cursorPos = cursorPos / glm::dvec2(size) * 2.0 - 1.0;
            cursorPos.x *= aspect;
            const bool
                rotateView = window.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT) || fpsMode,
                rotateOrbit = window.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT);
            if (rotateView || rotateOrbit)
            {
                // - to do: enable smooth rotation

                auto arcballPosition = [&](glm::dvec2 p)
                {
                    p *= mouseSensitivity;
                    auto length = glm::length(p);
                    p.y = -p.y;
                    if (length >= 1.0f) return glm::dvec3{1.0 * p / sqrt(length), 0.0};
                    else                return glm::dvec3{p, -sqrt(1.0 - length)};
                };
                auto c0 = glm::dvec2(0.0), c1 = cursorPos - lastCursorPos;
                auto a0 = arcballPosition(c0), a1 = arcballPosition(c1);
                auto cross = glm::cross(a0, a1);
                const auto epsilon = 1e-5;
                if (glm::length(cross) > epsilon)
                {
                    auto q = Object::Orientation{ dot(a0, a1), cross };
                    if (rotateView)
                    {
                        camera.ApplyRotation(glm::conjugate(q));
                    }
                    if (rotateOrbit)
                    {
                        auto viewMatInv = glm::inverse(camera.GetViewMatrix());
                        a0 = (viewMatInv * glm::dvec4(a0, 0.0));
                        a1 = (viewMatInv * glm::dvec4(a1, 0.0));
                        const auto planetR = atmosphere.GetPlanetRadius();
                        const auto mul = glm::min(1.0, camera.radius / (planetR * 2.25));
                        a0 *= mul;
                        cross = glm::cross(a0, a1);
                        auto q = Object::Orientation{ dot(a0, a1), cross };

                        auto ap = atmosphere.GetPosition();
                        auto cp = camera.GetPosition();

                        auto planetVS0 = glm::normalize(Object::Position(camera.GetViewMatrix() * glm::dvec4(ap, 1.0f)));
                        auto camPos = ap + glm::normalize(Object::Position{ glm::rotate(q, glm::dvec4(cp - ap, 1.0)) }) * camera.radius;

                        camera.SetPosition(camPos); // rotate camera position around planet
                        
                        auto planetVS1 = glm::normalize(Object::Position(camera.GetViewMatrix() * glm::dvec4(ap, 1.0f)));

                        auto p0 = planetVS1, p1 = planetVS0;
                        auto cross = glm::cross(p0, p1);
                        if (glm::length(cross) > epsilon)
                        {
                            auto q = Object::Orientation{ dot(p0, p1), cross };
                            camera.ApplyRotation(glm::pow(q, 0.5));
                        }
                    }
                }
            }
            lastCursorPos = cursorPos;
            camera.Update(dt);
            if (camera.needsUpdate || camera.GetVelocity() != Object::Position{ 0.0 })
            {
                auto cp = camera.GetPosition(), ap = atmosphere.GetPosition();
                auto R = atmosphere.GetPlanetRadius();
                camera.radius = glm::distance(cp, ap);
                
                const auto minR = 1.79; // seems like a decent test height
                if (collision && camera.radius < minR + R)
                {
                    camera.radius = minR + R;
                    camera.SetPosition(ap + glm::normalize(cp - ap) * camera.radius);
                }

                camera.needsUpdate = false;
            }

            if (window.IsKeyPressed(GLFW_KEY_U) || upright)
            {
                // Idea: rotate so that view right vector is perpendicular to planet normal at current location.
                auto viewMat = camera.GetViewMatrix();
                auto right = Object::Position{ glm::inverse(viewMat) * glm::dvec4{ 1, 0, 0, 0 } };
                auto up = glm::normalize(atmosphere.GetPosition() - camera.GetPosition());
                auto angle = 3.141592653589793 * 0.5 - acos(glm::dot(up, right));
                Object::Orientation q = glm::angleAxis(angle, Object::Position{ 0, 0, -1 });
                if (abs(angle) > 1e-3) camera.ApplyRotation(q);
            }
        }

        atmosphere.SetLightRotates(rotateLight);
        atmosphere.SetDownscaleFactor(downscaleFactor);
        atmosphere.Update(update, camera, depthLimit);
        atmosphere.Render(size, glfwGetTime(), camera);

        timer.EndFrame();
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
            window.SetVSync(!window.GetVSync());
            break;
        case GLFW_KEY_F11:
            //window.IsMaximized() ? window.Maximize() : window.Restore();
            window.SetFullscreen(!window.IsFullscreen());
            break;
        case GLFW_KEY_F6:
            showGui = !showGui;
            break;
        case GLFW_KEY_L:
            rotateLight = !rotateLight;
            break;
        case GLFW_KEY_U:
            update = !update;
            break;
        case GLFW_KEY_PRINT_SCREEN:
        case GLFW_KEY_F3:
            screenshotter.TakeScreenshot(window, camera);
            break;
        case GLFW_KEY_ENTER:
            window.DisableCursor(fpsMode = !fpsMode);
            break;
        case GLFW_KEY_KP_ADD:
            depthLimit += depthLimit < maxDepthLimit ? 1 : 0;
            break;
        case GLFW_KEY_KP_SUBTRACT:
            depthLimit -= depthLimit > 0 ? 1 : 0;
            break;
        }
    }

    void App::OnDrop(int count, const char** paths)
    {
        if (!count) return;
        screenshotter.ReceiveScreenshot(paths[0u], camera);
    }
}
