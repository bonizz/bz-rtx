
#include "pch.h"

#include "logging.h"
#include "DeviceVulkan.h"
#include "camera.h"

struct VkGeometryInstance
{
    float transform[12];
    uint32_t instanceId : 24;
    uint32_t mask : 8;
    uint32_t instanceOffset : 24;
    uint32_t flags : 8;
    uint64_t accelerationStructureHandle;
};

struct Mesh
{
    uint32_t numVertices;
    uint32_t numFaces;

    BufferVulkan positions;
    BufferVulkan indices;

    AccelerationStructureVulkan blas;
};

struct CameraUniformData
{
    glm::mat4 viewInverse;
    glm::mat4 projInverse;
};

struct CameraData
{
    glm::mat4 view;
    glm::mat4 perspective;

};

struct Scene
{
    Mesh mesh;

    Camera camera;
    BufferVulkan cameraBuffer;

    AccelerationStructureVulkan tlas;
};

struct App
{
    GLFWwindow* window;

    ImageVulkan offscreenImage;

    VkShaderModule raygenShader;
    VkShaderModule chitShader;
    VkShaderModule missShader;

    BufferVulkan instancesBuffer;
    BufferVulkan sbtBuffer;

    VkPipelineLayout pipelineLayout;
    VkPipeline rtPipeline;

    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;

    bool keysDown[GLFW_KEY_LAST + 1];
    bool mouseDown[GLFW_MOUSE_BUTTON_LAST + 1];
    glm::vec2 cursorPos;

    Scene scene;
};

const uint32_t kWindowWidth = 800;
const uint32_t kWindowHeight = 600;
const float kCameraRotationSpeed = 0.25f;
const float kCameraMoveSpeed = 2.f;

static DeviceVulkan vk;
static App app;


bool initApp()
{
    if (!glfwInit())
        return false;

    if (!glfwVulkanSupported())
        return false;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    app.window = glfwCreateWindow(kWindowWidth, kWindowHeight, "Test Vulkan App", nullptr, nullptr);

    if (!app.window)
        return false;

    HWND hwnd = glfwGetWin32Window(app.window);

    if (!createDeviceVulkan({hwnd, kWindowWidth, kWindowHeight}, &vk))
        return false;

    createImageVulkan(vk, { VK_IMAGE_TYPE_2D, vk.surfaceFormat.format,
        {kWindowWidth, kWindowHeight, 1},
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT }, &app.offscreenImage);

    return true;
}

