add_executable(${CMAKE_PROJECT_NAME} 
	main.cpp
    App.hpp
    App.cpp
    Common.hpp
    Atmosphere.hpp
    Atmosphere.cpp
    AtmosphereUpdater.hpp
    AtmosphereUpdater.cpp
    Octree.hpp
    Octree.cpp
    Camera.hpp
    Camera.cpp
    Object.hpp
    Object.cpp
)
set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY CXX_STANDARD 17)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${LIB_DIR}")
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${GLAD_DIR}/include")
target_link_libraries(${CMAKE_PROJECT_NAME} glfw)
target_link_libraries(${CMAKE_PROJECT_NAME} "glad" "${CMAKE_DL_LIBS}")
target_link_libraries(${CMAKE_PROJECT_NAME} imgui)

# Utils
target_include_directories("util" PRIVATE ${UTIL_DIR})
target_link_libraries(${CMAKE_PROJECT_NAME} "util")

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
     COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        ${OUT_DIR})
