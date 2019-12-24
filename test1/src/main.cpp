
#include "pch.h"

#include "logging.h"

#include "DeviceVulkan.h"



struct ImageVulkan
{
    VkFormat format;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView imageView;
    VkSampler sampler;
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


uint32_t kWindowWidth = 800;
uint32_t kWindowHeight = 600;

GLFWwindow* g_window;








VkShaderModule g_raygenShader;
VkShaderModule g_chitShader;
VkShaderModule g_missShader;

ImageVulkan g_offscreenImage;

BufferVulkan g_instancesBuffer;

VkDescriptorSetLayout g_descriptorSetLayout;
VkPipelineLayout g_pipelineLayout;
VkPipeline g_rtPipeline;

BufferVulkan g_sbtBuffer;

VkDescriptorPool g_descriptorPool;
VkDescriptorSet g_descriptorSet;




struct CameraUniformData
{
    glm::mat4 viewInverse;
    glm::mat4 projInverse;
};

struct CameraData
{
    glm::mat4 view;
    glm::mat4 perspective;

    BufferVulkan buffer;
};

CameraData g_camera;
Mesh g_mesh;
Scene g_scene;


DeviceVulkan vk;





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

        VkResult error = vkCreateShaderModule(vk.device, &ci, nullptr, pShaderModule);
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

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    g_window = glfwCreateWindow(kWindowWidth, kWindowHeight, "Test Vulkan App", nullptr, nullptr);

    if (!g_window)
        return false;

    HWND hwnd = glfwGetWin32Window(g_window);

    if (!createDeviceVulkan({hwnd, kWindowWidth, kWindowHeight}, &vk))
        return false;

    {
        VkExtent3D imageExtent = { kWindowWidth, kWindowHeight, 1 };

        auto& img = g_offscreenImage;
        img = {};

        img.format = vk.surfaceFormat.format;

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

        VK_CHECK(vkCreateImage(vk.device, &imageCreateInfo, nullptr, &img.image));

        VkMemoryRequirements memoryRequirements;
        vkGetImageMemoryRequirements(vk.device, img.image, &memoryRequirements);

        VkMemoryAllocateInfo memoryAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        memoryAllocateInfo.allocationSize = memoryRequirements.size;
        memoryAllocateInfo.memoryTypeIndex = getMemoryTypeVulkan(vk, memoryRequirements,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK(vkAllocateMemory(vk.device, &memoryAllocateInfo, nullptr, &img.memory));

        VK_CHECK(vkBindImageMemory(vk.device, img.image, img.memory, 0));

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

        VK_CHECK(vkCreateImageView(vk.device, &imageViewCreateInfo, nullptr, &img.imageView));
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

    createBufferVulkan(vk, { sizeof(positions),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        positions }, &g_mesh.positions);

    createBufferVulkan(vk, { sizeof(indices),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        indices }, &g_mesh.indices);

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

        VK_CHECK(vkCreateAccelerationStructureNV(vk.device, &accelerationStructureCreateInfo, nullptr, &g_mesh.blas.accelerationStructure));

        VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
        memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
        memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
        memoryRequirementsInfo.accelerationStructure = g_mesh.blas.accelerationStructure;

        VkMemoryRequirements2 memoryRequirements;
        vkGetAccelerationStructureMemoryRequirementsNV(vk.device, &memoryRequirementsInfo, &memoryRequirements);

        VkMemoryAllocateInfo memoryAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        memoryAllocateInfo.allocationSize = memoryRequirements.memoryRequirements.size;
        memoryAllocateInfo.memoryTypeIndex = getMemoryTypeVulkan(vk, memoryRequirements.memoryRequirements,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK(vkAllocateMemory(vk.device, &memoryAllocateInfo, nullptr, &g_mesh.blas.memory));

        VkBindAccelerationStructureMemoryInfoNV bindInfo = { VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV };
        bindInfo.accelerationStructure = g_mesh.blas.accelerationStructure;
        bindInfo.memory = g_mesh.blas.memory;
        bindInfo.memoryOffset = 0;

        vkBindAccelerationStructureMemoryNV(vk.device, 1, &bindInfo);

        VK_CHECK(vkGetAccelerationStructureHandleNV(vk.device, g_mesh.blas.accelerationStructure, sizeof(uint64_t), &g_mesh.blas.handle));
    }

    VkGeometryInstance instance;
    memcpy(instance.transform, transform, sizeof(transform));
    instance.instanceId = 0;
    instance.mask = 0xFF;
    instance.instanceOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;
    instance.accelerationStructureHandle = g_mesh.blas.handle;

    createBufferVulkan(vk, { sizeof(VkGeometryInstance),
        VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &instance }, &g_instancesBuffer);

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

        VK_CHECK(vkCreateAccelerationStructureNV(vk.device, &accelerationStructureCreateInfo, nullptr, &as.accelerationStructure));

        VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
        memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
        memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
        memoryRequirementsInfo.accelerationStructure = as.accelerationStructure;

        VkMemoryRequirements2 memoryRequirements;
        vkGetAccelerationStructureMemoryRequirementsNV(vk.device, &memoryRequirementsInfo, &memoryRequirements);

        VkMemoryAllocateInfo memoryAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        memoryAllocateInfo.allocationSize = memoryRequirements.memoryRequirements.size;
        memoryAllocateInfo.memoryTypeIndex = getMemoryTypeVulkan(vk, memoryRequirements.memoryRequirements,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK(vkAllocateMemory(vk.device, &memoryAllocateInfo, nullptr, &as.memory));

        VkBindAccelerationStructureMemoryInfoNV bindInfo = { VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV };
        bindInfo.accelerationStructure = as.accelerationStructure;
        bindInfo.memory = as.memory;
        bindInfo.memoryOffset = 0;

        vkBindAccelerationStructureMemoryNV(vk.device, 1, &bindInfo);

        VK_CHECK(vkGetAccelerationStructureHandleNV(vk.device, g_mesh.blas.accelerationStructure, sizeof(uint64_t), &as.handle));
    }

    // Build bottom/top AS
    
    VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
    memoryRequirementsInfo.sType = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV };
    memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;

    VkDeviceSize maxBlasSize = 0;
    memoryRequirementsInfo.accelerationStructure = g_mesh.blas.accelerationStructure;

    VkMemoryRequirements2 memReqBlas;
    vkGetAccelerationStructureMemoryRequirementsNV(vk.device, &memoryRequirementsInfo, &memReqBlas);

    maxBlasSize = memReqBlas.memoryRequirements.size;

    VkMemoryRequirements2 memReqTlas;
    memoryRequirementsInfo.accelerationStructure = g_scene.tlas.accelerationStructure;
    vkGetAccelerationStructureMemoryRequirementsNV(vk.device, &memoryRequirementsInfo, &memReqTlas);

    VkDeviceSize scratchBufferSize = std::max(maxBlasSize, memReqTlas.memoryRequirements.size);

    BufferVulkan scratchBuffer;

    createBufferVulkan(vk, { scratchBufferSize,
    VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT }, &scratchBuffer);

    VkCommandBufferAllocateInfo commandBufferAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    commandBufferAllocateInfo.commandPool = vk.commandPool;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(vk.device, &commandBufferAllocateInfo, &cmdBuffer));

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

