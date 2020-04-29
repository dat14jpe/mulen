#pragma once
#include <glm/glm.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui/imgui.h>
#include <string>

class Window {
    GLFWwindow* window;
    bool vsync = true;
    glm::ivec2 storedPosition, storedSize;

public:
    Window(const std::string& title, const glm::uvec2& size);
    ~Window();
    struct App
    {
        Window& window;
        App(Window& window) : window{ window } {}

        virtual void OnFrame() {};
        virtual void OnKey(int key, int scancode, int action, int mods) {}
        virtual void OnCursorPosition(glm::dvec2 position) {}
        virtual void OnMouseButton(int button, int action, int mods) {}
        virtual void OnDrop(int count, const char** paths) {}
    };

    void Run(App& app);

    void Close(int value = 1) { glfwSetWindowShouldClose(window, value); }
    glm::ivec2 GetSize() { glm::ivec2 size; glfwGetWindowSize(window, &size.x, &size.y); return size; }

    bool IsMaximized() { return glfwGetWindowAttrib(window, GLFW_MAXIMIZED); }
    void Maximize() { glfwMaximizeWindow(window); }
    void Restore() { glfwRestoreWindow(window); }
    void SetFullscreen(bool);
    bool IsFullscreen(); 
    
    bool GetVSync() const { return vsync; }
    void SetVSync(bool vsync)
    {
        this->vsync = vsync;
        glfwSwapInterval(vsync ? 1 : 0);
    }

    bool IsKeyPressed(int key)
    {
        return glfwGetKey(window, key) == GLFW_PRESS;
    }
    bool IsKeyReleased(int key)
    {
        return glfwGetKey(window, key) == GLFW_RELEASE;
    }
    bool IsMouseButtonPressed(int key)
    {
        return glfwGetMouseButton(window, key) == GLFW_PRESS;
    }
    glm::dvec2 GetCursorPosition()
    {
        glm::dvec2 pos;
        glfwGetCursorPos(window, &pos.x, &pos.y);
        return pos;
    }
};
