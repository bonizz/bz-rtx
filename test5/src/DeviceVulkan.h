#pragma once

#define VK_CHECK(_error) do { \
    if (_error != VK_SUCCESS) { \
        BASSERT(!#_error); \
    } \
} while (false)


struct BufferVulkan
{
    VkDeviceSize size;
    VkBuffer buffer;
    VkDeviceMemory memory;
};

struct BufferVulkanCreateInfo
{
    VkDeviceSize size;
    VkBufferUsageFlags usage;
    VkMemoryPropertyFlags memoryProperties;
    const void* pSrc;
};

struct ImageVulkan
{
    VkImageType type;
    VkFormat format;
    VkExtent3D extent;

    VkImage image;
    VkImageView view;

    VkDeviceMemory memory;
};

struct ImageVulkanCreateInfo
{
    VkImageType imageType;
    VkFormat format;
    VkExtent3D extent;
    VkImageUsageFlags usage;
    VkMemoryPropertyFlags memoryProperties;
};

struct AccelerationStructureVulkan
{
    VkDeviceMemory memory;
    VkAccelerationStructureInfoNV accelerationStructureInfo;
    VkAccelerationStructureNV accelerationStructure;
    uint64_t handle;
};

struct AccelerationStructureVulkanCreateInfo
{
    VkAccelerationStructureTypeNV type;
    uint32_t geometryCount;
    uint32_t instanceCount;
    VkGeometryNV* pGeometries;
};

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

struct DeviceVulkanCreateInfo
{
    void* hwnd;
    uint32_t windowWidth;
    uint32_t windowHeight;
};

bool createDeviceVulkan(const DeviceVulkanCreateInfo& ci, DeviceVulkan* deviceVulkan);

void destroyDeviceVulkan(DeviceVulkan& vk);

uint32_t getMemoryTypeVulkan(const DeviceVulkan& vk, VkMemoryRequirements& memoryRequirements, VkMemoryPropertyFlags memoryProperties);

bool createBufferVulkan(const DeviceVulkan& vk, const BufferVulkanCreateInfo& ci, BufferVulkan* pBuffer);

void destroyBufferVulkan(const DeviceVulkan& vk, BufferVulkan& buffer);

bool createImageVulkan(const DeviceVulkan& vk, const ImageVulkanCreateInfo& ci, ImageVulkan* pImage);

// Returns staging buffers which need to be freed by caller
bool createImageVulkanLocal(const DeviceVulkan& vk, const ImageVulkanCreateInfo& ci,
    VkCommandBuffer cmdBuffer, size_t size, const void* data,
    ImageVulkan* pImage, std::vector<BufferVulkan>* pStagingBuffers);

void destroyImageVulkan(const DeviceVulkan& vk, ImageVulkan& image);

bool createShaderVulkan(const DeviceVulkan& vk, const char* filename, VkShaderModule* pShaderModule);

bool createAccelerationStructureVulkan(const DeviceVulkan& vk,
    const AccelerationStructureVulkanCreateInfo& ci,
    AccelerationStructureVulkan* pAccelerationStructure);

void destroyAccelerationStructure(const DeviceVulkan& vk,
    AccelerationStructureVulkan& as);