    VK_CHECK(vkQueueSubmit(vk.queue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(vk.queue));

    vkFreeCommandBuffers(vk.device, vk.commandPool, 1, &cmdBuffer);

    destroyBufferVulkan(vk, scratchBuffer);
}

void setupCamera()
{
    createBufferVulkan(vk, { sizeof(CameraUniformData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT },
        &g_camera.buffer);

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

    VK_CHECK(vkCreateDescriptorSetLayout(vk.device, &set0LayoutInfo, nullptr, &g_descriptorSetLayout));
}

void createRaytracingPipeline()
{
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutCreateInfo.flags = 0;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &g_descriptorSetLayout;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 0;

    VK_CHECK(vkCreatePipelineLayout(vk.device, &pipelineLayoutCreateInfo, nullptr, &g_pipelineLayout));

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
    chitGroup.generalShader = VK_SHADER_UNUSED_NV;
    chitGroup.closestHitShader = 1;
    chitGroup.anyHitShader = VK_SHADER_UNUSED_NV;
    chitGroup.intersectionShader = VK_SHADER_UNUSED_NV;

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

    VK_CHECK(vkCreateRayTracingPipelinesNV(vk.device, VK_NULL_HANDLE, 1, &rayPipelineInfo, nullptr, &g_rtPipeline));
}

void createShaderBindingTable()
{
    uint32_t sbtSize = vk.rtProps.shaderGroupHandleSize * 3;

    createBufferVulkan(vk, { sbtSize,
        VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT }, &g_sbtBuffer);

    // Put shader group handles in SBT memory
    std::vector<char> tmp(g_sbtBuffer.size);

    int numGroups = 3; // raygen, 1 hit, 1 miss
    VK_CHECK(vkGetRayTracingShaderGroupHandlesNV(vk.device, g_rtPipeline, 0, numGroups, g_sbtBuffer.size, tmp.data()));

    void* sbtBufferMemory = nullptr;
    VK_CHECK(vkMapMemory(vk.device, g_sbtBuffer.memory, 0, g_sbtBuffer.size, 0, &sbtBufferMemory));
    memcpy(sbtBufferMemory, tmp.data(), tmp.size());
    vkUnmapMemory(vk.device, g_sbtBuffer.memory);
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

    VK_CHECK(vkCreateDescriptorPool(vk.device, &descPoolCreateInfo, nullptr, &g_descriptorPool));

    VkDescriptorSetLayout setLayouts[] = {
        g_descriptorSetLayout
    };

    VkDescriptorSetAllocateInfo descSetAllocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    descSetAllocInfo.descriptorPool = g_descriptorPool;
    descSetAllocInfo.pSetLayouts = setLayouts;
    descSetAllocInfo.descriptorSetCount = 1;

    VK_CHECK(vkAllocateDescriptorSets(vk.device, &descSetAllocInfo, &g_descriptorSet));

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

    vkUpdateDescriptorSets(vk.device, (uint32_t)std::size(descriptorWrites), descriptorWrites, 0, VK_NULL_HANDLE);
}

void fillCommandBuffers()
{
    VkCommandBufferBeginInfo commandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    commandBufferBeginInfo.flags = 0;

    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = 1;

    for (size_t i = 0; i < vk.commandBuffers.size(); i++)
    {
        const VkCommandBuffer cmdBuffer = vk.commandBuffers[i];

        VK_CHECK(vkBeginCommandBuffer(cmdBuffer, &commandBufferBeginInfo));

        VkImageMemoryBarrier imageMemoryBarrier1 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageMemoryBarrier1.srcAccessMask = 0; // ?
        imageMemoryBarrier1.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        imageMemoryBarrier1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageMemoryBarrier1.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageMemoryBarrier1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier1.image = g_offscreenImage.image;
        imageMemoryBarrier1.subresourceRange = subresourceRange;

        vkCmdPipelineBarrier(cmdBuffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier1);

        {
            vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, g_rtPipeline);

            vkCmdBindDescriptorSets(cmdBuffer,
                VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
                g_pipelineLayout, 0,
                1, &g_descriptorSet,
                0, 0);

            uint32_t stride = vk.rtProps.shaderGroupHandleSize;

            vkCmdTraceRaysNV(cmdBuffer,
                // raygen
                g_sbtBuffer.buffer, 0,
                // miss
                g_sbtBuffer.buffer, stride * 2, stride,
                // hit
                g_sbtBuffer.buffer, stride * 1, stride,
                // callable
                VK_NULL_HANDLE, 0, 0,
                kWindowWidth, kWindowHeight, 1);
        }

        // prepare swapchain as transfer dst

        VkImageMemoryBarrier imageMemoryBarrier2 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageMemoryBarrier2.srcAccessMask = 0;
        imageMemoryBarrier2.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageMemoryBarrier2.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageMemoryBarrier2.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageMemoryBarrier2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier2.image = vk.swapchainImages[i];
        imageMemoryBarrier2.subresourceRange = subresourceRange;

        vkCmdPipelineBarrier(cmdBuffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier2);

        // prepare offscreen image as transfer src

        VkImageMemoryBarrier imageMemoryBarrier3 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageMemoryBarrier3.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        imageMemoryBarrier3.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        imageMemoryBarrier3.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageMemoryBarrier3.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        imageMemoryBarrier3.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier3.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier3.image = g_offscreenImage.image;
        imageMemoryBarrier3.subresourceRange = subresourceRange;

        vkCmdPipelineBarrier(cmdBuffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier3);

        VkImageCopy copyRegion = {};
        copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copyRegion.srcOffset = { 0, 0, 0 };
        copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copyRegion.dstOffset = { 0, 0, 0 };
        copyRegion.extent = { kWindowWidth, kWindowHeight, 1 };

        vkCmdCopyImage(cmdBuffer,
            g_offscreenImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            vk.swapchainImages[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &copyRegion);

        VkImageMemoryBarrier imageMemoryBarrier4 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageMemoryBarrier4.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageMemoryBarrier4.dstAccessMask = 0;
        imageMemoryBarrier4.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageMemoryBarrier4.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        imageMemoryBarrier4.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier4.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier4.image = vk.swapchainImages[i];
        imageMemoryBarrier4.subresourceRange = subresourceRange;

        vkCmdPipelineBarrier(cmdBuffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier4);

        VK_CHECK(vkEndCommandBuffer(cmdBuffer));
    }

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

    // BONI TODO: do this every frame
    fillCommandBuffers();

    while (!glfwWindowShouldClose(g_window))
    {
        uint32_t imageIndex = 0;
        VK_CHECK(vkAcquireNextImageKHR(vk.device, vk.swapchain, UINT64_MAX, vk.semaphoreImageAcquired, nullptr, &imageIndex));

        const VkFence fence = vk.waitForFrameFences[imageIndex];
        VK_CHECK(vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX));
        vkResetFences(vk.device, 1, &fence);

        // Update camera
        {
            CameraUniformData camData;
            camData.viewInverse = glm::inverse(g_camera.view);
            camData.projInverse = glm::inverse(g_camera.perspective);

            void* mem = nullptr;
            VK_CHECK(vkMapMemory(vk.device, g_camera.buffer.memory, 0, g_camera.buffer.size, 0, &mem));

            memcpy(mem, &camData, sizeof(CameraUniformData));

            vkUnmapMemory(vk.device, g_camera.buffer.memory);
        }

        const VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &vk.semaphoreImageAcquired;
        submitInfo.pWaitDstStageMask = &waitStageMask;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &vk.commandBuffers[imageIndex];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &vk.semaphoreRenderFinished;

        VK_CHECK(vkQueueSubmit(vk.queue, 1, &submitInfo, fence));

        VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &vk.semaphoreRenderFinished;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &vk.swapchain;
        presentInfo.pImageIndices = &imageIndex;

        VK_CHECK(vkQueuePresentKHR(vk.queue, &presentInfo));

        glfwPollEvents();
    }

    vkDeviceWaitIdle(vk.device);

    glfwDestroyWindow(g_window);
    glfwTerminate();

    return 0;
}
