cmake_minimum_required (VERSION 3.10)
project(mulen
    VERSION     0.1
    DESCRIPTION "OpenGL Cloud Renderer"
    LANGUAGES   CXX C

)
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")
set(OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# GLFW
find_package(glfw3)
if (NOT glfw3_FOUND)
  set(GLFW_DIR "${LIB_DIR}/glfw")
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  add_subdirectory(${GLFW_DIR})
endif()

# GLM
find_package(glm)
if (NOT glm_FOUND)
  set(GLM_DIR "${LIB_DIR}/glm")
endif()

# glad
set(GLAD_DIR "${LIB_DIR}/glad")
add_library("glad" "${GLAD_DIR}/src/glad.c")
target_include_directories("glad" PRIVATE "${GLAD_DIR}/include")

# Imgui
set(IMGUI_DIR "${LIB_DIR}/imgui")
add_subdirectory(${IMGUI_DIR})

# Utilities
set(UTIL_DIR "${SRC_DIR}/util")
add_subdirectory(${UTIL_DIR})

# Project
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_subdirectory(src)
if ( MSVC )
    #set_target_properties(${targetname} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${OUT_DIR} )
    #set_target_properties( ${targetname} PROPERTIES LIBRARY_OUTPUT_DIRECTORY_DEBUG ${youroutputdirectory} )
    set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY_RELEASE ${OUT_DIR} )
    # etc for the other available configuration types (MinSizeRel, RelWithDebInfo)
endif ( MSVC )
set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
