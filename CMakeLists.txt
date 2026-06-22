cmake_minimum_required(VERSION 3.16)
project(gpu_tracing VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)

add_executable(hello_vulkan src/main.cpp)

target_link_libraries(hello_vulkan PRIVATE Vulkan::Vulkan glfw)
