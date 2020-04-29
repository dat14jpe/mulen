# Mulen
Mulen is an atmosphere-with-clouds renderer written in C++, rendering via OpenGL. It's still in its initial development phase, which means visuals and performance (as well as everything else, really) are in flux.

Libraries:

* [GLFW](https://github.com/glfw/glfw/)
* [glad](https://github.com/Dav1dde/glad)
* [GLM](https://github.com/g-truc/glm/)
* [ImGui](https://github.com/ocornut/imgui)
* [LodePNG](https://github.com/lvandeve/lodepng)

For now, the necessary libraries (GLFW, glad, GLM, ImGui) have to be manually copied into a lib directory inside the project root directory.

To do: more sensible CMake-employing dependency management (and the necessary portions of glad and ImGui included directly in this repository).

Ray-tracing an atmosphere volume with adaptively-higher level of detail depending on the camera location, lit by first-order light scattering:

![Ray-tracing the octree atmosphere](img/20200430_Mulen_1001.jpg)

(Admittedly extreme memory and computation time used for rendering the shown image, despite still obvious artefacts due to "low" voxel resolution. Somewhere around 5 FPS (though more moderate settings can result in real-time performance). GPU: Nvidia GTX 1080 Ti)
