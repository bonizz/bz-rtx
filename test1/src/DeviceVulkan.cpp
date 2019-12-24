#include "pch.h"

#include "logging.h"
#include "DeviceVulkan.h"

static bool kEnableValidation = true;

static VKAPI_ATTR VkBool32 VKAPI_CALL debugReportCallback(VkDebugReportFlagsEXT flags,
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

static VkPresentModeKHR getPresentMode(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

    for (auto& mode : presentModes)
        if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
            return mode;

    for (auto& mode : presentModes)
        if (mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR)
            return mode;

    return VK_PRESENT_MODE_FIFO_KHR;
}

bool createDeviceVulkan(void* hwnd, uint32_t windowWidth, uint32_t windowHeight, DeviceVulkan* deviceVulkan)
{
    DeviceVulkan& vk = *deviceVulkan;

    if (volkInitialize() != VK_SUCCESS)
        return false;

    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "Test Vulkan App";
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

    VkResult res = vkCreateInstance(&instInfo, nullptr, &vk.instance);
    if (res != VK_SUCCESS)
    {
        BASSERT(0);
        return false;
    }

    volkLoadInstance(vk.instance);

    {
        VkDebugReportCallbackCreateInfoEXT callbackCreateInfo = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT };
        callbackCreateInfo.flags =
            VK_DEBUG_REPORT_ERROR_BIT_EXT |
            VK_DEBUG_REPORT_WARNING_BIT_EXT |
            VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        callbackCreateInfo.pfnCallback = debugReportCallback;

        VK_CHECK(vkCreateDebugReportCallbackEXT(vk.instance, &callbackCreateInfo, nullptr, &vk.debugCallback));
    }

    uint32_t numPhysicalDevices = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(vk.instance, &numPhysicalDevices, nullptr));
    std::vector<VkPhysicalDevice> physicalDevices(numPhysicalDevices);
    VK_CHECK(vkEnumeratePhysicalDevices(vk.instance, &numPhysicalDevices, physicalDevices.data()));
    vk.physicalDevice = physicalDevices[0];

    uint32_t queueFamilyPropertyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(vk.physicalDevice, &queueFamilyPropertyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyPropertyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(vk.physicalDevice, &queueFamilyPropertyCount, queueFamilyProperties.data());

    vk.queueIndex = ~0u;
    for (uint32_t i = 0; i < queueFamilyPropertyCount; i++)
    {
        if (queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            vk.queueIndex = i;
            break;
        }
    }

    if (vk.queueIndex == ~0u)
        return false;

    const float queuePriorites[] = { 1.f };
    VkDeviceQueueCreateInfo deviceQueueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    deviceQueueCreateInfo.flags = 0;
    deviceQueueCreateInfo.queueFamilyIndex = vk.queueIndex;
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

    vkGetPhysicalDeviceFeatures2(vk.physicalDevice, &features2);

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

    VK_CHECK(vkCreateDevice(vk.physicalDevice, &deviceCreateInfo, nullptr, &vk.device));

    vkGetDeviceQueue(vk.device, vk.queueIndex, 0, &vk.queue);

    vk.rtProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV };
    vk.rtProps.maxRecursionDepth = 0;
    vk.rtProps.shaderGroupHandleSize = 0;

    VkPhysicalDeviceProperties2 deviceProperties2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    deviceProperties2.pNext = &vk.rtProps;

    vkGetPhysicalDeviceProperties2(vk.physicalDevice, &deviceProperties2);

    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    surfaceCreateInfo.hinstance = GetModuleHandle(nullptr);
    surfaceCreateInfo.hwnd = (HWND)hwnd;

    VK_CHECK(vkCreateWin32SurfaceKHR(vk.instance, &surfaceCreateInfo, nullptr, &vk.surface));

    VkBool32 supportPresent = VK_FALSE;
    VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(vk.physicalDevice, vk.queueIndex, vk.surface, &supportPresent));

    uint32_t surfaceFormatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physicalDevice, vk.surface, &surfaceFormatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physicalDevice, vk.surface, &surfaceFormatCount, surfaceFormats.data());

    // VK_FORMAT_B8G8R8A8_UNORM
    bool surfaceFound = false;
    for (auto& f : surfaceFormats)
    {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM)
        {
            vk.surfaceFormat = f;
            surfaceFound = true;
            break;
        }
    }

    if (!surfaceFound)
        vk.surfaceFormat = surfaceFormats[0];

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physicalDevice, vk.surface, &surfaceCapabilities));

    VkPresentModeKHR presentMode = getPresentMode(vk.physicalDevice, vk.surface);

    VkSwapchainCreateInfoKHR swapchainCreateInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    swapchainCreateInfo.flags = 0;
    swapchainCreateInfo.surface = vk.surface;
    swapchainCreateInfo.minImageCount = surfaceCapabilities.minImageCount;
    swapchainCreateInfo.imageFormat = vk.surfaceFormat.format;
    swapchainCreateInfo.imageColorSpace = vk.surfaceFormat.colorSpace;
    swapchainCreateInfo.imageExtent.width = windowWidth;
    swapchainCreateInfo.imageExtent.height = windowHeight;
    swapchainCreateInfo.imageArrayLayers = 1;
    swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCreateInfo.queueFamilyIndexCount = 0;
    swapchainCreateInfo.pQueueFamilyIndices = nullptr;
    swapchainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainCreateInfo.presentMode = presentMode;
    swapchainCreateInfo.clipped = VK_TRUE;
    swapchainCreateInfo.oldSwapchain = nullptr;

    VK_CHECK(vkCreateSwapchainKHR(vk.device, &swapchainCreateInfo, nullptr, &vk.swapchain));

    uint32_t imageCount;
    vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &imageCount, nullptr);
    vk.swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &imageCount, vk.swapchainImages.data());

    vk.swapchainImageViews.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++)
    {
        VkImageViewCreateInfo imageViewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        imageViewCreateInfo.format = vk.surfaceFormat.format;
        imageViewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.image = vk.swapchainImages[i];
        imageViewCreateInfo.flags = 0;
        imageViewCreateInfo.components = {};

        VK_CHECK(vkCreateImageView(vk.device, &imageViewCreateInfo, nullptr, &vk.swapchainImageViews[i]));
    }

    VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    vk.waitForFrameFences.resize(vk.swapchainImages.size());
    for (auto& fence : vk.waitForFrameFences)
        vkCreateFence(vk.device, &fenceCreateInfo, nullptr, &fence);

    VkCommandPoolCreateInfo commandPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCreateInfo.queueFamilyIndex = vk.queueIndex;

    VK_CHECK(vkCreateCommandPool(vk.device, &commandPoolCreateInfo, nullptr, &vk.commandPool));

    vkGetPhysicalDeviceMemoryProperties(vk.physicalDevice, &vk.physicalDeviceMemoryProperties);

    {
        vk.commandBuffers.resize(vk.swapchainImages.size());

        VkCommandBufferAllocateInfo commandBufferAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        commandBufferAllocateInfo.commandPool = vk.commandPool;
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferAllocateInfo.commandBufferCount = uint32_t(vk.commandBuffers.size());

        VK_CHECK(vkAllocateCommandBuffers(vk.device, &commandBufferAllocateInfo, vk.commandBuffers.data()));
    }

    {
        VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        semaphoreCreateInfo.flags = 0;

        VK_CHECK(vkCreateSemaphore(vk.device, &semaphoreCreateInfo, nullptr, &vk.semaphoreImageAcquired));
        VK_CHECK(vkCreateSemaphore(vk.device, &semaphoreCreateInfo, nullptr, &vk.semaphoreRenderFinished));
    }

    return true;
}

void destroyDeviceVulkan(DeviceVulkan& deviceVulkan)
{
    // BONI TODO
}

