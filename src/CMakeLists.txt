add_executable(${CMAKE_PROJECT_NAME} 
	main.cpp
    App.hpp
    App.cpp
    atmosphere/Common.hpp
    atmosphere/Atmosphere.hpp
    atmosphere/Atmosphere.cpp
    atmosphere/Model.hpp
    atmosphere/Model.cpp
    atmosphere/Updater.hpp
    atmosphere/Updater.cpp
    atmosphere/Generator.hpp
    atmosphere/Generator.cpp
    atmosphere/FeatureGenerator.hpp
    atmosphere/FeatureGenerator.cpp
    atmosphere/Octree.hpp
    atmosphere/Octree.cpp
    Benchmarker.hpp
    Benchmarker.cpp
    Camera.hpp
    Camera.cpp
    Object.hpp
    Object.cpp
    Screenshotter.hpp
    Screenshotter.cpp
    LightSource.hpp
)
set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY CXX_STANDARD 17)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ".")
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${LIB_DIR}")
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${GLAD_DIR}/include")
target_link_libraries(${CMAKE_PROJECT_NAME} glfw)
target_link_libraries(${CMAKE_PROJECT_NAME} "glad" "${CMAKE_DL_LIBS}")
target_link_libraries(${CMAKE_PROJECT_NAME} imgui)

# Look for appropriate thread library (needed at least on some Linux setups)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} Threads::Threads)

# Utils
target_include_directories("util" PRIVATE ${UTIL_DIR})
target_link_libraries(${CMAKE_PROJECT_NAME} "util")

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
     COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        ${OUT_DIR})
