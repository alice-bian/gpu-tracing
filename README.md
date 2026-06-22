# gpu-tracing Vulkan Starter

Minimal C++/Vulkan starter to open a GLFW window and initialize a Vulkan instance.

## Requirements
- Vulkan SDK installed (LunarG or equivalent)
- GLFW installed via system package manager or `vcpkg`
- CMake 3.16+

## Build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

If using vcpkg:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
```

## Run

```powershell
build\Release\hello_vulkan.exe
```

## Notes

This sample creates a Vulkan instance, debug messenger, GLFW window, and a Vulkan surface. It does not yet perform rendering.
