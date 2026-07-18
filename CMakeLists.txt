cmake_minimum_required(VERSION 3.16)
project(gpu_tracing VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)

find_program(GLSLC_EXECUTABLE
    NAMES glslc
    HINTS "$ENV{VULKAN_SDK}/Bin"
)
find_program(GLSLANG_VALIDATOR_EXECUTABLE
    NAMES glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin"
)

if(GLSLC_EXECUTABLE)
    set(SHADER_COMPILER "${GLSLC_EXECUTABLE}")
    set(SHADER_COMPILER_FLAGS "--target-env=vulkan1.2")
elseif(GLSLANG_VALIDATOR_EXECUTABLE)
    set(SHADER_COMPILER "${GLSLANG_VALIDATOR_EXECUTABLE}")
    set(SHADER_COMPILER_FLAGS "-V --target-env vulkan1.2")
else()
    message(FATAL_ERROR "Neither glslc nor glslangValidator was found. Make sure Vulkan SDK is installed and VULKAN_SDK is set.")
endif()

set(SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_BINARY_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_BINARY_DIR}")
set(SHADER_COMMON_INCLUDE "${SHADER_SOURCE_DIR}/common.glsl")

file(GLOB_RECURSE SHADER_SOURCE_FILES
    "${SHADER_SOURCE_DIR}/*.vert"
    "${SHADER_SOURCE_DIR}/*.rgen"
    "${SHADER_SOURCE_DIR}/*.rmiss"
    "${SHADER_SOURCE_DIR}/*.rchit"
)

set(SHADER_BINARY_FILES "")
foreach(SHADER_SRC IN LISTS SHADER_SOURCE_FILES)
    file(RELATIVE_PATH SRC_REL "${SHADER_SOURCE_DIR}" "${SHADER_SRC}")
    get_filename_component(SRC_NAME ${SRC_REL} NAME)
    set(SHADER_OUT "${SHADER_BINARY_DIR}/${SRC_NAME}.spv")

    add_custom_command(
        OUTPUT "${SHADER_OUT}"
        COMMAND "${SHADER_COMPILER}" ${SHADER_COMPILER_FLAGS} "${SHADER_SRC}" -o "${SHADER_OUT}"
        DEPENDS "${SHADER_SRC}" "${SHADER_COMMON_INCLUDE}"
        COMMENT "Compiling ${SRC_REL} to SPIR-V"
    )

    list(APPEND SHADER_BINARY_FILES "${SHADER_OUT}")
endforeach()

add_custom_target(Shaders DEPENDS ${SHADER_BINARY_FILES})

add_executable(hello_vulkan src/main.cpp)
add_dependencies(hello_vulkan Shaders)

target_link_libraries(hello_vulkan PRIVATE Vulkan::Vulkan glfw)

target_compile_definitions(hello_vulkan PRIVATE "SHADER_DIR=\"${SHADER_BINARY_DIR}\"")
