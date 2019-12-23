
#include "pch.h"

void DebugPrint(const char* fmt, ...);

#define BASSERT(expr) ((void)((!!(expr)) || \
        ((DebugPrint("Error: %s, %s:%d\n", #expr, __FILE__, __LINE__),1) \
         && (__debugbreak(),0)) ))




#define VK_CHECK(_error) do { \
    if (_error != VK_SUCCESS) { \
        BASSERT(!#_error); \
    } \
} while (false)

struct ImageVulkan
{
    VkFormat format;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView imageView;
    VkSampler sampler;
};

struct BufferVulkan
{
    VkDeviceSize size;
    VkBuffer buffer;
    VkDeviceMemory memory;
};

struct RTAccelerationStructure
{
    VkDeviceMemory memory;
    VkAccelerationStructureInfoNV accelerationStructureInfo;
    VkAccelerationStructureNV accelerationStructure;
    uint64_t handle;
};

struct Mesh
{
    uint32_t numVertices;
    uint32_t numFaces;

    BufferVulkan positions;
    BufferVulkan indices;

    RTAccelerationStructure blas;
};

struct Scene
{
    // BONI TODO: move meshes here

    RTAccelerationStructure tlas;
};

struct VkGeometryInstance
{
    float transform[12];
    uint32_t instanceId : 24;
    uint32_t mask : 8;
    uint32_t instanceOffset : 24;
    uint32_t flags : 8;
    uint64_t accelerationStructureHandle;
};

const char* kAppName = "test vulkan app";
bool kEnableValidation = true;

uint32_t kWindowWidth = 800;
uint32_t kWindowHeight = 600;

GLFWwindow* g_window;
VkInstance g_instance;
VkPhysicalDevice g_physicalDevice;
VkDevice g_device;
uint32_t g_queueIndex;
VkQueue g_queue;
VkPhysicalDeviceRayTracingPropertiesNV g_rtProps;
VkSurfaceKHR g_surface;
VkSurfaceFormatKHR g_surfaceFormat;
VkSwapchainKHR g_swapchain;
std::vector<VkImage> g_swapchainImages;
std::vector<VkImageView> g_swapchainImageViews;
std::vector<VkFence> g_waitForFrameFences;
VkCommandPool g_commandPool;
VkPhysicalDeviceMemoryProperties g_physicalDeviceMemoryProperties;
std::vector<VkCommandBuffer> g_commandBuffers;
VkSemaphore g_semaphoreAcquired;
VkSemaphore g_semaphoreRenderFinished;
VkDebugReportCallbackEXT g_debugCallback;
VkShaderModule g_raygenShader;
VkShaderModule g_chitShader;
VkShaderModule g_missShader;

ImageVulkan g_offscreenImage;

Mesh g_mesh;
BufferVulkan g_instancesBuffer;
Scene g_scene;

struct CameraUniformData
{
    glm::mat4 viewInverse;
    glm::mat4 projInverse;
};

struct CameraData
{
    glm::mat4 view;
    glm::mat4 perspective;

    CameraUniformData cameraUniformData;
    BufferVulkan buffer;
};

CameraData g_camera;

VkDescriptorSetLayout g_descriptorSetLayout;
VkPipelineLayout g_pipelineLayout;
VkPipeline g_rtPipeline;

BufferVulkan g_sbtBuffer;

VkDescriptorPool g_descriptorPool;
VkDescriptorSet g_descriptorSet;











void DebugPrint(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    size_t len = _vscprintf(fmt, args);
    std::vector<char> buff(len);
    vsnprintf_s(buff.data(), len, _TRUNCATE, fmt, args);
    OutputDebugStringA(buff.data());
    va_end(args);
}



VKAPI_ATTR VkBool32 VKAPI_CALL debugReportCallback(VkDebugReportFlagsEXT flags,
        VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location,
        int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData)
{
    if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
    {
        DebugPrint("Validation Error: [%s] %s", pLayerPrefix, pMessage);
        BASSERT(!"validation error");

    }
    else if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
    {
        DebugPrint("Validation Warning: [%s] %s", pLayerPrefix, pMessage);
        BASSERT(!"validation error");
    }
    else
    {
        DebugPrint("Validation Performance Warning: [%s] %s", pLayerPrefix, pMessage);
    }

    return VK_FALSE;
}


VkPresentModeKHR getPresentMode(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_physicalDevice, g_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_physicalDevice, g_surface, &presentModeCount, presentModes.data());

    for (auto& mode : presentModes)
        if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
            return mode;

    for (auto& mode : presentModes)
        if (mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR)
            return mode;

    return VK_PRESENT_MODE_FIFO_KHR;
}

uint32_t getMemoryType(VkMemoryRequirements& memoryRequirements, VkMemoryPropertyFlags memoryProperties)
{
    uint32_t result = 0;

    for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++)
    {
        if (memoryRequirements.memoryTypeBits & (1 << i))
        {
            if ((g_physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & memoryProperties)
                    == memoryProperties)
            {
                result = i;
                break;
            }
        }
    }

    return result;
}

