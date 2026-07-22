# gpu-tracing

A C++/Vulkan hardware ray tracer focused on physically based material shading, iterative path bounces, and progressive temporal accumulation in a real-time interactive renderer.

## What it does

- Renders with a Vulkan + GLFW setup and a hardware ray tracing pipeline
- Uses raygen, miss, and closest-hit shaders to shade metal, glass, and Lambertian materials
- Shows a reflective sphere against a procedural sunny-day environment with blue sky, clouds, grass, and a tree line
- Uses ping-pong temporal accumulation images in the raygen stage for progressive frame blending
- Supports two material modes:
  - Metal with optional fuzz
  - Glass with configurable refractive index and optional hollow-shell behavior
- Includes scene interaction and material switching controls

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
- F: cycle the metal fuzz amount and reset accumulation
- T: toggle between Metal and Glass modes and reset accumulation
- I: cycle through the glass presets, including hollow-shell variants, and reset accumulation
- W/A/S/D: move the camera (forward/left/back/right)
- Left mouse click: capture/release mouse look
- Mouse move (while captured): yaw/pitch look
- Esc: release mouse capture

## Troubleshooting

- If CMake reports a generator mismatch for an existing build folder, either:
  - rerun CMake with the same generator that was used before, or
  - delete the build directory and reconfigure with your desired generator.
