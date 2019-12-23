
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

Mesh g_mesh;
BufferVulkan g_instancesBuffer;
Scene g_scene;




ImageVulkan g_offscreenImage;

void DebugPrint(const char* fmt, ...)
{
    char buffer[512] = {};
    va_list args;
    va_start(args, fmt);
    vsnprintf_s(buffer, std::size(buffer), _TRUNCATE, fmt, args);
    OutputDebugStringA(buffer);
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
    float indices[] = { 0, 1, 2 };

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
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV,
            0, 1, &memoryBarrier, 0, 0, 0, 0);

    g_scene.tlas.accelerationStructureInfo.instanceCount = 1;
    g_scene.tlas.accelerationStructureInfo.geometryCount = 0;
    g_scene.tlas.accelerationStructureInfo.pGeometries = nullptr;
    vkCmdBuildAccelerationStructureNV(cmdBuffer, &g_scene.tlas.accelerationStructureInfo,
            g_instancesBuffer.buffer, 0, VK_FALSE, g_scene.tlas.accelerationStructure,
            VK_NULL_HANDLE, scratchBuffer.buffer, 0);

    vkCmdPipelineBarrier(cmdBuffer,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV,
            0, 1, &memoryBarrier, 0, 0, 0, 0);

    vkEndCommandBuffer(cmdBuffer);

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.waitSemaphoreCount = 0;
    submitInfo.signalSemaphoreCount = 0;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(g_queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_queue);
    vkFreeCommandBuffers(g_device, g_commandPool, 1, &cmdBuffer);
}

int main()
{
    if (!initVulkan())
    {
        fprintf(stderr, "Error: unable to initialize Vulkan\n");
        return 1;
    }

    createScene();







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