bool loadShader(const char* filename, VkShaderModule* pShaderModule)
{
    bool result = false;

    std::ifstream file(filename, std::ios::in | std::ios::binary);
    if (file)
    {
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> bytecode(fileSize);
        bytecode.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        ci.codeSize = fileSize;
        ci.pCode = (uint32_t*)(bytecode.data());
        ci.flags = 0;

        VkResult error = vkCreateShaderModule(g_device, &ci, nullptr, pShaderModule);
        VK_CHECK(error);

        result = error == VK_SUCCESS;
    }

    if (!result)
    {
        DebugPrint("Error loading: %s\n", filename);
        BASSERT(0);
    }

    return result;
}

// ------------

bool initVulkan()
{
    if (!glfwInit())
        return false;

    if (!glfwVulkanSupported())
        return false;

    if (volkInitialize() != VK_SUCCESS)
        return false;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    g_window = glfwCreateWindow(kWindowWidth, kWindowHeight, kAppName, nullptr, nullptr);

    if (!g_window)
        return false;

    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = kAppName;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "VulkanApp";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };

    std::vector<const char*> layers;

    if (kEnableValidation)
    {
        extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        layers.push_back("VK_LAYER_LUNARG_standard_validation");
    }

    VkInstanceCreateInfo instInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instInfo.flags = 0;
    instInfo.pApplicationInfo = &appInfo;
    instInfo.enabledExtensionCount = uint32_t(extensions.size());
    instInfo.ppEnabledExtensionNames = extensions.data();
    instInfo.enabledLayerCount = uint32_t(layers.size());
    instInfo.ppEnabledLayerNames = layers.data();

    VkResult res = vkCreateInstance(&instInfo, nullptr, &g_instance);
    if (res != VK_SUCCESS)
    {
        assert(0);
        return false;
    }

    volkLoadInstance(g_instance);

    {
        VkDebugReportCallbackCreateInfoEXT callbackCreateInfo = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT };
        callbackCreateInfo.flags =
            VK_DEBUG_REPORT_ERROR_BIT_EXT |
            VK_DEBUG_REPORT_WARNING_BIT_EXT |
            VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        callbackCreateInfo.pfnCallback = debugReportCallback;

        VK_CHECK(vkCreateDebugReportCallbackEXT(g_instance, &callbackCreateInfo, nullptr, &g_debugCallback));
    }

    uint32_t numPhysicalDevices = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(g_instance, &numPhysicalDevices, nullptr));
    std::vector<VkPhysicalDevice> physicalDevices(numPhysicalDevices);
    VK_CHECK(vkEnumeratePhysicalDevices(g_instance, &numPhysicalDevices, physicalDevices.data()));
    g_physicalDevice = physicalDevices[0];

    uint32_t queueFamilyPropertyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(g_physicalDevice, &queueFamilyPropertyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyPropertyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(g_physicalDevice, &queueFamilyPropertyCount, queueFamilyProperties.data());

    g_queueIndex = ~0u;
    for (uint32_t i = 0; i < queueFamilyPropertyCount; i++)
    {
        if (queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            g_queueIndex = i;
            break;
        }
    }

    if (g_queueIndex == ~0u)
        return false;

    const float queuePriorites[] = { 1.f };
    VkDeviceQueueCreateInfo deviceQueueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    deviceQueueCreateInfo.flags = 0;
    deviceQueueCreateInfo.queueFamilyIndex = g_queueIndex;
    deviceQueueCreateInfo.queueCount = 1;
    deviceQueueCreateInfo.pQueuePriorities = queuePriorites;

    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_NV_RAY_TRACING_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
    };

    VkPhysicalDeviceDescriptorIndexingFeaturesEXT descriptorIndexing = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT };

    VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    features2.pNext = &descriptorIndexing;

    vkGetPhysicalDeviceFeatures2(g_physicalDevice, &features2);

    VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    deviceCreateInfo.pNext = &features2;
    deviceCreateInfo.flags = 0;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &deviceQueueCreateInfo;
    deviceCreateInfo.enabledLayerCount = 0;
    deviceCreateInfo.ppEnabledLayerNames = nullptr;
    deviceCreateInfo.enabledExtensionCount = uint32_t(deviceExtensions.size());
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
    deviceCreateInfo.pEnabledFeatures = nullptr;

    VK_CHECK(vkCreateDevice(g_physicalDevice, &deviceCreateInfo, nullptr, &g_device));

    vkGetDeviceQueue(g_device, g_queueIndex, 0, &g_queue);

    g_rtProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV };
    g_rtProps.maxRecursionDepth = 0;
    g_rtProps.shaderGroupHandleSize = 0;

    VkPhysicalDeviceProperties2 deviceProperties2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    deviceProperties2.pNext = &g_rtProps;

    vkGetPhysicalDeviceProperties2(g_physicalDevice, &deviceProperties2);

    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    surfaceCreateInfo.hinstance = GetModuleHandle(nullptr);
    surfaceCreateInfo.hwnd = glfwGetWin32Window(g_window);

    VK_CHECK(vkCreateWin32SurfaceKHR(g_instance, &surfaceCreateInfo, nullptr, &g_surface));

    VkBool32 supportPresent = VK_FALSE;
    VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(g_physicalDevice, g_queueIndex, g_surface, &supportPresent));

    uint32_t surfaceFormatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_physicalDevice, g_surface, &surfaceFormatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_physicalDevice, g_surface, &surfaceFormatCount, surfaceFormats.data());

    // VK_FORMAT_B8G8R8A8_UNORM
    bool surfaceFound = false;
    for (auto& f : surfaceFormats)
    {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM)
        {
            g_surfaceFormat = f;
            surfaceFound = true;
            break;
        }
    }

    if (!surfaceFound)
        g_surfaceFormat = surfaceFormats[0];

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_physicalDevice, g_surface, &surfaceCapabilities));

    VkPresentModeKHR presentMode = getPresentMode(g_physicalDevice, g_surface);

    VkSwapchainKHR prevSwapchain = g_swapchain;

    VkSwapchainCreateInfoKHR swapchainCreateInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    swapchainCreateInfo.flags = 0;
    swapchainCreateInfo.surface = g_surface;
    swapchainCreateInfo.minImageCount = surfaceCapabilities.minImageCount;
    swapchainCreateInfo.imageFormat = g_surfaceFormat.format;
    swapchainCreateInfo.imageColorSpace = g_surfaceFormat.colorSpace;
    swapchainCreateInfo.imageExtent.width = kWindowWidth;
    swapchainCreateInfo.imageExtent.height = kWindowHeight;
    swapchainCreateInfo.imageArrayLayers = 1;
    swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCreateInfo.queueFamilyIndexCount = 0;
    swapchainCreateInfo.pQueueFamilyIndices = nullptr;
    swapchainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainCreateInfo.presentMode = presentMode;
    swapchainCreateInfo.clipped = VK_TRUE;
    swapchainCreateInfo.oldSwapchain = prevSwapchain;

    VK_CHECK(vkCreateSwapchainKHR(g_device, &swapchainCreateInfo, nullptr, &g_swapchain));

    if (prevSwapchain)
    {
        for (auto& view : g_swapchainImageViews)
            vkDestroyImageView(g_device, view, nullptr);
        g_swapchainImageViews.clear();

        // BONI TODO: need to destroy images?

        vkDestroySwapchainKHR(g_device, prevSwapchain, nullptr);
    }

    uint32_t imageCount;
    vkGetSwapchainImagesKHR(g_device, g_swapchain, &imageCount, nullptr);
    g_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(g_device, g_swapchain, &imageCount, g_swapchainImages.data());

    g_swapchainImageViews.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++)
    {
        VkImageViewCreateInfo imageViewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        imageViewCreateInfo.format = g_surfaceFormat.format;
        imageViewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.image = g_swapchainImages[i];
        imageViewCreateInfo.flags = 0;
        imageViewCreateInfo.components = {};

        VK_CHECK(vkCreateImageView(g_device, &imageViewCreateInfo, nullptr, &g_swapchainImageViews[i]));
    }

    VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    g_waitForFrameFences.resize(g_swapchainImages.size());
    for (auto& fence : g_waitForFrameFences)
        vkCreateFence(g_device, &fenceCreateInfo, nullptr, &fence);

    VkCommandPoolCreateInfo commandPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCreateInfo.queueFamilyIndex = g_queueIndex;

    VK_CHECK(vkCreateCommandPool(g_device, &commandPoolCreateInfo, nullptr, &g_commandPool));

    vkGetPhysicalDeviceMemoryProperties(g_physicalDevice, &g_physicalDeviceMemoryProperties);

    {
        VkExtent3D imageExtent = { kWindowWidth, kWindowHeight, 1 };

        auto& img = g_offscreenImage;
        img = {};

        img.format = g_surfaceFormat.format;

        VkImageCreateInfo imageCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageCreateInfo.flags = 0;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = img.format;
        imageCreateInfo.extent = imageExtent;
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageCreateInfo.queueFamilyIndexCount = 0;
        imageCreateInfo.pQueueFamilyIndices = nullptr;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VK_CHECK(vkCreateImage(g_device, &imageCreateInfo, nullptr, &img.image));

        VkMemoryRequirements memoryRequirements;
        vkGetImageMemoryRequirements(g_device, img.image, &memoryRequirements);

        VkMemoryAllocateInfo memoryAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        memoryAllocateInfo.allocationSize = memoryRequirements.size;
        memoryAllocateInfo.memoryTypeIndex = getMemoryType(memoryRequirements,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK(vkAllocateMemory(g_device, &memoryAllocateInfo, nullptr, &img.memory));

        VK_CHECK(vkBindImageMemory(g_device, img.image, img.memory, 0));

        VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        VkImageViewCreateInfo imageViewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = img.format;
        imageViewCreateInfo.subresourceRange = range;
        imageViewCreateInfo.image = img.image;
        imageViewCreateInfo.flags = 0;
        imageViewCreateInfo.components = {
            VK_COMPONENT_SWIZZLE_R,
            VK_COMPONENT_SWIZZLE_G,
            VK_COMPONENT_SWIZZLE_B,
            VK_COMPONENT_SWIZZLE_A
        };

        VK_CHECK(vkCreateImageView(g_device, &imageViewCreateInfo, nullptr, &img.imageView));
    }

    {
        g_commandBuffers.resize(g_swapchainImages.size());

        VkCommandBufferAllocateInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        info.commandPool = g_commandPool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = uint32_t(g_commandBuffers.size());

        VK_CHECK(vkAllocateCommandBuffers(g_device, &info, g_commandBuffers.data()));
    }

    {
        VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        info.flags = 0;

        VK_CHECK(vkCreateSemaphore(g_device, &info, nullptr, &g_semaphoreAcquired));
        VK_CHECK(vkCreateSemaphore(g_device, &info, nullptr, &g_semaphoreRenderFinished));
    }

    return true;
}

