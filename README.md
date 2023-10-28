Cornell box done in glTF format.

Large-format screenshots (may require a TDR time extension, if your framerate dips below 1 FPS, like on my 3060):

KeyPath : HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\GraphicsDrivers
KeyValue : TdrDelay
ValueType : REG_DWORD (32bit)
ValueData : Number of seconds to delay. 2 seconds is the default value.

Based off of Sascha Willems' raytracingreflections code.

Model by Rob Rau.

To install Sascha Willems' code, do the following:

Obtain the files

1) Download the base code from: https://github.com/SaschaWillems/Vulkan

2) Extract Vulkan-master directory to your hard drive (e.g. C:/dev/Vulkan-master/)

3) Open command prompt, change directory to C:/dev/Vulkan-master/

4) Type: cmake -G "Visual Studio 17 2022" -A x64

5) Download the assets (models and textures) from: https://github.com/SaschaWillems/Vulkan-Assets

6) Extract the model and textures subdirectories and the assorted files to C:/dev/Vulkan-master/assets/


Building the executables

7) Open the solution file C:/dev/Vulkan-master/vulkanExamples.sln

8) Change build setting to Release mode, x64

9) Select the Build -> Build Solution menu item (be patient, this takes a long time the first time)

10) Run an example (e.g. C:/dev/Vulkan-master/bin/Release/raytracingreflections.exe)

