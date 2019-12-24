#pragma once

#define VK_CHECK(_error) do { \
    if (_error != VK_SUCCESS) { \
        BASSERT(!#_error); \
    } \
} while (false)

struct DeviceVulkan
{

    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;

    uint32_t queueIndex;
    VkQueue queue;

    VkPhysicalDeviceRayTracingPropertiesNV rtProps;

    VkSurfaceKHR surface;
    VkSurfaceFormatKHR surfaceFormat;
    VkSwapchainKHR swapchain;

    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFence> waitForFrameFences;

    VkCommandPool commandPool;
    VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties;
    std::vector<VkCommandBuffer> commandBuffers;
    VkSemaphore semaphoreImageAcquired;
    VkSemaphore semaphoreRenderFinished;
    VkDebugReportCallbackEXT debugCallback;

};

bool createDeviceVulkan(void* hwnd, uint32_t windowWidth, uint32_t windowHeight, DeviceVulkan* deviceVulkan);

void destroyDeviceVulkan(DeviceVulkan& deviceVulkan);

