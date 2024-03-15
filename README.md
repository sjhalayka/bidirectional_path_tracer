Vulkan/GLSL/C++ path tracer, including such features as:

- Reflection and refraction
- Chromatic aberration
- Reflection and refraction caustics
- Subsurface scattering of light by opaque objects
- Fog
- Depth of field
- High resolution screenshots
- Denoising via Intel Open Image Denoise library

This software is under heavy construction!

Screenshots may require a TDR time extension, if your framerate dips below 1 FPS, like on my 3060:

KeyPath : HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\GraphicsDrivers<br>
KeyValue : TdrDelay<br>
ValueType : REG_DWORD (32bit)<br>
ValueData : Number of seconds to delay. 2 seconds is the default value.<br>

Based off of Sascha Willems' raytracingreflections code.

Cornell boxes by Rob Rau.

![image](https://github.com/sjhalayka/bidirectional_path_tracer/assets/16404554/be067695-c673-4de1-a50d-a4df1cacce70)

![image](https://github.com/sjhalayka/bidirectional_path_tracer/assets/16404554/8bd1ed4f-ee14-401e-a788-feaffaf5bb2f)


To obtain and compile the demo code:

Obtain the files

1) Download the base code from: https://github.com/SaschaWillems/Vulkan

2) Extract Vulkan-master directory to your hard drive (e.g. C:/dev/Vulkan-master/)

3) Open command prompt, change directory to C:/dev/Vulkan-master/

4) Type: cmake -G "Visual Studio 17 2022" -A x64

5) Download the assets (models and textures) from: https://github.com/SaschaWillems/Vulkan-Assets

6) Extract the model and textures subdirectories and the assorted files to C:/dev/Vulkan-master/assets/

7) Download the OLD GLM library from: https://github.com/g-truc/glm/tree/1ad55c5016339b83b7eec98c31007e0aee57d2bf

8) Extract the contents of glm-1ad55c5016339b83b7eec98c31007e0aee57d2bf/glm subdirectory to C:/dev/Vulkan-master/external/glm

9) Download and install the Intel Open Image Denoise library.


Building the executables

1) Open the solution file C:/dev/Vulkan-master/vulkanExamples.sln

2) Change build setting to Release mode, x64

3) Select the Build -> Build Solution menu item (be patient, this takes a long time the first time)

4) Run an example (e.g. C:/dev/Vulkan-master/bin/Release/raytracingreflections.exe)


Replacing the source files

1) Copy VulkanglTFModel.cpp and VulkanglTFModel.h to C:/dev/Vulkan-master/base

2) Copy closesthit.rchit, miss.rmiss, raygen.rgen and shadow.rmiss to C:/dev/Vulkan-master/shaders/glsl/raytracingreflections

3) Copy raytracingreflections.cpp to C:/dev/Vulkan-master/examples/raytracingreflections

4) Make directory C:/temp/rob_rau_cornell/gltf/

5) Copy cornell.bin, cornell.gltf, ColorMapWithOpacityAlpha512.png and GlowMapWithReflectionAlpha512.png to C:/temp/rob_rau_cornell/gltf/

6) Delete all projects from the solution, except for ALL_BUILD, base, raytracingreflections and ZERO_CHECK.

7) Rebuild solution

8) Make a batch file rtreflect.bat in C:/dev/Vulkan-master/bin/Release/ with the following contents:

glslc.exe "C:\dev\Vulkan-master\shaders\glsl\raytracingreflections\closesthit.rchit"  --target-env=vulkan1.2 -o "C:\dev\Vulkan-master\shaders\glsl\raytracingreflections\closesthit.rchit.spv" <br>
glslc.exe "C:\dev\Vulkan-master\shaders\glsl\raytracingreflections\miss.rmiss" --target-env=vulkan1.2 -o "C:\dev\Vulkan-master\shaders\glsl\raytracingreflections\miss.rmiss.spv"<br>
glslc.exe "C:\dev\Vulkan-master\shaders\glsl\raytracingreflections\raygen.rgen" --target-env=vulkan1.2 -o "C:\dev\Vulkan-master\shaders\glsl\raytracingreflections\raygen.rgen.spv"<br>
glslc.exe "C:\dev\Vulkan-master\shaders\glsl\raytracingreflections\shadow.rmiss" --target-env=vulkan1.2 -o "C:\dev\Vulkan-master\shaders\glsl\raytracingreflections\shadow.rmiss.spv"<br>
raytracingreflections.exe