void createScene()
{
    float positions[][3] = {
        {1.f, 1.f, 0.f},
        {-1.f, 1.f, 0.f},
        {0.f, -1.f, 0.f}
    };
    uint32_t indices[] = { 0, 1, 2 };

    {
        VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferCreateInfo.flags = 0;
        bufferCreateInfo.size = sizeof(positions);
        bufferCreateInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        auto& buff = g_mesh.positions;

        buff.size = sizeof(positions);

        VK_CHECK(vkCreateBuffer(g_device, &bufferCreateInfo, nullptr, &buff.buffer));

        VkMemoryRequirements memoryRequirements;
        vkGetBufferMemoryRequirements(g_device, buff.buffer, &memoryRequirements);

        VkMemoryAllocateInfo memAllocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        memAllocInfo.allocationSize = memoryRequirements.size;
        memAllocInfo.memoryTypeIndex = getMemoryType(memoryRequirements,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VK_CHECK(vkAllocateMemory(g_device, &memAllocInfo, nullptr, &buff.memory));

        VK_CHECK(vkBindBufferMemory(g_device, buff.buffer, buff.memory, 0));

        void* mem = nullptr;
        VkDeviceSize size = buff.size;
        int offset = 0;
        VK_CHECK(vkMapMemory(g_device, buff.memory, offset, size, 0, &mem));

        memcpy(mem, positions, size);

        vkUnmapMemory(g_device, buff.memory);
    }

    {
        VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferCreateInfo.flags = 0;
        bufferCreateInfo.size = sizeof(indices);
        bufferCreateInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        auto& buff = g_mesh.indices;

        buff.size = sizeof(indices);

        VK_CHECK(vkCreateBuffer(g_device, &bufferCreateInfo, nullptr, &buff.buffer));

        VkMemoryRequirements memoryRequirements;
        vkGetBufferMemoryRequirements(g_device, buff.buffer, &memoryRequirements);

        VkMemoryAllocateInfo memAllocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        memAllocInfo.allocationSize = memoryRequirements.size;
        memAllocInfo.memoryTypeIndex = getMemoryType(memoryRequirements,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VK_CHECK(vkAllocateMemory(g_device, &memAllocInfo, nullptr, &buff.memory));

        VK_CHECK(vkBindBufferMemory(g_device, buff.buffer, buff.memory, 0));

        void* mem = nullptr;
        VkDeviceSize size = buff.size;
        int offset = 0;
        VK_CHECK(vkMapMemory(g_device, buff.memory, offset, size, 0, &mem));

        memcpy(mem, indices, size);

        vkUnmapMemory(g_device, buff.memory);
    }

    g_mesh.numVertices = 3;
    g_mesh.numFaces = 1;

    VkGeometryNV geometry = { VK_STRUCTURE_TYPE_GEOMETRY_NV };
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_NV;
    geometry.geometry.triangles = { VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV };
    geometry.geometry.triangles.vertexData = g_mesh.positions.buffer;
    geometry.geometry.triangles.vertexOffset = 0;
    geometry.geometry.triangles.vertexCount = g_mesh.numVertices;
    geometry.geometry.triangles.vertexStride = sizeof(float) * 3;
    geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geometry.geometry.triangles.indexData = g_mesh.indices.buffer;
    geometry.geometry.triangles.indexOffset = 0;
    geometry.geometry.triangles.indexCount = g_mesh.numFaces * 3;
    geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32; // BONI TODO: switch to uint16
    geometry.geometry.triangles.transformData = VK_NULL_HANDLE;
    geometry.geometry.triangles.transformOffset = 0;
    geometry.geometry.aabbs = { VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV };
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV;

    const float transform[12] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
    };

    {
        VkAccelerationStructureInfoNV& accelerationStructureInfo = g_mesh.blas.accelerationStructureInfo;
        accelerationStructureInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV };
        accelerationStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
        accelerationStructureInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;
        accelerationStructureInfo.geometryCount = 1;
        accelerationStructureInfo.instanceCount = 0;
        accelerationStructureInfo.pGeometries = &geometry;

        VkAccelerationStructureCreateInfoNV accelerationStructureCreateInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV };
        accelerationStructureCreateInfo.info = accelerationStructureInfo;

        VK_CHECK(vkCreateAccelerationStructureNV(g_device, &accelerationStructureCreateInfo, nullptr, &g_mesh.blas.accelerationStructure));

        VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
        memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
        memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
        memoryRequirementsInfo.accelerationStructure = g_mesh.blas.accelerationStructure;

        VkMemoryRequirements2 memoryRequirements;
        vkGetAccelerationStructureMemoryRequirementsNV(g_device, &memoryRequirementsInfo, &memoryRequirements);

        VkMemoryAllocateInfo memoryAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        memoryAllocateInfo.allocationSize = memoryRequirements.memoryRequirements.size;
        memoryAllocateInfo.memoryTypeIndex = getMemoryType(memoryRequirements.memoryRequirements,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK(vkAllocateMemory(g_device, &memoryAllocateInfo, nullptr, &g_mesh.blas.memory));

        VkBindAccelerationStructureMemoryInfoNV bindInfo = { VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV };
        bindInfo.accelerationStructure = g_mesh.blas.accelerationStructure;
        bindInfo.memory = g_mesh.blas.memory;
        bindInfo.memoryOffset = 0;

        vkBindAccelerationStructureMemoryNV(g_device, 1, &bindInfo);

        VK_CHECK(vkGetAccelerationStructureHandleNV(g_device, g_mesh.blas.accelerationStructure, sizeof(uint64_t), &g_mesh.blas.handle));
    }

    VkGeometryInstance instance;
    memcpy(instance.transform, transform, sizeof(transform));
    instance.instanceId = 0;
    instance.mask = 0xFF;
    instance.instanceOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;
    instance.accelerationStructureHandle = g_mesh.blas.handle;

    
    {
        auto& buff = g_instancesBuffer;

        VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferCreateInfo.flags = 0;
        bufferCreateInfo.size = sizeof(VkGeometryInstance);
        bufferCreateInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        buff.size = sizeof(VkGeometryInstance);

        VK_CHECK(vkCreateBuffer(g_device, &bufferCreateInfo, nullptr, &buff.buffer));

        VkMemoryRequirements memoryRequirements;
        vkGetBufferMemoryRequirements(g_device, buff.buffer, &memoryRequirements);

        VkMemoryAllocateInfo memAllocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        memAllocInfo.allocationSize = memoryRequirements.size;
        memAllocInfo.memoryTypeIndex = getMemoryType(memoryRequirements,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VK_CHECK(vkAllocateMemory(g_device, &memAllocInfo, nullptr, &buff.memory));

        VK_CHECK(vkBindBufferMemory(g_device, buff.buffer, buff.memory, 0));

        void* mem = nullptr;
        VkDeviceSize size = buff.size;
        int offset = 0;
        VK_CHECK(vkMapMemory(g_device, buff.memory, offset, size, 0, &mem));

        memcpy(mem, &instance, size);

        vkUnmapMemory(g_device, buff.memory);
    }

    {
        RTAccelerationStructure& as = g_scene.tlas;

        VkAccelerationStructureInfoNV& accelerationStructureInfo = as.accelerationStructureInfo;
        accelerationStructureInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV };
        accelerationStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
        accelerationStructureInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;
        accelerationStructureInfo.geometryCount = 0;
        accelerationStructureInfo.instanceCount = 1;
        accelerationStructureInfo.pGeometries = nullptr;

        VkAccelerationStructureCreateInfoNV accelerationStructureCreateInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV };
        accelerationStructureCreateInfo.info = accelerationStructureInfo;

        VK_CHECK(vkCreateAccelerationStructureNV(g_device, &accelerationStructureCreateInfo, nullptr, &as.accelerationStructure));

        VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
        memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
        memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
        memoryRequirementsInfo.accelerationStructure = as.accelerationStructure;

        VkMemoryRequirements2 memoryRequirements;
        vkGetAccelerationStructureMemoryRequirementsNV(g_device, &memoryRequirementsInfo, &memoryRequirements);

        VkMemoryAllocateInfo memoryAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        memoryAllocateInfo.allocationSize = memoryRequirements.memoryRequirements.size;
        memoryAllocateInfo.memoryTypeIndex = getMemoryType(memoryRequirements.memoryRequirements,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK(vkAllocateMemory(g_device, &memoryAllocateInfo, nullptr, &as.memory));

        VkBindAccelerationStructureMemoryInfoNV bindInfo = { VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV };
        bindInfo.accelerationStructure = as.accelerationStructure;
        bindInfo.memory = as.memory;
        bindInfo.memoryOffset = 0;

        vkBindAccelerationStructureMemoryNV(g_device, 1, &bindInfo);

        VK_CHECK(vkGetAccelerationStructureHandleNV(g_device, g_mesh.blas.accelerationStructure, sizeof(uint64_t), &as.handle));
    }

    // Build bottom/top AS
    
    VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
    memoryRequirementsInfo.sType = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV };
    memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;

    VkDeviceSize maxBlasSize = 0;
    memoryRequirementsInfo.accelerationStructure = g_mesh.blas.accelerationStructure;

    VkMemoryRequirements2 memReqBlas;
    vkGetAccelerationStructureMemoryRequirementsNV(g_device, &memoryRequirementsInfo, &memReqBlas);

    maxBlasSize = memReqBlas.memoryRequirements.size;

    VkMemoryRequirements2 memReqTlas;
    memoryRequirementsInfo.accelerationStructure = g_scene.tlas.accelerationStructure;
    vkGetAccelerationStructureMemoryRequirementsNV(g_device, &memoryRequirementsInfo, &memReqTlas);

    VkDeviceSize scratchBufferSize = std::max(maxBlasSize, memReqTlas.memoryRequirements.size);

    BufferVulkan scratchBuffer;
    {
        auto& buff = scratchBuffer;

        VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferCreateInfo.flags = 0;
        bufferCreateInfo.size = scratchBufferSize;
        bufferCreateInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        buff.size = scratchBufferSize;

        VK_CHECK(vkCreateBuffer(g_device, &bufferCreateInfo, nullptr, &buff.buffer));

        VkMemoryRequirements memoryRequirements;
        vkGetBufferMemoryRequirements(g_device, buff.buffer, &memoryRequirements);

        VkMemoryAllocateInfo memAllocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        memAllocInfo.allocationSize = memoryRequirements.size;
        memAllocInfo.memoryTypeIndex = getMemoryType(memoryRequirements,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK(vkAllocateMemory(g_device, &memAllocInfo, nullptr, &buff.memory));

        VK_CHECK(vkBindBufferMemory(g_device, buff.buffer, buff.memory, 0));
    }

    VkCommandBufferAllocateInfo commandBufferAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    commandBufferAllocateInfo.commandPool = g_commandPool;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(g_device, &commandBufferAllocateInfo, &cmdBuffer));

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    VkMemoryBarrier memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
    memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;

    g_mesh.blas.accelerationStructureInfo.instanceCount = 0;
    g_mesh.blas.accelerationStructureInfo.geometryCount = 1;
    g_mesh.blas.accelerationStructureInfo.pGeometries = &geometry;
    vkCmdBuildAccelerationStructureNV(cmdBuffer, &g_mesh.blas.accelerationStructureInfo,
            VK_NULL_HANDLE, 0, VK_FALSE, g_mesh.blas.accelerationStructure,
            VK_NULL_HANDLE, scratchBuffer.buffer, 0);

    vkCmdPipelineBarrier(cmdBuffer,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV,
            0, 1, &memoryBarrier, 0, 0, 0, 0);

    g_scene.tlas.accelerationStructureInfo.instanceCount = 1;
    g_scene.tlas.accelerationStructureInfo.geometryCount = 0;
    g_scene.tlas.accelerationStructureInfo.pGeometries = nullptr;
    vkCmdBuildAccelerationStructureNV(cmdBuffer, &g_scene.tlas.accelerationStructureInfo,
            g_instancesBuffer.buffer, 0, VK_FALSE, g_scene.tlas.accelerationStructure,
            VK_NULL_HANDLE, scratchBuffer.buffer, 0);

    vkCmdPipelineBarrier(cmdBuffer,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV,
            0, 1, &memoryBarrier, 0, 0, 0, 0);

    vkEndCommandBuffer(cmdBuffer);

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.waitSemaphoreCount = 0;
    submitInfo.signalSemaphoreCount = 0;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    VK_CHECK(vkQueueSubmit(g_queue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(g_queue));

    vkFreeCommandBuffers(g_device, g_commandPool, 1, &cmdBuffer);

    {
        vkDestroyBuffer(g_device, scratchBuffer.buffer, nullptr);
        vkFreeMemory(g_device, scratchBuffer.memory, nullptr);
        scratchBuffer = {};
    }
}

