# gpu-tracing

A small C++/Vulkan renderer that uses a fullscreen triangle and a fragment shader to trace a simple scene with progressive accumulation.

## What it does

- Renders with a Vulkan + GLFW setup and a shader-based path tracer
- Accumulates frames over time for a progressively refined image
- Supports two material modes:
  - Metal with optional fuzz
  - Glass with configurable refractive index and optional hollow-shell behavior
- Includes simple scene interaction and reset controls for debugging accumulation and material changes

## Requirements

- Vulkan SDK installed (LunarG or equivalent)
- GLFW installed via system package manager or vcpkg
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

## Controls

- R: move the sphere to the next test position and reset accumulation
- F: cycle the metal fuzz amount
- T: toggle between Metal and Glass modes
- I: cycle through the glass presets, including hollow-shell variants

## Notes

The renderer uses push constants and image-based accumulation in the shader pipeline. The main implementation lives in src/main.cpp and the shader logic is in shaders/solid.frag.
