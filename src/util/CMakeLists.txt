set(LIB_NAME "util")
add_library(${LIB_NAME}
    GLObject.hpp
    Buffer.hpp
    Files.hpp
    Framebuffer.hpp
    Shader.hpp
    Shader.cpp
    Texture.hpp
    VertexArray.hpp
    Window.hpp
    Window.cpp
    Timer.hpp
    Timer.cpp
)
set_property(TARGET ${LIB_NAME} PROPERTY CXX_STANDARD 17)

target_include_directories(${LIB_NAME} PRIVATE "${LIB_DIR}")
target_link_libraries(${LIB_NAME} glfw)
target_include_directories(${LIB_NAME} PRIVATE "${GLAD_DIR}/include")
target_link_libraries(${LIB_NAME} imgui)