void setupCamera()
{
    {
        auto& buff = g_camera.buffer;

        VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferCreateInfo.flags = 0;
        bufferCreateInfo.size = sizeof(CameraUniformData);
        bufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
            VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        buff.size = sizeof(CameraUniformData);

        VK_CHECK(vkCreateBuffer(g_device, &bufferCreateInfo, nullptr, &buff.buffer));

        VkMemoryRequirements memoryRequirements;
        vkGetBufferMemoryRequirements(g_device, buff.buffer, &memoryRequirements);

        VkMemoryAllocateInfo memAllocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        memAllocInfo.allocationSize = memoryRequirements.size;
        memAllocInfo.memoryTypeIndex = getMemoryType(memoryRequirements,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VK_CHECK(vkAllocateMemory(g_device, &memAllocInfo, nullptr, &buff.memory));

        VK_CHECK(vkBindBufferMemory(g_device, buff.buffer, buff.memory, 0));

        /*
        void* mem = nullptr;
        VkDeviceSize size = buff.size;
        int offset = 0;
        VK_CHECK(vkMapMemory(g_device, buff.memory, offset, size, 0, &mem));

        memcpy(mem, &instance, size);

        vkUnmapMemory(g_device, buff.memory);
        */
    }

    g_camera.perspective = glm::perspective(glm::radians(60.f),
            float(kWindowWidth) / kWindowHeight, 0.1f, 512.f);
    g_camera.view = glm::translate(glm::mat4(1.f), glm::vec3(0, 0, -2.5f));
}

void createDescriptorSetLayouts()
{
    // Set 0:
    //   Binding 0 -> AS
    //   Binding 1 -> output image
    //   Binding 2 -> camera data

    VkDescriptorSetLayoutBinding asLayoutBinding = {};
    asLayoutBinding.binding = 0;
    asLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
    asLayoutBinding.descriptorCount = 1;
    asLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;

    VkDescriptorSetLayoutBinding outputImageLayoutBinding = {};
    outputImageLayoutBinding.binding = 1;
    outputImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputImageLayoutBinding.descriptorCount = 1;
    outputImageLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;

    VkDescriptorSetLayoutBinding cameraDataLayoutBinding = {};
    cameraDataLayoutBinding.binding = 2;
    cameraDataLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cameraDataLayoutBinding.descriptorCount = 1;
    cameraDataLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;

    VkDescriptorSetLayoutBinding bindings[] = {
        asLayoutBinding,
        outputImageLayoutBinding,
        cameraDataLayoutBinding
    };

    VkDescriptorSetLayoutCreateInfo set0LayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    set0LayoutInfo.flags = 0;
    set0LayoutInfo.bindingCount = (uint32_t)std::size(bindings);
    set0LayoutInfo.pBindings = bindings;

    VK_CHECK(vkCreateDescriptorSetLayout(g_device, &set0LayoutInfo, nullptr, &g_descriptorSetLayout));
}

void createRaytracingPipeline()
{
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutCreateInfo.flags = 0;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &g_descriptorSetLayout;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 0;

    VK_CHECK(vkCreatePipelineLayout(g_device, &pipelineLayoutCreateInfo, nullptr, &g_pipelineLayout));

    loadShader("shaders/raygen.rgen.spv", &g_raygenShader);
    loadShader("shaders/chit.rchit.spv", &g_chitShader);
    loadShader("shaders/miss.rmiss.spv", &g_missShader);

    VkPipelineShaderStageCreateInfo raygenStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    raygenStage.stage = VK_SHADER_STAGE_RAYGEN_BIT_NV;
    raygenStage.module = g_raygenShader;
    raygenStage.pName = "main";

    VkPipelineShaderStageCreateInfo chitStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    chitStage.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
    chitStage.module = g_chitShader;
    chitStage.pName = "main";

    VkPipelineShaderStageCreateInfo missStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    missStage.stage = VK_SHADER_STAGE_MISS_BIT_NV;
    missStage.module = g_missShader;
    missStage.pName = "main";

    VkRayTracingShaderGroupCreateInfoNV raygenGroup = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV };
    raygenGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
    raygenGroup.generalShader = 0;
    raygenGroup.closestHitShader = VK_SHADER_UNUSED_NV;
    raygenGroup.anyHitShader = VK_SHADER_UNUSED_NV;
    raygenGroup.intersectionShader = VK_SHADER_UNUSED_NV;

    VkRayTracingShaderGroupCreateInfoNV chitGroup = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV };
    chitGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV;
    raygenGroup.generalShader = VK_SHADER_UNUSED_NV;
    raygenGroup.closestHitShader = 1;
    raygenGroup.anyHitShader = VK_SHADER_UNUSED_NV;
    raygenGroup.intersectionShader = VK_SHADER_UNUSED_NV;

    VkRayTracingShaderGroupCreateInfoNV missGroup = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV };
    missGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
    missGroup.generalShader = 2;
    missGroup.closestHitShader = VK_SHADER_UNUSED_NV;
    missGroup.anyHitShader = VK_SHADER_UNUSED_NV;
    missGroup.intersectionShader = VK_SHADER_UNUSED_NV;

    VkPipelineShaderStageCreateInfo stages[] = {
        raygenStage,
        chitStage,
        missStage
    };

    VkRayTracingShaderGroupCreateInfoNV groups[] = {
        raygenGroup,
        chitGroup,
        missGroup
    };

    VkRayTracingPipelineCreateInfoNV rayPipelineInfo = { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV };
    rayPipelineInfo.stageCount = (uint32_t)std::size(stages);
    rayPipelineInfo.pStages = stages;
    rayPipelineInfo.groupCount = (uint32_t)std::size(groups);
    rayPipelineInfo.pGroups = groups;
    rayPipelineInfo.maxRecursionDepth = 1;
    rayPipelineInfo.layout = g_pipelineLayout;

    VK_CHECK(vkCreateRayTracingPipelinesNV(g_device, VK_NULL_HANDLE, 1, &rayPipelineInfo, nullptr, &g_rtPipeline));
}