void shutdownApp()
{
    vkDeviceWaitIdle(vk.device);

    destroyBufferVulkan(vk, app.scene.cameraBuffer);

    {
        destroyBufferVulkan(vk, app.scene.mesh.positions);
        destroyBufferVulkan(vk, app.scene.mesh.indices);

        destroyAccelerationStructure(vk, app.scene.mesh.blas);
    }

    destroyAccelerationStructure(vk, app.scene.tlas);

    vkDestroyShaderModule(vk.device, app.raygenShader, nullptr);
    vkDestroyShaderModule(vk.device, app.chitShader, nullptr);
    vkDestroyShaderModule(vk.device, app.missShader, nullptr);

    vkDestroyDescriptorPool(vk.device, app.descriptorPool, nullptr);

    destroyBufferVulkan(vk, app.sbtBuffer);

    vkDestroyPipeline(vk.device, app.rtPipeline, nullptr);
    vkDestroyPipelineLayout(vk.device, app.pipelineLayout, nullptr);

    vkDestroyDescriptorSetLayout(vk.device, app.descriptorSetLayout, nullptr);

    destroyBufferVulkan(vk, app.instancesBuffer);
    destroyImageVulkan(vk, app.offscreenImage);

    destroyDeviceVulkan(vk);

    glfwDestroyWindow(app.window);
    glfwTerminate();
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
        positions }, &app.scene.mesh.positions);

    createBufferVulkan(vk, { sizeof(indices),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        indices }, &app.scene.mesh.indices);

    app.scene.mesh.numVertices = 3;
    app.scene.mesh.numFaces = 1;

    VkGeometryNV geometry = { VK_STRUCTURE_TYPE_GEOMETRY_NV };
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_NV;
    geometry.geometry.triangles = { VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV };
    geometry.geometry.triangles.vertexData = app.scene.mesh.positions.buffer;
    geometry.geometry.triangles.vertexOffset = 0;
    geometry.geometry.triangles.vertexCount = app.scene.mesh.numVertices;
    geometry.geometry.triangles.vertexStride = sizeof(float) * 3;
    geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geometry.geometry.triangles.indexData = app.scene.mesh.indices.buffer;
    geometry.geometry.triangles.indexOffset = 0;
    geometry.geometry.triangles.indexCount = app.scene.mesh.numFaces * 3;
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

    createAccelerationStructureVulkan(vk, {
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV,
        1, 0, &geometry }, &app.scene.mesh.blas);

    VkGeometryInstance instance;
    memcpy(instance.transform, transform, sizeof(transform));
    instance.instanceId = 0;
    instance.mask = 0xFF;
    instance.instanceOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;
    instance.accelerationStructureHandle = app.scene.mesh.blas.handle;

    createBufferVulkan(vk, { sizeof(VkGeometryInstance),
        VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &instance }, &app.instancesBuffer);

    createAccelerationStructureVulkan(vk, {
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV,
        0, 1, nullptr }, &app.scene.tlas);

    // Build bottom/top AS
    
    VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
    memoryRequirementsInfo.sType = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV };
    memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;

    VkDeviceSize maxBlasSize = 0;
    memoryRequirementsInfo.accelerationStructure = app.scene.mesh.blas.accelerationStructure;

    VkMemoryRequirements2 memReqBlas;
    vkGetAccelerationStructureMemoryRequirementsNV(vk.device, &memoryRequirementsInfo, &memReqBlas);

    maxBlasSize = memReqBlas.memoryRequirements.size;

    VkMemoryRequirements2 memReqTlas;
    memoryRequirementsInfo.accelerationStructure = app.scene.tlas.accelerationStructure;
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

    app.scene.mesh.blas.accelerationStructureInfo.instanceCount = 0;
    app.scene.mesh.blas.accelerationStructureInfo.geometryCount = 1;
    app.scene.mesh.blas.accelerationStructureInfo.pGeometries = &geometry;
    vkCmdBuildAccelerationStructureNV(cmdBuffer, &app.scene.mesh.blas.accelerationStructureInfo,
            VK_NULL_HANDLE, 0, VK_FALSE, app.scene.mesh.blas.accelerationStructure,
            VK_NULL_HANDLE, scratchBuffer.buffer, 0);

    vkCmdPipelineBarrier(cmdBuffer,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV,
            0, 1, &memoryBarrier, 0, 0, 0, 0);

    app.scene.tlas.accelerationStructureInfo.instanceCount = 1;
    app.scene.tlas.accelerationStructureInfo.geometryCount = 0;
    app.scene.tlas.accelerationStructureInfo.pGeometries = nullptr;
    vkCmdBuildAccelerationStructureNV(cmdBuffer, &app.scene.tlas.accelerationStructureInfo,
            app.instancesBuffer.buffer, 0, VK_FALSE, app.scene.tlas.accelerationStructure,
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
        &app.scene.cameraBuffer);

    app.scene.camera.viewport = { 0, 0, kWindowWidth, kWindowHeight };
    app.scene.camera.position = glm::vec3(0, 0, -2.5f);
    app.scene.camera.direction = glm::vec3(0, 0, 1.f);
    cameraUpdateView(app.scene.camera);
    cameraUpdateProjection(app.scene.camera);
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

    VK_CHECK(vkCreateDescriptorSetLayout(vk.device, &set0LayoutInfo, nullptr, &app.descriptorSetLayout));
}

