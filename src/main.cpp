#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <set>
#include <string>
#include <array>
#include <cstring>
#include <vector>

const uint32_t WIDTH = 1280;
const uint32_t HEIGHT = 720;

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
    VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    VK_KHR_SPIRV_1_4_EXTENSION_NAME,
    VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME
};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct CameraPushConstants {
    std::array<float, 3> origin;
    float pad0;
    std::array<float, 3> u;
    float pad1;
    std::array<float, 3> v;
    float pad2;
    std::array<float, 3> w;
    float pad3;
    std::array<float, 3> sphereCenter;
    float sphereFuzz;
    float materialType;
    float sphereIor;
    float hollowShell;
    float fov;
    float aspect;
    float width;
    float height;
    float frame;
};

std::string joinStrings(const std::vector<std::string>& values) {
    if (values.empty()) {
        return "";
    }
    std::ostringstream oss;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << values[i];
    }
    return oss.str();
}

std::vector<char> readFile(const std::filesystem::path& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename.string());
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
    file.close();

    return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }
    return shaderModule;
}

std::filesystem::path getShaderDirectory() {
#ifdef SHADER_DIR
    return std::filesystem::path(SHADER_DIR);
#else
    return std::filesystem::current_path() / "shaders";
#endif
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    (void)messageSeverity;
    (void)messageType;
    (void)pUserData;
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator) {
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

bool checkValidationLayerSupport() {
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool layerFound = false;
        for (const auto& layerProperties : availableLayers) {
            if (std::strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

std::vector<const char*> getRequiredExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    return extensions;
}

std::array<float, 3> crossVec3(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]
    };
}

float lengthVec3(const std::array<float, 3>& v) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

std::array<float, 3> normalizeVec3(const std::array<float, 3>& v) {
    float len = lengthVec3(v);
    if (len <= 1e-6f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return {v[0] / len, v[1] / len, v[2] / len};
}

std::vector<std::string> getMissingDeviceExtensions(VkPhysicalDevice device) {
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return std::vector<std::string>(requiredExtensions.begin(), requiredExtensions.end());
}

bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
    return getMissingDeviceExtensions(device).empty();
}

struct RayTracingFeatureSupport {
    VkBool32 accelerationStructure = VK_FALSE;
    VkBool32 rayTracingPipeline = VK_FALSE;
    VkBool32 bufferDeviceAddress = VK_FALSE;
};

RayTracingFeatureSupport queryRayTracingFeatureSupport(VkPhysicalDevice device) {
    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{};
    bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};
    accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelerationStructureFeatures.pNext = &bufferDeviceAddressFeatures;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{};
    rayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rayTracingPipelineFeatures.pNext = &accelerationStructureFeatures;

    VkPhysicalDeviceFeatures2 deviceFeatures2{};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.pNext = &rayTracingPipelineFeatures;
    vkGetPhysicalDeviceFeatures2(device, &deviceFeatures2);

    RayTracingFeatureSupport support{};
    support.accelerationStructure = accelerationStructureFeatures.accelerationStructure;
    support.rayTracingPipeline = rayTracingPipelineFeatures.rayTracingPipeline;
    support.bufferDeviceAddress = bufferDeviceAddressFeatures.bufferDeviceAddress;
    return support;
}

QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (queueFamily.queueCount > 0 && presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }

        i++;
    }

    return indices;
}

SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    } else {
        VkExtent2D actualExtent = {WIDTH, HEIGHT};

        actualExtent.width = std::max(capabilities.minImageExtent.width,
                                      std::min(capabilities.maxImageExtent.width, actualExtent.width));
        actualExtent.height = std::max(capabilities.minImageExtent.height,
                                       std::min(capabilities.maxImageExtent.height, actualExtent.height));

        return actualExtent;
    }
}

bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices = findQueueFamilies(device, surface);
    bool extensionsSupported = checkDeviceExtensionSupport(device);
    RayTracingFeatureSupport rtSupport = queryRayTracingFeatureSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device, surface);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);

        return indices.isComplete() && extensionsSupported && swapChainAdequate &&
            rtSupport.accelerationStructure &&
            rtSupport.rayTracingPipeline &&
            rtSupport.bufferDeviceAddress &&
           (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
            deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
}

VkPhysicalDevice pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    std::ostringstream diagnostics;

    for (const auto& device : devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);

        if (isDeviceSuitable(device, surface)) {
            return device;
        }

        QueueFamilyIndices indices = findQueueFamilies(device, surface);
        std::vector<std::string> missingExtensions = getMissingDeviceExtensions(device);

        bool swapChainAdequate = false;
        if (missingExtensions.empty()) {
            SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device, surface);
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
        }

        RayTracingFeatureSupport rtSupport = queryRayTracingFeatureSupport(device);
        diagnostics << "Device '" << properties.deviceName << "' is not suitable:";
        if (!indices.isComplete()) {
            diagnostics << " missing required queue families;";
        }
        if (!missingExtensions.empty()) {
            diagnostics << " missing extensions: " << joinStrings(missingExtensions) << ";";
        }
        if (!swapChainAdequate) {
            diagnostics << " swapchain support incomplete;";
        }
        if (!rtSupport.accelerationStructure) {
            diagnostics << " accelerationStructure feature is not supported;";
        }
        if (!rtSupport.rayTracingPipeline) {
            diagnostics << " rayTracingPipeline feature is not supported;";
        }
        if (!rtSupport.bufferDeviceAddress) {
            diagnostics << " bufferDeviceAddress feature is not supported;";
        }
        diagnostics << std::endl;
    }

    throw std::runtime_error("failed to find a suitable GPU for the requested Vulkan features/extensions.\n" + diagnostics.str());
}