void createShaderBindingTable()
{
    {
        auto& buff = g_sbtBuffer;

        uint32_t sbtSize = g_rtProps.shaderGroupHandleSize * 3;

        VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferCreateInfo.flags = 0;
        bufferCreateInfo.size = sbtSize;
        bufferCreateInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        buff.size = sbtSize;

        VK_CHECK(vkCreateBuffer(g_device, &bufferCreateInfo, nullptr, &buff.buffer));

        VkMemoryRequirements memoryRequirements;
        vkGetBufferMemoryRequirements(g_device, buff.buffer, &memoryRequirements);

        VkMemoryAllocateInfo memAllocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        memAllocInfo.allocationSize = memoryRequirements.size;
        memAllocInfo.memoryTypeIndex = getMemoryType(memoryRequirements,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

        VK_CHECK(vkAllocateMemory(g_device, &memAllocInfo, nullptr, &buff.memory));

        VK_CHECK(vkBindBufferMemory(g_device, buff.buffer, buff.memory, 0));
    }

    void* sbtBufferMemory = nullptr;
    VK_CHECK(vkMapMemory(g_device, g_sbtBuffer.memory, 0, g_sbtBuffer.size, 0, &sbtBufferMemory));

    // We only need the handles
    int numGroups = 3; // raygen, 1 hit, 1 miss
    VK_CHECK(vkGetRayTracingShaderGroupHandlesNV(g_device, g_rtPipeline, 0,
                numGroups, g_sbtBuffer.size, sbtBufferMemory));

    vkUnmapMemory(g_device, g_sbtBuffer.memory);
}

void createDescriptorSets()
{
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 1 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }
    };

    VkDescriptorPoolCreateInfo descPoolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    descPoolCreateInfo.poolSizeCount = (uint32_t)std::size(poolSizes);
    descPoolCreateInfo.pPoolSizes = poolSizes;
    descPoolCreateInfo.maxSets = 1;

    VK_CHECK(vkCreateDescriptorPool(g_device, &descPoolCreateInfo, nullptr, &g_descriptorPool));

    VkDescriptorSetLayout setLayouts[] = {
        g_descriptorSetLayout
    };

    VkDescriptorSetAllocateInfo descSetAllocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    descSetAllocInfo.descriptorPool = g_descriptorPool;
    descSetAllocInfo.pSetLayouts = setLayouts;
    descSetAllocInfo.descriptorSetCount = 1;

    VK_CHECK(vkAllocateDescriptorSets(g_device, &descSetAllocInfo, &g_descriptorSet));

    VkWriteDescriptorSetAccelerationStructureNV descAccelStructInfo = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV };
    descAccelStructInfo.accelerationStructureCount = 1;
    descAccelStructInfo.pAccelerationStructures = &g_scene.tlas.accelerationStructure;

    VkWriteDescriptorSet accelStructWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    accelStructWrite.pNext = &descAccelStructInfo;
    accelStructWrite.dstSet = g_descriptorSet;
    accelStructWrite.dstBinding = 0;
    accelStructWrite.descriptorCount = 1;
    accelStructWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
    accelStructWrite.pImageInfo = nullptr;
    accelStructWrite.pBufferInfo = nullptr;
    accelStructWrite.pTexelBufferView = nullptr;

    VkDescriptorImageInfo descOutputImageInfo = {};
    descOutputImageInfo.imageView = g_offscreenImage.imageView;
    descOutputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet resImageWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    resImageWrite.dstSet = g_descriptorSet;
    resImageWrite.dstBinding = 1;
    resImageWrite.descriptorCount = 1;
    resImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    resImageWrite.pImageInfo = &descOutputImageInfo;
    resImageWrite.pBufferInfo = nullptr;
    resImageWrite.pTexelBufferView = nullptr;

    VkDescriptorBufferInfo camdataBufferInfo = {};
    camdataBufferInfo.buffer = g_camera.buffer.buffer;
    camdataBufferInfo.offset = 0;
    camdataBufferInfo.range = g_camera.buffer.size;

    VkWriteDescriptorSet camdataBufferWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    camdataBufferWrite.dstSet = g_descriptorSet;
    camdataBufferWrite.dstBinding = 2;
    camdataBufferWrite.descriptorCount = 1;
    camdataBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    camdataBufferWrite.pImageInfo = nullptr;
    camdataBufferWrite.pBufferInfo = &camdataBufferInfo;
    camdataBufferWrite.pTexelBufferView = nullptr;

    VkWriteDescriptorSet descriptorWrites[] = {
        accelStructWrite,
        resImageWrite,
        camdataBufferWrite
    };

    vkUpdateDescriptorSets(g_device, (uint32_t)std::size(descriptorWrites), descriptorWrites, 0, VK_NULL_HANDLE);
}

int main()
{
    if (!initVulkan())
    {
        fprintf(stderr, "Error: unable to initialize Vulkan\n");
        return 1;
    }

    createScene();

    setupCamera();

    createDescriptorSetLayouts();
    createRaytracingPipeline();
    createShaderBindingTable();
    createDescriptorSets();

    





    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

    printf("%d extensions supported\n", extensionCount);

    glm::mat4 matrix;
    glm::vec4 vec;
    auto test = matrix * vec;

    while (!glfwWindowShouldClose(g_window)) {
        glfwPollEvents();
    }

    vkDeviceWaitIdle(g_device);

    glfwDestroyWindow(g_window);
    glfwTerminate();

    return 0;
}