void createRaytracingPipeline()
{
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutCreateInfo.flags = 0;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &app.descriptorSetLayout;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 0;

    VK_CHECK(vkCreatePipelineLayout(vk.device, &pipelineLayoutCreateInfo, nullptr, &app.pipelineLayout));

    createShaderVulkan(vk, "shaders/raygen.rgen.spv", &app.raygenShader);
    createShaderVulkan(vk, "shaders/chit.rchit.spv", &app.chitShader);
    createShaderVulkan(vk, "shaders/miss.rmiss.spv", &app.missShader);

    VkPipelineShaderStageCreateInfo raygenStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    raygenStage.stage = VK_SHADER_STAGE_RAYGEN_BIT_NV;
    raygenStage.module = app.raygenShader;
    raygenStage.pName = "main";

    VkPipelineShaderStageCreateInfo chitStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    chitStage.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
    chitStage.module = app.chitShader;
    chitStage.pName = "main";

    VkPipelineShaderStageCreateInfo missStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    missStage.stage = VK_SHADER_STAGE_MISS_BIT_NV;
    missStage.module = app.missShader;
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
    rayPipelineInfo.layout = app.pipelineLayout;

    VK_CHECK(vkCreateRayTracingPipelinesNV(vk.device, VK_NULL_HANDLE, 1, &rayPipelineInfo, nullptr, &app.rtPipeline));
}

