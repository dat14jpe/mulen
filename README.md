# Mulen
Mulen is an atmosphere-with-clouds renderer written in C++, rendering via OpenGL. It's still in its initial development phase, which means visuals and performance (as well as everything else, really) are in flux.

For now, the necessary libraries (GLFW, Glad, GLM, ImGui) have to be manually copied into a lib directory inside the project root directory.

To do: more sensible CMake-employing dependency management.

Ray-tracing an atmosphere volume 1024 voxels across (at its widest; only nodes within the atmosphere shell are loaded in the octree, thereby saving most of the memory that would be wasted if a simple 1024^3 texture were used instead) with first-order light scattering:

![Ray-tracing the practical equivalent of a 1024^3 voxel volume](img/20200203_Mulen_291.png)

(altitude: 492 m, total frame time (including non-Mulen-specific overhead of approximately 0.5 ms): 2.9 ms. Lighting is static with respect to the light direction; a full light pass takes around 18 ms. GPU: Nvidia GTX 1080 Ti)
