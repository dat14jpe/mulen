#include "Window.hpp"
#include <imgui/imgui.h>
#include <imgui/examples/imgui_impl_opengl3.h>
#include <imgui/examples/imgui_impl_glfw.h>
#include <iostream>

static GLFWwindow* initialize(const std::string& title, const glm::uvec2& size)
{
    int glfwInitRes = glfwInit();
    if (!glfwInitRes) 
    {
        std::cerr << "Unable to initialize GLFW\n";
        return nullptr;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(size.x, size.y, title.c_str(), nullptr, nullptr);
    if (!window) 
    {
        std::cerr << "Unable to create GLFW window\n";
        glfwTerminate();
        return nullptr;
    }

    glfwMakeContextCurrent(window);

    int gladInitRes = gladLoadGL();
    if (!gladInitRes) 
    {
        std::cerr << "Unable to initialize glad\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return nullptr;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // Setup Platform/Renderer bindings
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init();
    // - to do: check for errors

    return window;
}

static void errorCallback(int error, const char* description) 
{
    std::cerr << "GLFW error " << error << ": " << description << "\n";
}

static Window::App* GetApp(GLFWwindow* window) 
{
    return reinterpret_cast<Window::App*>(glfwGetWindowUserPointer(window));
}

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    GetApp(window)->OnKey(key, scancode, action, mods);
}
static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    GetApp(window)->OnCursorPosition({ xpos, ypos });
}
static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    GetApp(window)->OnMouseButton(button, action, mods);
}

Window::Window(const std::string& title, const glm::uvec2& size)
{
    //glfwWindowHint(GLFW_MAXIMIZED, GL_TRUE);
    glfwSetErrorCallback(errorCallback); // - maybe shouldn't exactly be in Window
    window = initialize(title, size);
}

void Window::Run(Window::App& app)
{
    if (!window) return;
    glfwSetWindowUserPointer(window, &app);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);

    while (!glfwWindowShouldClose(window))
    {
        /*glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);*/

        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        app.OnFrame();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);

        // If you are using this code with non-legacy OpenGL header/contexts (which you should not, prefer using imgui_impl_opengl3.cpp!!),
        // you may need to backup/reset/restore current shader using the commented lines below.
        //GLint last_program;
        //glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
        //glUseProgram(0);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        //glUseProgram(last_program);

        glfwSwapBuffers(window); 
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

Window::~Window()
{
    if (!window) return;
    glfwDestroyWindow(window);
    glfwTerminate();
}