void createShaderBindingTable()
{
    uint32_t sbtSize = vk.rtProps.shaderGroupHandleSize * 3;

    createBufferVulkan(vk, { sbtSize,
        VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT }, &app.sbtBuffer);

    // Put shader group handles in SBT memory
    std::vector<char> tmp(app.sbtBuffer.size);

    int numGroups = 3; // raygen, 1 hit, 1 miss
    VK_CHECK(vkGetRayTracingShaderGroupHandlesNV(vk.device, app.rtPipeline, 0, numGroups, app.sbtBuffer.size, tmp.data()));

    void* sbtBufferMemory = nullptr;
    VK_CHECK(vkMapMemory(vk.device, app.sbtBuffer.memory, 0, app.sbtBuffer.size, 0, &sbtBufferMemory));
    memcpy(sbtBufferMemory, tmp.data(), tmp.size());
    vkUnmapMemory(vk.device, app.sbtBuffer.memory);
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

    VK_CHECK(vkCreateDescriptorPool(vk.device, &descPoolCreateInfo, nullptr, &app.descriptorPool));

    VkDescriptorSetLayout setLayouts[] = {
        app.descriptorSetLayout
    };

    VkDescriptorSetAllocateInfo descSetAllocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    descSetAllocInfo.descriptorPool = app.descriptorPool;
    descSetAllocInfo.pSetLayouts = setLayouts;
    descSetAllocInfo.descriptorSetCount = 1;

    VK_CHECK(vkAllocateDescriptorSets(vk.device, &descSetAllocInfo, &app.descriptorSet));

    VkWriteDescriptorSetAccelerationStructureNV descAccelStructInfo = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV };
    descAccelStructInfo.accelerationStructureCount = 1;
    descAccelStructInfo.pAccelerationStructures = &app.scene.tlas.accelerationStructure;

    VkWriteDescriptorSet accelStructWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    accelStructWrite.pNext = &descAccelStructInfo;
    accelStructWrite.dstSet = app.descriptorSet;
    accelStructWrite.dstBinding = 0;
    accelStructWrite.descriptorCount = 1;
    accelStructWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
    accelStructWrite.pImageInfo = nullptr;
    accelStructWrite.pBufferInfo = nullptr;
    accelStructWrite.pTexelBufferView = nullptr;

    VkDescriptorImageInfo descOutputImageInfo = {};
    descOutputImageInfo.imageView = app.offscreenImage.view;
    descOutputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet resImageWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    resImageWrite.dstSet = app.descriptorSet;
    resImageWrite.dstBinding = 1;
    resImageWrite.descriptorCount = 1;
    resImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    resImageWrite.pImageInfo = &descOutputImageInfo;
    resImageWrite.pBufferInfo = nullptr;
    resImageWrite.pTexelBufferView = nullptr;

    VkDescriptorBufferInfo camdataBufferInfo = {};
    camdataBufferInfo.buffer = app.scene.cameraBuffer.buffer;
    camdataBufferInfo.offset = 0;
    camdataBufferInfo.range = app.scene.cameraBuffer.size;

    VkWriteDescriptorSet camdataBufferWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    camdataBufferWrite.dstSet = app.descriptorSet;
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
        imageMemoryBarrier1.image = app.offscreenImage.image;
        imageMemoryBarrier1.subresourceRange = subresourceRange;

        vkCmdPipelineBarrier(cmdBuffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier1);

        {
            vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, app.rtPipeline);

            vkCmdBindDescriptorSets(cmdBuffer,
                VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
                app.pipelineLayout, 0,
                1, &app.descriptorSet,
                0, 0);

            uint32_t stride = vk.rtProps.shaderGroupHandleSize;

            vkCmdTraceRaysNV(cmdBuffer,
                // raygen
                app.sbtBuffer.buffer, 0,
                // miss
                app.sbtBuffer.buffer, stride * 2, stride,
                // hit
                app.sbtBuffer.buffer, stride * 1, stride,
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
        imageMemoryBarrier3.image = app.offscreenImage.image;
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
            app.offscreenImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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

void updateCamera(const float dt)
{
    Camera& cam = app.scene.camera;

    glm::vec3 moveDelta(0.f, 0.f, 0.f);

    if (app.keysDown[GLFW_KEY_W]) moveDelta.z += 1.f;
    if (app.keysDown[GLFW_KEY_S]) moveDelta.z -= 1.f;
    if (app.keysDown[GLFW_KEY_A]) moveDelta.x -= 1.f;
    if (app.keysDown[GLFW_KEY_D]) moveDelta.x += 1.f;
    if (app.keysDown[GLFW_KEY_Q]) moveDelta.y += 1.f;
    if (app.keysDown[GLFW_KEY_E]) moveDelta.y -= 1.f;

    moveDelta *= kCameraMoveSpeed * dt;
    cameraMove(cam, moveDelta);

    cameraUpdateView(cam);


    CameraUniformData camData;
    camData.viewInverse = glm::inverse(app.scene.camera.view);
    camData.projInverse = glm::inverse(app.scene.camera.projection);

    void* mem = nullptr;
    VK_CHECK(vkMapMemory(vk.device, app.scene.cameraBuffer.memory, 0, app.scene.cameraBuffer.size, 0, &mem));

    memcpy(mem, &camData, sizeof(CameraUniformData));

    vkUnmapMemory(vk.device, app.scene.cameraBuffer.memory);
}


int main()
{
    if (!initApp())
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

    glfwSetKeyCallback(app.window, [](auto* wnd, int key, int scancode, int action, int mods) {
        if ((action == GLFW_PRESS || action == GLFW_RELEASE) && key <= GLFW_KEY_LAST)
            app.keysDown[key] = action == GLFW_PRESS;
    });

    glfwSetMouseButtonCallback(app.window, [](auto* wnd, int button, int action, int mods) {
        if ((action == GLFW_PRESS || action == GLFW_RELEASE) && button <= GLFW_MOUSE_BUTTON_LAST)
            app.mouseDown[button] = action == GLFW_PRESS;
    });

    glfwSetCursorPosCallback(app.window, [](auto* wnd, double x, double y) {
        glm::vec2 newPos = glm::vec2(float(x), float(y));

        if (app.mouseDown[GLFW_MOUSE_BUTTON_1])
        {
            glm::vec2 delta = app.cursorPos - newPos;
            cameraRotate(app.scene.camera, delta.x * kCameraRotationSpeed, -delta.y * kCameraRotationSpeed);
        }

        app.cursorPos = newPos;
    });

    double prevTime = 0.;

    while (!glfwWindowShouldClose(app.window))
    {
        double currTime = glfwGetTime();
        double dt = std::min(currTime - prevTime, 2.);
        prevTime = currTime;

        uint32_t imageIndex = 0;
        VK_CHECK(vkAcquireNextImageKHR(vk.device, vk.swapchain, UINT64_MAX, vk.semaphoreImageAcquired, nullptr, &imageIndex));

        const VkFence fence = vk.waitForFrameFences[imageIndex];
        VK_CHECK(vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX));
        vkResetFences(vk.device, 1, &fence);

        updateCamera(float(dt));

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

    shutdownApp();

    return 0;
}
