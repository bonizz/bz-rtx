
#include "pch.h"

void DebugPrint(const char* fmt, ...);

#define BASSERT(expr) ((void)((!!(expr)) || \
        ((DebugPrint("Error: %s, %s:%d\n", #expr, __FILE__, __LINE__),1) \
         && (__debugbreak(),0)) ))




#define VK_CHECK(_error) do { \
    if (_error != VK_SUCCESS) { \
        BASSERT(false && #_error); \
    } \
} while (false)

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



struct ImageVulkan
{
    VkFormat format;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView imageView;
    VkSampler sampler;
};

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
    BASSERT(0 && "validation error");

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

bool init()
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
    //VkResult error = mOffscreenImage.Create(VK_IMAGE_TYPE_2D,
    //                                        mSurfaceFormat.format,
    //                                        extent,
    //                                        VK_IMAGE_TILING_OPTIMAL,
    //                                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    //                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
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

//    error = mOffscreenImage.CreateImageView(VK_IMAGE_VIEW_TYPE_2D, mSurfaceFormat.format, range);
//
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

int main()
{
    if (!init())
    {
        fprintf(stderr, "Error: unable to initialize Vulkan\n");
        return 1;
    }





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