class HelloVulkan {
public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    struct GpuBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
    };

    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties{};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{};
    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{};
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;

    VkImage accumulationImages[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory accumulationImageMemories[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView accumulationImageViews[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    bool accumulationImagesInitialized = false;

    PFN_vkCreateAccelerationStructureKHR fpCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR fpDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR fpGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR fpCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR fpGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR fpCreateRayTracingPipelinesKHR = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR fpGetRayTracingShaderGroupHandlesKHR = nullptr;
    PFN_vkCmdTraceRaysKHR fpCmdTraceRaysKHR = nullptr;

    GpuBuffer sphereVertexBuffer;
    GpuBuffer sphereIndexBuffer;
    GpuBuffer blasBuffer;
    GpuBuffer tlasBuffer;
    GpuBuffer blasScratchBuffer;
    GpuBuffer tlasScratchBuffer;
    GpuBuffer tlasInstanceBuffer;
    GpuBuffer rtSbtBuffer;
    VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
    VkDeviceAddress blasDeviceAddress = 0;
    uint32_t sphereTriangleCount = 0;
    uint32_t sphereVertexCount = 0;

    VkDescriptorSetLayout rtDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool rtDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet rtDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout rtPipelineLayout = VK_NULL_HANDLE;
    VkPipeline rtPipeline = VK_NULL_HANDLE;
    VkImage rtOutputImage = VK_NULL_HANDLE;
    VkDeviceMemory rtOutputImageMemory = VK_NULL_HANDLE;
    VkImageView rtOutputImageView = VK_NULL_HANDLE;
    VkStridedDeviceAddressRegionKHR rtRaygenRegion{};
    VkStridedDeviceAddressRegionKHR rtMissRegion{};
    VkStridedDeviceAddressRegionKHR rtHitRegion{};
    VkStridedDeviceAddressRegionKHR rtCallableRegion{};
    VkDeviceSize rtSbtStride = 0;
    VkDeviceSize rtSbtRegionSize = 0;
    std::array<std::array<float, 3>, 3> spherePositions{
        std::array<float, 3>{0.0f, 0.0f, -1.0f},
        std::array<float, 3>{0.6f, 0.1f, -0.6f},
        std::array<float, 3>{-0.4f, 0.2f, -1.2f}
    };
    std::array<float, 3> sphereCenter = spherePositions[1];
    std::array<float, 7> fuzzPresets{
        0.0f,
        0.05f,
        0.15f,
        0.3f,
        0.45f,
        0.65f,
        0.85f
    };
    struct GlassPreset {
        float ior;
        float hollow;
    };
    std::array<GlassPreset, 6> glassPresets{
        GlassPreset{1.0f, 0.0f},
        GlassPreset{1.0f, 1.0f},
        GlassPreset{1.33f, 0.0f},
        GlassPreset{1.5f, 0.0f},
        GlassPreset{1.5f, 1.0f},
        GlassPreset{2.4f, 0.0f}
    };
    bool useGlass = true;
    float sphereFuzz = fuzzPresets[0];
    float materialType = 0.0f;
    float sphereIor = glassPresets[3].ior;
    float hollowShell = glassPresets[3].hollow;
    float cameraFov = 60.0f;
    uint32_t currentSphereIndex = 1;
    uint32_t currentFuzzIndex = 0;
    uint32_t currentIorIndex = 3;
    uint32_t frameCount = 0;
    std::array<float, 3> cameraPosition = {0.0f, 0.0f, 0.0f};
    float cameraYaw = -90.0f;
    float cameraPitch = 0.0f;
    float cameraMoveSpeed = 3.5f;
    float mouseSensitivity = 0.1f;
    bool mouseCaptured = false;
    bool mouseToggleRequested = false;
    bool escapeKeyDown = false;
    bool firstMouseSample = true;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;

    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
        (void)mods;
        if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) {
            return;
        }
        auto* app = reinterpret_cast<HelloVulkan*>(glfwGetWindowUserPointer(window));
        if (app != nullptr) {
            app->mouseToggleRequested = true;
        }
    }

    void setMouseCapture(bool capture) {
        mouseCaptured = capture;
        glfwSetInputMode(window, GLFW_CURSOR, capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, capture ? GLFW_TRUE : GLFW_FALSE);
        }
        firstMouseSample = true;
    }

    static float degreesToRadians(float degrees) {
        return degrees * 0.01745329251994329577f;
    }

    std::array<float, 3> getCameraForward() const {
        float yawRad = degreesToRadians(cameraYaw);
        float pitchRad = degreesToRadians(cameraPitch);
        return normalizeVec3({
            std::cos(yawRad) * std::cos(pitchRad),
            std::sin(pitchRad),
            std::sin(yawRad) * std::cos(pitchRad)
        });
    }

    bool updateCamera(float deltaTime) {
        bool cameraChanged = false;

        std::array<float, 3> worldUp = {0.0f, 1.0f, 0.0f};
        std::array<float, 3> forward = getCameraForward();
        std::array<float, 3> right = normalizeVec3(crossVec3(forward, worldUp));
        std::array<float, 3> moveInput = {0.0f, 0.0f, 0.0f};

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            moveInput[0] += forward[0];
            moveInput[1] += forward[1];
            moveInput[2] += forward[2];
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            moveInput[0] -= forward[0];
            moveInput[1] -= forward[1];
            moveInput[2] -= forward[2];
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            moveInput[0] += right[0];
            moveInput[1] += right[1];
            moveInput[2] += right[2];
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            moveInput[0] -= right[0];
            moveInput[1] -= right[1];
            moveInput[2] -= right[2];
        }

        float moveLengthSquared = moveInput[0] * moveInput[0] +
                                  moveInput[1] * moveInput[1] +
                                  moveInput[2] * moveInput[2];
        if (moveLengthSquared > 0.0f) {
            std::array<float, 3> moveDir = normalizeVec3(moveInput);
            float velocity = cameraMoveSpeed * deltaTime;
            cameraPosition[0] += moveDir[0] * velocity;
            cameraPosition[1] += moveDir[1] * velocity;
            cameraPosition[2] += moveDir[2] * velocity;
            cameraChanged = true;
        }

        if (mouseCaptured) {
            double cursorX = 0.0;
            double cursorY = 0.0;
            glfwGetCursorPos(window, &cursorX, &cursorY);
            if (firstMouseSample) {
                lastMouseX = cursorX;
                lastMouseY = cursorY;
                firstMouseSample = false;
            }

            float xOffset = static_cast<float>(cursorX - lastMouseX);
            float yOffset = static_cast<float>(lastMouseY - cursorY);
            lastMouseX = cursorX;
            lastMouseY = cursorY;

            if (xOffset != 0.0f || yOffset != 0.0f) {
                cameraYaw += xOffset * mouseSensitivity;
                cameraPitch += yOffset * mouseSensitivity;

                if (cameraPitch > 89.0f) {
                    cameraPitch = 89.0f;
                }
                if (cameraPitch < -89.0f) {
                    cameraPitch = -89.0f;
                }
                cameraChanged = true;
            }
        }

        return cameraChanged;
    }

    void initWindow() {
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        window = glfwCreateWindow(WIDTH, HEIGHT, "Hello Vulkan", nullptr, nullptr);
        glfwSetWindowUserPointer(window, this);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
    }

    void initVulkan() {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createCommandPool();
        createRayTracingAccelerationStructures();
        createRayTracingResources();
        createAccumulationResources();
        createCommandBuffers();
        createSyncObjects();
    }

    void updateWindowTitle() {
        std::string title = useGlass ? "Glass" : "Metal";
        title += " ";
        if (useGlass) {
            title += "I=" + std::to_string(sphereIor);
            if (hollowShell > 0.5f) {
                title += " hollow";
            }
            title += " (R,I,T)";
        } else {
            title += "F=" + std::to_string(sphereFuzz);
            title += " (R,F,T)";
        }
        glfwSetWindowTitle(window, title.c_str());
    }

    void mainLoop() {
        bool resetKeyDown = false;
        bool fuzzKeyDown = false;
        bool iorKeyDown = false;
        bool toggleKeyDown = false;
        double lastFrameTime = glfwGetTime();

        materialType = useGlass ? 2.0f : 0.0f;

        updateWindowTitle();

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            if (mouseToggleRequested) {
                setMouseCapture(!mouseCaptured);
                mouseToggleRequested = false;
            }

            bool escPressed = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
            if (escPressed && !escapeKeyDown && mouseCaptured) {
                setMouseCapture(false);
            }
            escapeKeyDown = escPressed;

            double currentFrameTime = glfwGetTime();
            float deltaTime = static_cast<float>(currentFrameTime - lastFrameTime);
            lastFrameTime = currentFrameTime;

            bool rPressed = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
            if (rPressed && !resetKeyDown) {
                currentSphereIndex = (currentSphereIndex + 1) % static_cast<uint32_t>(spherePositions.size());
                sphereCenter = spherePositions[currentSphereIndex];
                rebuildTopLevelASForCurrentSphereCenter();
                resetAccumulation();
                resetKeyDown = true;
            } else if (!rPressed) {
                resetKeyDown = false;
            }

            bool iPressed = glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS;
            if (iPressed && !iorKeyDown) {
                if (useGlass) {
                    currentIorIndex = (currentIorIndex + 1) % static_cast<uint32_t>(glassPresets.size());
                    sphereIor = glassPresets[currentIorIndex].ior;
                    hollowShell = glassPresets[currentIorIndex].hollow;
                    resetAccumulation();
                    updateWindowTitle();
                }
                iorKeyDown = true;
            } else if (!iPressed) {
                iorKeyDown = false;
            }

            bool fPressed = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
            if (fPressed && !fuzzKeyDown) {
                if (!useGlass) {
                    currentFuzzIndex = (currentFuzzIndex + 1) % static_cast<uint32_t>(fuzzPresets.size());
                    sphereFuzz = fuzzPresets[currentFuzzIndex];
                    resetAccumulation();
                    updateWindowTitle();
                }
                fuzzKeyDown = true;
            } else if (!fPressed) {
                fuzzKeyDown = false;
            }

            bool tPressed = glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS;
            if (tPressed && !toggleKeyDown) {
                useGlass = !useGlass;
                materialType = useGlass ? 2.0f : 0.0f;
                resetAccumulation();
                updateWindowTitle();
                toggleKeyDown = true;
            } else if (!tPressed) {
                toggleKeyDown = false;
            }

            if (updateCamera(deltaTime)) {
                resetAccumulation();
            }

            drawFrame();
        }
        vkDeviceWaitIdle(device);
    }

    void cleanup() {
        if (renderFinishedSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
        }
        if (imageAvailableSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
        }
        destroyRayTracingResources();
        destroyRayTracingAccelerationStructures();
        if (commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandPool, nullptr);
        }
        for (auto imageView : accumulationImageViews) {
            if (imageView != VK_NULL_HANDLE) {
                vkDestroyImageView(device, imageView, nullptr);
            }
        }
        for (int i = 0; i < 2; i++) {
            if (accumulationImages[i] != VK_NULL_HANDLE) {
                vkDestroyImage(device, accumulationImages[i], nullptr);
            }
            if (accumulationImageMemories[i] != VK_NULL_HANDLE) {
                vkFreeMemory(device, accumulationImageMemories[i], nullptr);
            }
        }
        for (auto imageView : swapChainImageViews) {
            vkDestroyImageView(device, imageView, nullptr);
        }
        if (swapChain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapChain, nullptr);
        }
        if (device != VK_NULL_HANDLE) {
            vkDestroyDevice(device, nullptr);
        }
        if (surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
        }
        if (enableValidationLayers) {
            DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
        }
        if (window != nullptr) {
            glfwDestroyWindow(window);
            glfwTerminate();
        }
    }

    void createInstance() {
        if (enableValidationLayers && !checkValidationLayerSupport()) {
            throw std::runtime_error("validation layers requested, but not available!");
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Hello Vulkan";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        auto extensions = getRequiredExtensions();
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();

            populateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext = &debugCreateInfo;
        } else {
            createInfo.enabledLayerCount = 0;
            createInfo.pNext = nullptr;
        }

        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error("failed to create Vulkan instance!");
        }
    }

    void setupDebugMessenger() {
        if (!enableValidationLayers) return;

        VkDebugUtilsMessengerCreateInfoEXT createInfo;
        populateDebugMessengerCreateInfo(createInfo);

        if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
            throw std::runtime_error("failed to set up debug messenger!");
        }
    }

    void createSurface() {
        if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
            throw std::runtime_error("failed to create window surface!");
        }
    }

    void pickPhysicalDevice() {
        physicalDevice = ::pickPhysicalDevice(instance, surface);

        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);
        std::cout << "Selected GPU: " << properties.deviceName << std::endl;

        rayTracingPipelineProperties = {};
        rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        VkPhysicalDeviceProperties2 properties2{};
        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties2.pNext = &rayTracingPipelineProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);

        bufferDeviceAddressFeatures = {};
        bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        accelerationStructureFeatures = {};
        accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        accelerationStructureFeatures.pNext = &bufferDeviceAddressFeatures;
        rayTracingPipelineFeatures = {};
        rayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        rayTracingPipelineFeatures.pNext = &accelerationStructureFeatures;

        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &rayTracingPipelineFeatures;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

        std::cout << "Ray tracing supported: " << (rayTracingPipelineFeatures.rayTracingPipeline ? "YES" : "NO") << std::endl;
        std::cout << "Acceleration structures supported: " << (accelerationStructureFeatures.accelerationStructure ? "YES" : "NO") << std::endl;
        std::cout << "Max ray recursion depth: " << rayTracingPipelineProperties.maxRayRecursionDepth << std::endl;
        std::cout << "Shader group handle size: " << rayTracingPipelineProperties.shaderGroupHandleSize << " bytes" << std::endl;

        std::vector<std::string> missingExtensions = getMissingDeviceExtensions(physicalDevice);
        if (!missingExtensions.empty() ||
            !rayTracingPipelineFeatures.rayTracingPipeline ||
            !accelerationStructureFeatures.accelerationStructure ||
            !bufferDeviceAddressFeatures.bufferDeviceAddress) {
            std::ostringstream message;
            message << "Selected GPU does not fully support required ray tracing capabilities.";
            if (!missingExtensions.empty()) {
                message << " Missing extensions: " << joinStrings(missingExtensions) << ".";
            }
            if (!rayTracingPipelineFeatures.rayTracingPipeline) {
                message << " Missing feature: rayTracingPipeline.";
            }
            if (!accelerationStructureFeatures.accelerationStructure) {
                message << " Missing feature: accelerationStructure.";
            }
            if (!bufferDeviceAddressFeatures.bufferDeviceAddress) {
                message << " Missing feature: bufferDeviceAddress.";
            }
            throw std::runtime_error(message.str());
        }
    }

    void createLogicalDevice() {
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice, surface);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeaturesEnabled{};
        bufferDeviceAddressFeaturesEnabled.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        bufferDeviceAddressFeaturesEnabled.bufferDeviceAddress = VK_TRUE;

        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeaturesEnabled{};
        accelerationStructureFeaturesEnabled.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        accelerationStructureFeaturesEnabled.accelerationStructure = VK_TRUE;
        accelerationStructureFeaturesEnabled.pNext = &bufferDeviceAddressFeaturesEnabled;

        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeaturesEnabled{};
        rayTracingPipelineFeaturesEnabled.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        rayTracingPipelineFeaturesEnabled.rayTracingPipeline = VK_TRUE;
        rayTracingPipelineFeaturesEnabled.pNext = &accelerationStructureFeaturesEnabled;

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.pNext = &rayTracingPipelineFeaturesEnabled;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
        } else {
            createInfo.enabledLayerCount = 0;
        }

        if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
            throw std::runtime_error("failed to create logical device!");
        }

        vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
        vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
        loadRayTracingFunctionPointers();
    }

    void createSwapChain() {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice, surface);

        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        QueueFamilyIndices indices = findQueueFamilies(physicalDevice, surface);
        uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        if (indices.graphicsFamily != indices.presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = nullptr;
        }

        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
            throw std::runtime_error("failed to create swap chain!");
        }

        vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
        swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

        swapChainImageFormat = surfaceFormat.format;
        swapChainExtent = extent;
    }

    void createImageViews() {
        swapChainImageViews.resize(swapChainImages.size());

        for (size_t i = 0; i < swapChainImages.size(); i++) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = swapChainImages[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = swapChainImageFormat;
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device, &createInfo, nullptr, &swapChainImageViews[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create image views!");
            }
        }
    }

    void createCommandPool() {
        QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice, surface);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create command pool!");
        }
    }

    void loadRayTracingFunctionPointers() {
        fpCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
            vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR"));
        fpDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
            vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR"));
        fpGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
            vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR"));
        fpCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
            vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR"));
        fpGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
            vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR"));

        if (fpCreateAccelerationStructureKHR == nullptr ||
            fpDestroyAccelerationStructureKHR == nullptr ||
            fpGetAccelerationStructureBuildSizesKHR == nullptr ||
            fpCmdBuildAccelerationStructuresKHR == nullptr ||
            fpGetAccelerationStructureDeviceAddressKHR == nullptr) {
            throw std::runtime_error("failed to load required ray tracing device function pointers");
        }

        fpCreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(
            vkGetDeviceProcAddr(device, "vkCreateRayTracingPipelinesKHR"));
        fpGetRayTracingShaderGroupHandlesKHR = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(
            vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesKHR"));
        fpCmdTraceRaysKHR = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(
            vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR"));

        if (fpCreateRayTracingPipelinesKHR == nullptr ||
            fpGetRayTracingShaderGroupHandlesKHR == nullptr ||
            fpCmdTraceRaysKHR == nullptr) {
            throw std::runtime_error("failed to load ray tracing pipeline function pointers");
        }
    }

    void createImage(VkExtent2D extent,
                     VkFormat format,
                     VkImageUsageFlags usage,
                     VkImage& outImage,
                     VkDeviceMemory& outMemory) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = extent.width;
        imageInfo.extent.height = extent.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(device, &imageInfo, nullptr, &outImage) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, outImage, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate image memory");
        }

        if (vkBindImageMemory(device, outImage, outMemory, 0) != VK_SUCCESS) {
            throw std::runtime_error("failed to bind image memory");
        }
    }

    void destroyImage(VkImage& image, VkDeviceMemory& memory, VkImageView& view) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
            view = VK_NULL_HANDLE;
        }
        if (image != VK_NULL_HANDLE) {
            vkDestroyImage(device, image, nullptr);
            image = VK_NULL_HANDLE;
        }
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }
    }

    void transitionImageLayout(VkCommandBuffer commandBuffer,
                               VkImage image,
                               VkImageLayout oldLayout,
                               VkImageLayout newLayout,
                               VkPipelineStageFlags srcStage,
                               VkPipelineStageFlags dstStage,
                               VkAccessFlags srcAccess,
                               VkAccessFlags dstAccess) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;

        vkCmdPipelineBarrier(commandBuffer,
                             srcStage,
                             dstStage,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);
    }

    VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) const {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    void createRayTracingOutputImage() {
        createImage(swapChainExtent,
                    VK_FORMAT_R32G32B32A32_SFLOAT,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    rtOutputImage,
                    rtOutputImageMemory);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = rtOutputImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &rtOutputImageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create RT output image view");
        }

        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        transitionImageLayout(commandBuffer,
                              rtOutputImage,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_GENERAL,
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                              0,
                              VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT);
        endSingleTimeCommands(commandBuffer);
    }

    void createRayTracingDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding outputBinding{};
        outputBinding.binding = 0;
        outputBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        outputBinding.descriptorCount = 1;
        outputBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        VkDescriptorSetLayoutBinding tlasBinding{};
        tlasBinding.binding = 1;
        tlasBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        tlasBinding.descriptorCount = 1;
        tlasBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {outputBinding, tlasBinding};

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &rtDescriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create RT descriptor set layout");
        }
    }

    void createRayTracingDescriptorPool() {
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        poolSizes[1].descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &rtDescriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create RT descriptor pool");
        }
    }

    void createRayTracingDescriptorSet() {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = rtDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &rtDescriptorSetLayout;

        if (vkAllocateDescriptorSets(device, &allocInfo, &rtDescriptorSet) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate RT descriptor set");
        }

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo.imageView = rtOutputImageView;
        imageInfo.sampler = VK_NULL_HANDLE;

        VkWriteDescriptorSet writes[1]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = rtDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, writes, 0, nullptr);
        updateRayTracingTlasDescriptor();
    }

    void updateRayTracingTlasDescriptor() {
        VkWriteDescriptorSetAccelerationStructureKHR asInfo{};
        asInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        asInfo.accelerationStructureCount = 1;
        asInfo.pAccelerationStructures = &tlas;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = rtDescriptorSet;
        write.dstBinding = 1;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        write.pNext = &asInfo;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    void createRayTracingPipeline() {
        auto shaderDir = getShaderDirectory();
        auto raygenCode = readFile(shaderDir / "raygen.rgen.spv");
        auto missCode = readFile(shaderDir / "miss.rmiss.spv");
        auto closestHitCode = readFile(shaderDir / "closesthit.rchit.spv");

        VkShaderModule raygenModule = createShaderModule(device, raygenCode);
        VkShaderModule missModule = createShaderModule(device, missCode);
        VkShaderModule closestHitModule = createShaderModule(device, closestHitCode);

        VkPipelineShaderStageCreateInfo stages[3]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        stages[0].module = raygenModule;
        stages[0].pName = "main";

        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
        stages[1].module = missModule;
        stages[1].pName = "main";

        stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[2].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        stages[2].module = closestHitModule;
        stages[2].pName = "main";

        VkRayTracingShaderGroupCreateInfoKHR groups[3]{};
        groups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[0].generalShader = 0;
        groups[0].closestHitShader = VK_SHADER_UNUSED_KHR;

        groups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[1].generalShader = 1;
        groups[1].closestHitShader = VK_SHADER_UNUSED_KHR;

        groups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        groups[2].generalShader = VK_SHADER_UNUSED_KHR;
        groups[2].closestHitShader = 2;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &rtDescriptorSetLayout;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(CameraPushConstants);
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &rtPipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create RT pipeline layout");
        }

        VkRayTracingPipelineCreateInfoKHR pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        pipelineInfo.stageCount = 3;
        pipelineInfo.pStages = stages;
        pipelineInfo.groupCount = 3;
        pipelineInfo.pGroups = groups;
        pipelineInfo.maxPipelineRayRecursionDepth = 1;
        pipelineInfo.layout = rtPipelineLayout;

        if (fpCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rtPipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create RT pipeline");
        }

        vkDestroyShaderModule(device, closestHitModule, nullptr);
        vkDestroyShaderModule(device, missModule, nullptr);
        vkDestroyShaderModule(device, raygenModule, nullptr);
    }

    void createRayTracingShaderBindingTable() {
        uint32_t groupCount = 3;
        VkDeviceSize handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
        VkDeviceSize handleAlignment = rayTracingPipelineProperties.shaderGroupHandleAlignment;
        VkDeviceSize baseAlignment = rayTracingPipelineProperties.shaderGroupBaseAlignment;

        rtSbtStride = alignUp(handleSize, handleAlignment);
        rtSbtRegionSize = rtSbtStride;

        VkDeviceSize raygenOffset = 0;
        VkDeviceSize missOffset = alignUp(raygenOffset + rtSbtRegionSize, baseAlignment);
        VkDeviceSize hitOffset = alignUp(missOffset + rtSbtRegionSize, baseAlignment);
        VkDeviceSize sbtSize = alignUp(hitOffset + rtSbtRegionSize, baseAlignment);

        createBuffer(sbtSize,
                     VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     rtSbtBuffer,
                     true);

        std::vector<uint8_t> handles(static_cast<size_t>(handleSize) * groupCount);
        if (fpGetRayTracingShaderGroupHandlesKHR(device, rtPipeline, 0, groupCount, handles.size(), handles.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to fetch RT shader group handles");
        }

        void* mapped = nullptr;
        vkMapMemory(device, rtSbtBuffer.memory, 0, sbtSize, 0, &mapped);
        std::memset(mapped, 0, static_cast<size_t>(sbtSize));
        for (uint32_t group = 0; group < groupCount; group++) {
            VkDeviceSize dstOffset = group == 0 ? raygenOffset : (group == 1 ? missOffset : hitOffset);
            std::memcpy(static_cast<uint8_t*>(mapped) + dstOffset,
                        handles.data() + static_cast<size_t>(group) * handleSize,
                        static_cast<size_t>(handleSize));
        }
        vkUnmapMemory(device, rtSbtBuffer.memory);

        VkDeviceAddress baseAddress = getBufferDeviceAddress(rtSbtBuffer.buffer);
        rtRaygenRegion.deviceAddress = baseAddress + raygenOffset;
        rtRaygenRegion.stride = rtSbtStride;
        rtRaygenRegion.size = rtSbtRegionSize;

        rtMissRegion.deviceAddress = baseAddress + missOffset;
        rtMissRegion.stride = rtSbtStride;
        rtMissRegion.size = rtSbtRegionSize;

        rtHitRegion.deviceAddress = baseAddress + hitOffset;
        rtHitRegion.stride = rtSbtStride;
        rtHitRegion.size = rtSbtRegionSize;

        rtCallableRegion = {};
    }

    void createRayTracingResources() {
        createRayTracingOutputImage();
        createRayTracingDescriptorSetLayout();
        createRayTracingDescriptorPool();
        createRayTracingDescriptorSet();
        createRayTracingPipeline();
        createRayTracingShaderBindingTable();
    }

    void destroyRayTracingResources() {
        destroyBuffer(rtSbtBuffer);
        if (rtPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, rtPipeline, nullptr);
            rtPipeline = VK_NULL_HANDLE;
        }
        if (rtPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, rtPipelineLayout, nullptr);
            rtPipelineLayout = VK_NULL_HANDLE;
        }
        if (rtDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, rtDescriptorPool, nullptr);
            rtDescriptorPool = VK_NULL_HANDLE;
        }
        if (rtDescriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, rtDescriptorSetLayout, nullptr);
            rtDescriptorSetLayout = VK_NULL_HANDLE;
        }
        destroyImage(rtOutputImage, rtOutputImageMemory, rtOutputImageView);
    }

    void createBuffer(VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      GpuBuffer& outBuffer,
                      bool enableDeviceAddress) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer.buffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create buffer");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, outBuffer.buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        VkMemoryAllocateFlagsInfo allocFlagsInfo{};
        if (enableDeviceAddress) {
            allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
            allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
            allocInfo.pNext = &allocFlagsInfo;
        }

        if (vkAllocateMemory(device, &allocInfo, nullptr, &outBuffer.memory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate buffer memory");
        }

        if (vkBindBufferMemory(device, outBuffer.buffer, outBuffer.memory, 0) != VK_SUCCESS) {
            throw std::runtime_error("failed to bind buffer memory");
        }

        outBuffer.size = size;
    }

    void destroyBuffer(GpuBuffer& buffer) {
        if (buffer.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, buffer.buffer, nullptr);
            buffer.buffer = VK_NULL_HANDLE;
        }
        if (buffer.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, buffer.memory, nullptr);
            buffer.memory = VK_NULL_HANDLE;
        }
        buffer.size = 0;
    }

    VkDeviceAddress getBufferDeviceAddress(VkBuffer buffer) {
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = buffer;
        return vkGetBufferDeviceAddress(device, &addressInfo);
    }

    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
        endSingleTimeCommands(commandBuffer);
    }

    void generateSphereMesh(uint32_t stacks,
                            uint32_t slices,
                            float radius,
                            std::vector<float>& vertices,
                            std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();
        vertices.reserve(static_cast<size_t>(stacks + 1) * static_cast<size_t>(slices + 1) * 3);
        indices.reserve(static_cast<size_t>(stacks) * static_cast<size_t>(slices) * 6);

        const float pi = 3.14159265359f;
        for (uint32_t stack = 0; stack <= stacks; stack++) {
            float v = static_cast<float>(stack) / static_cast<float>(stacks);
            float phi = v * pi;
            float y = radius * std::cos(phi);
            float ringRadius = radius * std::sin(phi);

            for (uint32_t slice = 0; slice <= slices; slice++) {
                float u = static_cast<float>(slice) / static_cast<float>(slices);
                float theta = u * 2.0f * pi;
                float x = ringRadius * std::cos(theta);
                float z = ringRadius * std::sin(theta);
                vertices.push_back(x);
                vertices.push_back(y);
                vertices.push_back(z);
            }
        }

        for (uint32_t stack = 0; stack < stacks; stack++) {
            for (uint32_t slice = 0; slice < slices; slice++) {
                uint32_t first = stack * (slices + 1) + slice;
                uint32_t second = first + slices + 1;

                indices.push_back(first);
                indices.push_back(second);
                indices.push_back(first + 1);

                indices.push_back(second);
                indices.push_back(second + 1);
                indices.push_back(first + 1);
            }
        }
    }

    void uploadSphereMeshBuffers(const std::vector<float>& vertices, const std::vector<uint32_t>& indices) {
        VkDeviceSize vertexBufferSize = sizeof(float) * vertices.size();
        VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();

        GpuBuffer vertexStaging;
        GpuBuffer indexStaging;

        createBuffer(vertexBufferSize,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     vertexStaging,
                     false);
        createBuffer(indexBufferSize,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     indexStaging,
                     false);

        void* mapped = nullptr;
        vkMapMemory(device, vertexStaging.memory, 0, vertexBufferSize, 0, &mapped);
        std::memcpy(mapped, vertices.data(), static_cast<size_t>(vertexBufferSize));
        vkUnmapMemory(device, vertexStaging.memory);

        vkMapMemory(device, indexStaging.memory, 0, indexBufferSize, 0, &mapped);
        std::memcpy(mapped, indices.data(), static_cast<size_t>(indexBufferSize));
        vkUnmapMemory(device, indexStaging.memory);

        createBuffer(vertexBufferSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     sphereVertexBuffer,
                     true);

        createBuffer(indexBufferSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     sphereIndexBuffer,
                     true);

        copyBuffer(vertexStaging.buffer, sphereVertexBuffer.buffer, vertexBufferSize);
        copyBuffer(indexStaging.buffer, sphereIndexBuffer.buffer, indexBufferSize);

        destroyBuffer(vertexStaging);
        destroyBuffer(indexStaging);
    }

    void buildBottomLevelAS(uint32_t vertexCount, uint32_t primitiveCount) {
        VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triangles.vertexData.deviceAddress = getBufferDeviceAddress(sphereVertexBuffer.buffer);
        triangles.vertexStride = sizeof(float) * 3;
        triangles.maxVertex = vertexCount - 1;
        triangles.indexType = VK_INDEX_TYPE_UINT32;
        triangles.indexData.deviceAddress = getBufferDeviceAddress(sphereIndexBuffer.buffer);

        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.triangles = triangles;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        fpGetAccelerationStructureBuildSizesKHR(
            device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo,
            &primitiveCount,
            &sizeInfo);

        createBuffer(sizeInfo.accelerationStructureSize,
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     blasBuffer,
                     true);

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = blasBuffer.buffer;
        createInfo.offset = 0;
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

        if (fpCreateAccelerationStructureKHR(device, &createInfo, nullptr, &blas) != VK_SUCCESS) {
            throw std::runtime_error("failed to create BLAS");
        }

        createBuffer(sizeInfo.buildScratchSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     blasScratchBuffer,
                     true);

        buildInfo.dstAccelerationStructure = blas;
        buildInfo.scratchData.deviceAddress = getBufferDeviceAddress(blasScratchBuffer.buffer);

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = primitiveCount;
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        fpCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &pRangeInfo);
        endSingleTimeCommands(commandBuffer);

        VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        addressInfo.accelerationStructure = blas;
        blasDeviceAddress = fpGetAccelerationStructureDeviceAddressKHR(device, &addressInfo);

        std::cout << "BLAS built successfully (" << primitiveCount << " triangles)" << std::endl;
    }

    void buildTopLevelAS() {
        VkAccelerationStructureInstanceKHR instance{};
        instance.transform.matrix[0][0] = 1.0f;
        instance.transform.matrix[0][1] = 0.0f;
        instance.transform.matrix[0][2] = 0.0f;
        instance.transform.matrix[0][3] = sphereCenter[0];
        instance.transform.matrix[1][0] = 0.0f;
        instance.transform.matrix[1][1] = 1.0f;
        instance.transform.matrix[1][2] = 0.0f;
        instance.transform.matrix[1][3] = sphereCenter[1];
        instance.transform.matrix[2][0] = 0.0f;
        instance.transform.matrix[2][1] = 0.0f;
        instance.transform.matrix[2][2] = 1.0f;
        instance.transform.matrix[2][3] = sphereCenter[2];
        instance.instanceCustomIndex = 0;
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = blasDeviceAddress;

        GpuBuffer instanceStaging;
        createBuffer(sizeof(VkAccelerationStructureInstanceKHR),
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     instanceStaging,
                     false);

        void* mapped = nullptr;
        vkMapMemory(device, instanceStaging.memory, 0, instanceStaging.size, 0, &mapped);
        std::memcpy(mapped, &instance, sizeof(instance));
        vkUnmapMemory(device, instanceStaging.memory);

        createBuffer(sizeof(VkAccelerationStructureInstanceKHR),
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     tlasInstanceBuffer,
                     true);

        copyBuffer(instanceStaging.buffer, tlasInstanceBuffer.buffer, sizeof(VkAccelerationStructureInstanceKHR));
        destroyBuffer(instanceStaging);

        VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
        instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        instancesData.arrayOfPointers = VK_FALSE;
        instancesData.data.deviceAddress = getBufferDeviceAddress(tlasInstanceBuffer.buffer);

        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances = instancesData;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        uint32_t primitiveCount = 1;
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        fpGetAccelerationStructureBuildSizesKHR(
            device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo,
            &primitiveCount,
            &sizeInfo);

        createBuffer(sizeInfo.accelerationStructureSize,
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     tlasBuffer,
                     true);

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = tlasBuffer.buffer;
        createInfo.offset = 0;
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

        if (fpCreateAccelerationStructureKHR(device, &createInfo, nullptr, &tlas) != VK_SUCCESS) {
            throw std::runtime_error("failed to create TLAS");
        }

        createBuffer(sizeInfo.buildScratchSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     tlasScratchBuffer,
                     true);

        buildInfo.dstAccelerationStructure = tlas;
        buildInfo.scratchData.deviceAddress = getBufferDeviceAddress(tlasScratchBuffer.buffer);

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = primitiveCount;
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        fpCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &pRangeInfo);
        endSingleTimeCommands(commandBuffer);

        std::cout << "TLAS built successfully (1 instance)" << std::endl;
    }

    void createRayTracingAccelerationStructures() {
        std::vector<float> vertices;
        std::vector<uint32_t> indices;
        generateSphereMesh(64, 64, 0.5f, vertices, indices);
        sphereVertexCount = static_cast<uint32_t>(vertices.size() / 3);
        sphereTriangleCount = static_cast<uint32_t>(indices.size() / 3);

        uploadSphereMeshBuffers(vertices, indices);
        buildBottomLevelAS(sphereVertexCount, sphereTriangleCount);
        buildTopLevelAS();
    }

    void rebuildTopLevelASForCurrentSphereCenter() {
        if (fpDestroyAccelerationStructureKHR != nullptr && tlas != VK_NULL_HANDLE) {
            fpDestroyAccelerationStructureKHR(device, tlas, nullptr);
            tlas = VK_NULL_HANDLE;
        }

        destroyBuffer(tlasScratchBuffer);
        destroyBuffer(tlasInstanceBuffer);
        destroyBuffer(tlasBuffer);

        buildTopLevelAS();
        if (rtDescriptorSet != VK_NULL_HANDLE) {
            updateRayTracingTlasDescriptor();
        }
    }

    void destroyRayTracingAccelerationStructures() {
        if (fpDestroyAccelerationStructureKHR != nullptr) {
            if (tlas != VK_NULL_HANDLE) {
                fpDestroyAccelerationStructureKHR(device, tlas, nullptr);
                tlas = VK_NULL_HANDLE;
            }
            if (blas != VK_NULL_HANDLE) {
                fpDestroyAccelerationStructureKHR(device, blas, nullptr);
                blas = VK_NULL_HANDLE;
            }
        }

        destroyBuffer(tlasScratchBuffer);
        destroyBuffer(blasScratchBuffer);
        destroyBuffer(tlasInstanceBuffer);
        destroyBuffer(tlasBuffer);
        destroyBuffer(blasBuffer);
        destroyBuffer(sphereIndexBuffer);
        destroyBuffer(sphereVertexBuffer);
        blasDeviceAddress = 0;
        sphereVertexCount = 0;
    }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw std::runtime_error("failed to find suitable memory type!");
    }

    VkCommandBuffer beginSingleTimeCommands() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        return commandBuffer;
    }

    void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    void clearAccumulationImages() {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barriers[2]{};
        for (int i = 0; i < 2; i++) {
            barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[i].oldLayout = accumulationImagesInitialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].image = accumulationImages[i];
            barriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barriers[i].subresourceRange.baseMipLevel = 0;
            barriers[i].subresourceRange.levelCount = 1;
            barriers[i].subresourceRange.baseArrayLayer = 0;
            barriers[i].subresourceRange.layerCount = 1;
            barriers[i].srcAccessMask = 0;
            barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        }

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            2, barriers);

        VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 0.0f}};
        for (int i = 0; i < 2; i++) {
            VkImageSubresourceRange range{};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel = 0;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = 1;
            vkCmdClearColorImage(commandBuffer, accumulationImages[i], VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &range);
        }

        endSingleTimeCommands(commandBuffer);
        accumulationImagesInitialized = true;
    }

    void resetAccumulation() {
        frameCount = 0;
    }

    void createAccumulationResources() {
        VkFormat accumulationFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

        for (int i = 0; i < 2; i++) {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = swapChainExtent.width;
            imageInfo.extent.height = swapChainExtent.height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = accumulationFormat;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateImage(device, &imageInfo, nullptr, &accumulationImages[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create accumulation image!");
            }

            VkMemoryRequirements memRequirements;
            vkGetImageMemoryRequirements(device, accumulationImages[i], &memRequirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            if (vkAllocateMemory(device, &allocInfo, nullptr, &accumulationImageMemories[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to allocate accumulation image memory!");
            }

            vkBindImageMemory(device, accumulationImages[i], accumulationImageMemories[i], 0);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = accumulationImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = accumulationFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device, &viewInfo, nullptr, &accumulationImageViews[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create accumulation image view!");
            }
        }

        resetAccumulation();
    }

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = 0;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin recording command buffer!");
        }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipelineLayout, 0, 1, &rtDescriptorSet, 0, nullptr);

        std::array<float, 3> forward = getCameraForward();
        std::array<float, 3> worldUp = {0.0f, 1.0f, 0.0f};
        std::array<float, 3> right = normalizeVec3(crossVec3(forward, worldUp));
        std::array<float, 3> up = normalizeVec3(crossVec3(right, forward));

        CameraPushConstants push{};
        push.origin = cameraPosition;
        push.pad0 = 0.0f;
        push.u = right;
        push.pad1 = 0.0f;
        push.v = up;
        push.pad2 = 0.0f;
        push.w = {-forward[0], -forward[1], -forward[2]};
        push.pad3 = 0.0f;
        push.sphereCenter = sphereCenter;
        push.sphereFuzz = sphereFuzz;
        push.materialType = materialType;
        push.sphereIor = sphereIor;
        push.hollowShell = hollowShell;
        push.fov = cameraFov;
        push.aspect = static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height);
        push.width = static_cast<float>(swapChainExtent.width);
        push.height = static_cast<float>(swapChainExtent.height);
        push.frame = static_cast<float>(frameCount);

        vkCmdPushConstants(commandBuffer,
                   rtPipelineLayout,
                   VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
                   0,
                   sizeof(CameraPushConstants),
                   &push);

        fpCmdTraceRaysKHR(commandBuffer,
                          &rtRaygenRegion,
                          &rtMissRegion,
                          &rtHitRegion,
                          &rtCallableRegion,
                          swapChainExtent.width,
                          swapChainExtent.height,
                          1);

        VkImageMemoryBarrier rtToTransfer{};
        rtToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        rtToTransfer.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        rtToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        rtToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        rtToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        rtToTransfer.image = rtOutputImage;
        rtToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        rtToTransfer.subresourceRange.baseMipLevel = 0;
        rtToTransfer.subresourceRange.levelCount = 1;
        rtToTransfer.subresourceRange.baseArrayLayer = 0;
        rtToTransfer.subresourceRange.layerCount = 1;
        rtToTransfer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        rtToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        VkImageMemoryBarrier swapToTransfer{};
        swapToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        swapToTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        swapToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        swapToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapToTransfer.image = swapChainImages[imageIndex];
        swapToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        swapToTransfer.subresourceRange.baseMipLevel = 0;
        swapToTransfer.subresourceRange.levelCount = 1;
        swapToTransfer.subresourceRange.baseArrayLayer = 0;
        swapToTransfer.subresourceRange.layerCount = 1;
        swapToTransfer.srcAccessMask = 0;
        swapToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        std::array<VkImageMemoryBarrier, 2> preBlitBarriers = {rtToTransfer, swapToTransfer};
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             static_cast<uint32_t>(preBlitBarriers.size()),
                             preBlitBarriers.data());

        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = 0;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {static_cast<int32_t>(swapChainExtent.width), static_cast<int32_t>(swapChainExtent.height), 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = 0;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {static_cast<int32_t>(swapChainExtent.width), static_cast<int32_t>(swapChainExtent.height), 1};

        vkCmdBlitImage(commandBuffer,
                       rtOutputImage,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       swapChainImages[imageIndex],
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1,
                       &blit,
                       VK_FILTER_NEAREST);

        VkImageMemoryBarrier rtBackToGeneral{};
        rtBackToGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        rtBackToGeneral.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        rtBackToGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        rtBackToGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        rtBackToGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        rtBackToGeneral.image = rtOutputImage;
        rtBackToGeneral.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        rtBackToGeneral.subresourceRange.baseMipLevel = 0;
        rtBackToGeneral.subresourceRange.levelCount = 1;
        rtBackToGeneral.subresourceRange.baseArrayLayer = 0;
        rtBackToGeneral.subresourceRange.layerCount = 1;
        rtBackToGeneral.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        rtBackToGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

        VkImageMemoryBarrier swapBackToPresent{};
        swapBackToPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        swapBackToPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        swapBackToPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        swapBackToPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapBackToPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapBackToPresent.image = swapChainImages[imageIndex];
        swapBackToPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        swapBackToPresent.subresourceRange.baseMipLevel = 0;
        swapBackToPresent.subresourceRange.levelCount = 1;
        swapBackToPresent.subresourceRange.baseArrayLayer = 0;
        swapBackToPresent.subresourceRange.layerCount = 1;
        swapBackToPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        swapBackToPresent.dstAccessMask = 0;

        std::array<VkImageMemoryBarrier, 2> postBlitBarriers = {rtBackToGeneral, swapBackToPresent};
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             static_cast<uint32_t>(postBlitBarriers.size()),
                             postBlitBarriers.data());

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to record command buffer!");
        }
    }

    void createCommandBuffers() {
        commandBuffers.resize(swapChainImages.size());

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

        if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffers!");
        }
    }

    void createSyncObjects() {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS) {
            throw std::runtime_error("failed to create semaphores!");
        }
    }

    void drawFrame() {
        uint32_t imageIndex;
        VkResult acquireResult = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("failed to acquire swap chain image! VkResult=" + std::to_string(acquireResult));
        }

        vkQueueWaitIdle(graphicsQueue);

        vkResetCommandBuffer(commandBuffers[imageIndex], 0);
        recordCommandBuffer(commandBuffers[imageIndex], imageIndex);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

        VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        VkResult submitResult = vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        if (submitResult != VK_SUCCESS) {
            throw std::runtime_error("failed to submit draw command buffer! VkResult=" + std::to_string(submitResult));
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = {swapChain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;
        presentInfo.pResults = nullptr;

        VkResult presentResult = vkQueuePresentKHR(presentQueue, &presentInfo);
        if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("failed to present swap chain image! VkResult=" + std::to_string(presentResult));
        }
        vkQueueWaitIdle(presentQueue);

        frameCount++;
    }
};

int main() {
    HelloVulkan app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
