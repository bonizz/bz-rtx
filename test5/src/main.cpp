
#include "pch.h"

#include "App.h"
#include "logging.h"
#include "gltfLoader.h"

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
    destroyBufferVulkan(vk, app.scene.materialsBuffer);
    destroyBufferVulkan(vk, app.scene.meshInstanceDataBuffer);

    {
        for (auto& mesh : app.scene.meshes)
        {
            destroyBufferVulkan(vk, mesh.positions);
            destroyBufferVulkan(vk, mesh.normals);
            destroyBufferVulkan(vk, mesh.uvs);
            destroyBufferVulkan(vk, mesh.indices);

            destroyAccelerationStructure(vk, mesh.blas);
        }
    }

    vkDestroySampler(vk.device, app.scene.linearSampler, nullptr);
    for (auto& img : app.scene.textures)
        destroyImageVulkan(vk, img);

    destroyAccelerationStructure(vk, app.scene.topLevelStruct);

    vkDestroyShaderModule(vk.device, app.raygenShader, nullptr);
    vkDestroyShaderModule(vk.device, app.chitShader, nullptr);
    vkDestroyShaderModule(vk.device, app.shadowChitShader, nullptr);
    vkDestroyShaderModule(vk.device, app.missShader, nullptr);
    vkDestroyShaderModule(vk.device, app.shadowMissShader, nullptr);

    vkDestroyDescriptorPool(vk.device, app.descriptorPool, nullptr);

    destroyBufferVulkan(vk, app.sbtBuffer);

    vkDestroyPipeline(vk.device, app.rtPipeline, nullptr);
    vkDestroyPipelineLayout(vk.device, app.pipelineLayout, nullptr);

    for (auto& dsl : app.descriptorSetLayouts)
        vkDestroyDescriptorSetLayout(vk.device, dsl, nullptr);

    destroyBufferVulkan(vk, app.instancesBuffer);
    destroyImageVulkan(vk, app.offscreenImage);

    destroyDeviceVulkan(vk);

    glfwDestroyWindow(app.window);
    glfwTerminate();
}

void createScene()
{
    //bool res = loadGltfFile("../data/colored-spheres.gltf", &app.scene);
    //bool res = loadGltfFile("../data/shadow-test2.gltf", &app.scene);
    //bool res = loadGltfFile(vk, "../data/misc-boxes.gltf", &app.scene);
    bool res = loadGltfFile(vk, "../data/christmas-spheres/christmas-sphere.gltf", &app.scene);
    BASSERT(res);

    // BONI TODO: this doesn't handle child nodes
    size_t meshCount = app.scene.meshes.size();
    size_t nodesCount = app.scene.nodes.size();

    std::vector<VkGeometryNV> geometries(meshCount); // needs to exist when blas is built
    std::vector<VkGeometryInstance> instances(meshCount);

    app.scene.normalsBufferInfos.resize(meshCount);
    app.scene.uvsBufferInfos.resize(meshCount);
    app.scene.indicesBufferInfos.resize(nodesCount);

    for (size_t i = 0; i < meshCount; i++)
    {
        Mesh& mesh = app.scene.meshes[i];
        VkGeometryNV& geometry = geometries[i];

        geometry = { VK_STRUCTURE_TYPE_GEOMETRY_NV };
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_NV;
        geometry.geometry.triangles = { VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV };
        geometry.geometry.triangles.vertexData = mesh.positions.buffer;
        geometry.geometry.triangles.vertexOffset = 0;
        geometry.geometry.triangles.vertexCount = mesh.vertexCount;
        geometry.geometry.triangles.vertexStride = sizeof(float) * 3;
        geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        geometry.geometry.triangles.indexData = mesh.indices.buffer;
        geometry.geometry.triangles.indexOffset = 0;
        geometry.geometry.triangles.indexCount = mesh.indexCount;
        geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32; // BONI TODO: switch to uint16
        geometry.geometry.triangles.transformData = VK_NULL_HANDLE;
        geometry.geometry.triangles.transformOffset = 0;
        geometry.geometry.aabbs = { VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV };
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV;

        createAccelerationStructureVulkan(vk, {
            VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV,
            1, 0, &geometry }, &mesh.blas);
    }

    for (size_t i = 0; i < nodesCount; i++)
    {
        SceneNode& node = app.scene.nodes[i];

        // BONI TODO: handle child nodes
        BASSERT(node.children.size() == 0);

        Mesh& mesh = app.scene.meshes[node.meshID];

        glm::mat4 TRS;

        if (node.matrixValid)
        {
            TRS = node.matrix;
        }
        else
        {
            glm::mat4 T = glm::translate(glm::mat4(1.f), node.translation);
            glm::mat4 R = glm::toMat4(node.rotation);
            glm::mat4 S = glm::scale(glm::mat4(1.f), node.scale);
            TRS = T * R * S;
        }

        TRS = glm::transpose(TRS);

        float transform[12] = {};
        memcpy(transform, glm::value_ptr(TRS), sizeof(transform));

        VkGeometryInstance& instance = instances[i];
        memcpy(instance.transform, transform, sizeof(transform));
        instance.instanceCustomIndex = 0; // We can use gl_InstanceID if we're just using `i` here.
        instance.mask = 0xFF;
        instance.instanceOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;
        instance.accelerationStructureHandle = mesh.blas.handle;

        VkDescriptorBufferInfo& normalsBufferInfo = app.scene.normalsBufferInfos[i];
        normalsBufferInfo.buffer = mesh.normals.buffer;
        normalsBufferInfo.offset = 0;
        normalsBufferInfo.range = mesh.normals.size;

        VkDescriptorBufferInfo& uvsBufferInfo = app.scene.uvsBufferInfos[i];
        uvsBufferInfo.buffer = mesh.uvs.buffer;
        uvsBufferInfo.offset = 0;
        uvsBufferInfo.range = mesh.uvs.size;

        VkDescriptorBufferInfo& indicesBufferInfo = app.scene.indicesBufferInfos[i];
        indicesBufferInfo.buffer = mesh.indices.buffer;
        indicesBufferInfo.offset = 0;
        indicesBufferInfo.range = mesh.indices.size;
    }

    createBufferVulkan(vk, { instances.size() * sizeof(VkGeometryInstance),
        VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        instances.data() }, &app.instancesBuffer);

    createAccelerationStructureVulkan(vk, {
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV,
        0, (uint32_t)instances.size(), nullptr }, &app.scene.topLevelStruct);

    VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
    memoryRequirementsInfo.sType = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV };
    memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;

    VkDeviceSize scratchBufferSize = 0;

    for (auto& mesh : app.scene.meshes)
    {
        memoryRequirementsInfo.accelerationStructure = mesh.blas.accelerationStructure;

        VkMemoryRequirements2 memReqBlas;
        vkGetAccelerationStructureMemoryRequirementsNV(vk.device, &memoryRequirementsInfo, &memReqBlas);

        scratchBufferSize = std::max(scratchBufferSize, memReqBlas.memoryRequirements.size);
    }

    VkMemoryRequirements2 memReqTlas;
    memoryRequirementsInfo.accelerationStructure = app.scene.topLevelStruct.accelerationStructure;
    vkGetAccelerationStructureMemoryRequirementsNV(vk.device, &memoryRequirementsInfo, &memReqTlas);

    scratchBufferSize = std::max(scratchBufferSize, memReqTlas.memoryRequirements.size);

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

    for (auto& mesh : app.scene.meshes)
    {
        vkCmdBuildAccelerationStructureNV(cmdBuffer, &mesh.blas.accelerationStructureInfo,
            VK_NULL_HANDLE, 0, VK_FALSE, mesh.blas.accelerationStructure,
            VK_NULL_HANDLE, scratchBuffer.buffer, 0);

        vkCmdPipelineBarrier(cmdBuffer,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV,
            0, 1, &memoryBarrier, 0, 0, 0, 0);
    }

    vkCmdBuildAccelerationStructureNV(cmdBuffer, &app.scene.topLevelStruct.accelerationStructureInfo,
            app.instancesBuffer.buffer, 0, VK_FALSE, app.scene.topLevelStruct.accelerationStructure,
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

void setupDefaultCamera()
{
    createBufferVulkan(vk, { sizeof(CameraUniformData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT },
        &app.scene.cameraBuffer);

    Camera& cam = app.scene.camera;

    cam.viewport = { 0, 0, kWindowWidth, kWindowHeight };
    cam.position = glm::vec3(0, 0, 2.5f);
    cam.up = glm::vec3(0, 1, 0);
    cam.forward = glm::vec3(0, 0, -1.f);
    cameraUpdateView(cam);
    cameraUpdateProjection(cam);
}

void createSamplers()
{
    VkSamplerCreateInfo samplerCreateInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; // BONI TODO: VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.maxLod = FLT_MAX;

    VK_CHECK(vkCreateSampler(vk.device, &samplerCreateInfo, nullptr, &app.scene.linearSampler));
}

void createDescriptorSetLayouts()
{
    // Set 0:
    //   Binding 0 -> AS
    //   Binding 1 -> output image
    //   Binding 2 -> camera data
    //   Binding 3 -> MeshInstanceData[] (per instance)
    //   Binding 4 -> Material[]

    // Set 1: float[3] normalsArrays[] (per instance)
    //   Binding 0-N, where N = mesh count

    // Set 2: vec2 uvsArray[] (per instance)
    //   Binding 0-N, where N = mesh count

    // Set 3: uint indicesArrays[] (per instance)
    //   Binding 0-N, where N = mesh count

    // Set 4: (optional: only when textures present)
    //   Binding 0: linear sampler
    //   Binding 1: texture2d Textures[]
    const int numDescriptorSets = (app.scene.textures.size() > 0) ? 5 : 4;

    app.descriptorSetLayouts.resize(numDescriptorSets);

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

    VkDescriptorSetLayoutBinding meshInstanceDataLayoutBinding = {};
    meshInstanceDataLayoutBinding.binding = 3;
    meshInstanceDataLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    meshInstanceDataLayoutBinding.descriptorCount = 1;
    meshInstanceDataLayoutBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;

    VkDescriptorSetLayoutBinding materialLayoutBinding = {};
    materialLayoutBinding.binding = 4;
    materialLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialLayoutBinding.descriptorCount = 1;
    materialLayoutBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;

    VkDescriptorSetLayoutBinding set0Bindings[] = {
        asLayoutBinding,
        outputImageLayoutBinding,
        cameraDataLayoutBinding,
        meshInstanceDataLayoutBinding,
        materialLayoutBinding
    };

    VkDescriptorSetLayoutCreateInfo set0LayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    set0LayoutInfo.flags = 0;
    set0LayoutInfo.bindingCount = (uint32_t)std::size(set0Bindings);
    set0LayoutInfo.pBindings = set0Bindings;

    VK_CHECK(vkCreateDescriptorSetLayout(vk.device, &set0LayoutInfo, nullptr,
        &app.descriptorSetLayouts[0]));

    // Set 1: vec4 normalsArrays[]
    //   Binding 0-N, where N = mesh count

    const VkDescriptorBindingFlagsEXT flags = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT;

    VkDescriptorSetLayoutBindingFlagsCreateInfoEXT bindingFlags = {};
    bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
    bindingFlags.pBindingFlags = &flags;
    bindingFlags.bindingCount = 1;

    VkDescriptorSetLayoutBinding storageBufferBinding = {};
    storageBufferBinding.binding = 0;
    storageBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    storageBufferBinding.descriptorCount = (uint32_t)app.scene.meshes.size();
    storageBufferBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;

    VkDescriptorSetLayoutCreateInfo set1LayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    set1LayoutInfo.pNext = &bindingFlags;
    set1LayoutInfo.flags = 0;
    set1LayoutInfo.bindingCount = 1;
    set1LayoutInfo.pBindings = &storageBufferBinding;

    VK_CHECK(vkCreateDescriptorSetLayout(vk.device, &set1LayoutInfo, nullptr,
        &app.descriptorSetLayouts[1]));

    // Set 2: float[2] uvsArray[] (per instance)
    //   Binding 0-N, where N = mesh count

    VK_CHECK(vkCreateDescriptorSetLayout(vk.device, &set1LayoutInfo, nullptr,
        &app.descriptorSetLayouts[2]));

    // Set 3: uint indicesArrays[] (per instance)
    //   Binding 0-N, where N = mesh count

    VK_CHECK(vkCreateDescriptorSetLayout(vk.device, &set1LayoutInfo, nullptr,
        &app.descriptorSetLayouts[3]));

    if (app.scene.textures.size() > 0)
    {
        // Set 4:
        //   Binding 0: linear sampler
        //   Binding 1: texture2d Textures[]

        VkDescriptorSetLayoutBinding linearSamplerBinding = {};
        linearSamplerBinding.binding = 0;
        linearSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        linearSamplerBinding.descriptorCount = 1;
        linearSamplerBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;

        VkDescriptorSetLayoutBinding texturesBinding = {};
        texturesBinding.binding = 1;
        texturesBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        texturesBinding.descriptorCount = (uint32_t)app.scene.textures.size();
        texturesBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;

        VkDescriptorSetLayoutBinding set4Bindings[] = {
            linearSamplerBinding,
            texturesBinding
        };

        VkDescriptorSetLayoutCreateInfo set4LayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        set4LayoutInfo.flags = 0;
        set4LayoutInfo.bindingCount = (uint32_t)std::size(set4Bindings);
        set4LayoutInfo.pBindings = set4Bindings;

        VK_CHECK(vkCreateDescriptorSetLayout(vk.device, &set4LayoutInfo, nullptr,
            &app.descriptorSetLayouts[4]));
    }
}

void createRaytracingPipeline()
{
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutCreateInfo.flags = 0;
    pipelineLayoutCreateInfo.setLayoutCount = (uint32_t)app.descriptorSetLayouts.size();
    pipelineLayoutCreateInfo.pSetLayouts = app.descriptorSetLayouts.data();
    pipelineLayoutCreateInfo.pushConstantRangeCount = 0;

    VK_CHECK(vkCreatePipelineLayout(vk.device, &pipelineLayoutCreateInfo, nullptr, &app.pipelineLayout));

    createShaderVulkan(vk, "shaders/raygen.rgen.spv", &app.raygenShader);
    createShaderVulkan(vk, "shaders/chit.rchit.spv", &app.chitShader);
    createShaderVulkan(vk, "shaders/shadow-chit.rchit.spv", &app.shadowChitShader);
    createShaderVulkan(vk, "shaders/miss.rmiss.spv", &app.missShader);
    createShaderVulkan(vk, "shaders/shadow-miss.rmiss.spv", &app.shadowMissShader);

    VkPipelineShaderStageCreateInfo raygenStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    raygenStage.stage = VK_SHADER_STAGE_RAYGEN_BIT_NV;
    raygenStage.module = app.raygenShader;
    raygenStage.pName = "main";

    VkPipelineShaderStageCreateInfo chitStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    chitStage.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
    chitStage.module = app.chitShader;
    chitStage.pName = "main";

    VkPipelineShaderStageCreateInfo shadowChitStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    shadowChitStage.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
    shadowChitStage.module = app.shadowChitShader;
    shadowChitStage.pName = "main";

    VkPipelineShaderStageCreateInfo missStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    missStage.stage = VK_SHADER_STAGE_MISS_BIT_NV;
    missStage.module = app.missShader;
    missStage.pName = "main";

    VkPipelineShaderStageCreateInfo shadowMissStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    shadowMissStage.stage = VK_SHADER_STAGE_MISS_BIT_NV;
    shadowMissStage.module = app.shadowMissShader;
    shadowMissStage.pName = "main";

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

    VkRayTracingShaderGroupCreateInfoNV shadowChitGroup = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV };
    shadowChitGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV;
    shadowChitGroup.generalShader = VK_SHADER_UNUSED_NV;
    shadowChitGroup.closestHitShader = 2;
    shadowChitGroup.anyHitShader = VK_SHADER_UNUSED_NV;
    shadowChitGroup.intersectionShader = VK_SHADER_UNUSED_NV;

    VkRayTracingShaderGroupCreateInfoNV missGroup = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV };
    missGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
    missGroup.generalShader = 3;
    missGroup.closestHitShader = VK_SHADER_UNUSED_NV;
    missGroup.anyHitShader = VK_SHADER_UNUSED_NV;
    missGroup.intersectionShader = VK_SHADER_UNUSED_NV;

    VkRayTracingShaderGroupCreateInfoNV shadowMissGroup = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV };
    shadowMissGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
    shadowMissGroup.generalShader = 4;
    shadowMissGroup.closestHitShader = VK_SHADER_UNUSED_NV;
    shadowMissGroup.anyHitShader = VK_SHADER_UNUSED_NV;
    shadowMissGroup.intersectionShader = VK_SHADER_UNUSED_NV;

    VkPipelineShaderStageCreateInfo stages[] = {
        raygenStage,
        chitStage,
        shadowChitStage,
        missStage,
        shadowMissStage
    };

    VkRayTracingShaderGroupCreateInfoNV groups[] = {
        raygenGroup,
        chitGroup,
        shadowChitGroup,
        missGroup,
        shadowMissGroup
    };

    VkRayTracingPipelineCreateInfoNV rayPipelineInfo = { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV };
    rayPipelineInfo.stageCount = (uint32_t)std::size(stages);
    rayPipelineInfo.pStages = stages;
    rayPipelineInfo.groupCount = (uint32_t)std::size(groups);
    rayPipelineInfo.pGroups = groups;
    rayPipelineInfo.maxRecursionDepth = 1; // no recursion needed now
    rayPipelineInfo.layout = app.pipelineLayout;

    VK_CHECK(vkCreateRayTracingPipelinesNV(vk.device, VK_NULL_HANDLE, 1, &rayPipelineInfo, nullptr, &app.rtPipeline));
}

void createShaderBindingTable()
{
    int numGroups = 1 + 2 + 2; // raygen, 2 hit, 2 miss

    uint32_t sbtSize = vk.rtProps.shaderGroupHandleSize * numGroups;

    createBufferVulkan(vk, { sbtSize,
        VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT }, &app.sbtBuffer);

    // Put shader group handles in SBT memory
    std::vector<char> tmp(app.sbtBuffer.size);

    VK_CHECK(vkGetRayTracingShaderGroupHandlesNV(vk.device, app.rtPipeline, 0, numGroups, app.sbtBuffer.size, tmp.data()));

    void* sbtBufferMemory = nullptr;
    VK_CHECK(vkMapMemory(vk.device, app.sbtBuffer.memory, 0, app.sbtBuffer.size, 0, &sbtBufferMemory));
    memcpy(sbtBufferMemory, tmp.data(), tmp.size());
    vkUnmapMemory(vk.device, app.sbtBuffer.memory);
}

void createDescriptorSets()
{
    uint32_t meshCount = (uint32_t)app.scene.meshes.size();

    app.descriptorSets.resize(app.descriptorSetLayouts.size());

    std::vector<VkDescriptorPoolSize> poolSizes = {
        // set 0
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 1 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }, // camera data
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }, // MeshInstanceData[]
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }, // Material[]
        // set 1
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, meshCount }, // normals
        // set 2
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, meshCount }, // uvs
        // set 3
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, meshCount }, // indices
    };

    if (app.scene.textures.size() > 0)
    {
        // set 4
        poolSizes.push_back({ VK_DESCRIPTOR_TYPE_SAMPLER, 1 }); // linear sampler

        poolSizes.push_back({ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            (uint32_t)app.scene.textures.size() }); // texture2D[]
    }

    VkDescriptorPoolCreateInfo descPoolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    descPoolCreateInfo.poolSizeCount = (uint32_t)std::size(poolSizes);
    descPoolCreateInfo.pPoolSizes = poolSizes.data();
    descPoolCreateInfo.maxSets = (uint32_t)app.descriptorSetLayouts.size();

    VK_CHECK(vkCreateDescriptorPool(vk.device, &descPoolCreateInfo, nullptr, &app.descriptorPool));

    std::vector<uint32_t> variableDescriptorCounts = {
        1, // set 0
        meshCount, // set 1: normals
        meshCount, // set 2: uvs
        meshCount, // set 3: indices
    };

    if (app.scene.textures.size() > 0)
    {
        variableDescriptorCounts.push_back(1); // set 4 sampler/textures
    }


    VkDescriptorSetVariableDescriptorCountAllocateInfoEXT variableDescriptorCountInfo = {};
    variableDescriptorCountInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
    variableDescriptorCountInfo.descriptorSetCount = (uint32_t)std::size(variableDescriptorCounts);
    variableDescriptorCountInfo.pDescriptorCounts = variableDescriptorCounts.data();

    VkDescriptorSetAllocateInfo descSetAllocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    descSetAllocInfo.pNext = &variableDescriptorCountInfo;
    descSetAllocInfo.descriptorPool = app.descriptorPool;
    descSetAllocInfo.descriptorSetCount = (uint32_t)app.descriptorSetLayouts.size();
    descSetAllocInfo.pSetLayouts = app.descriptorSetLayouts.data();

    VK_CHECK(vkAllocateDescriptorSets(vk.device, &descSetAllocInfo, app.descriptorSets.data()));

    VkWriteDescriptorSetAccelerationStructureNV descAccelStructInfo = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV };
    descAccelStructInfo.accelerationStructureCount = 1;
    descAccelStructInfo.pAccelerationStructures = &app.scene.topLevelStruct.accelerationStructure;

    VkWriteDescriptorSet accelStructWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    accelStructWrite.pNext = &descAccelStructInfo;
    accelStructWrite.dstSet = app.descriptorSets[0];
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
    resImageWrite.dstSet = app.descriptorSets[0];
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
    camdataBufferWrite.dstSet = app.descriptorSets[0];
    camdataBufferWrite.dstBinding = 2;
    camdataBufferWrite.descriptorCount = 1;
    camdataBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    camdataBufferWrite.pImageInfo = nullptr;
    camdataBufferWrite.pBufferInfo = &camdataBufferInfo;
    camdataBufferWrite.pTexelBufferView = nullptr;

    VkDescriptorBufferInfo meshInstanceDataBufferInfo = {};
    meshInstanceDataBufferInfo.buffer = app.scene.meshInstanceDataBuffer.buffer;
    meshInstanceDataBufferInfo.offset = 0;
    meshInstanceDataBufferInfo.range = app.scene.meshInstanceDataBuffer.size;

    VkWriteDescriptorSet meshInstanceDataBufferWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    meshInstanceDataBufferWrite.dstSet = app.descriptorSets[0];
    meshInstanceDataBufferWrite.dstBinding = 3;
    meshInstanceDataBufferWrite.descriptorCount = 1;
    meshInstanceDataBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    meshInstanceDataBufferWrite.pImageInfo = nullptr;
    meshInstanceDataBufferWrite.pBufferInfo = &meshInstanceDataBufferInfo;
    meshInstanceDataBufferWrite.pTexelBufferView = nullptr;

    VkDescriptorBufferInfo materialBufferInfo = {};
    materialBufferInfo.buffer = app.scene.materialsBuffer.buffer;
    materialBufferInfo.offset = 0;
    materialBufferInfo.range = app.scene.materialsBuffer.size;

    VkWriteDescriptorSet materialBufferWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    materialBufferWrite.dstSet = app.descriptorSets[0];
    materialBufferWrite.dstBinding = 4;
    materialBufferWrite.descriptorCount = 1;
    materialBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialBufferWrite.pImageInfo = nullptr;
    materialBufferWrite.pBufferInfo = &materialBufferInfo;
    materialBufferWrite.pTexelBufferView = nullptr;

    VkWriteDescriptorSet normalsBufferWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    normalsBufferWrite.dstSet = app.descriptorSets[1];
    normalsBufferWrite.dstBinding = 0;
    normalsBufferWrite.descriptorCount = meshCount;
    normalsBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    normalsBufferWrite.pImageInfo = nullptr;
    normalsBufferWrite.pBufferInfo = app.scene.normalsBufferInfos.data();
    normalsBufferWrite.pTexelBufferView = nullptr;

    VkWriteDescriptorSet uvsBufferWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    uvsBufferWrite.dstSet = app.descriptorSets[2];
    uvsBufferWrite.dstBinding = 0;
    uvsBufferWrite.descriptorCount = meshCount;
    uvsBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    uvsBufferWrite.pImageInfo = nullptr;
    uvsBufferWrite.pBufferInfo = app.scene.uvsBufferInfos.data();
    uvsBufferWrite.pTexelBufferView = nullptr;

    VkWriteDescriptorSet indicesBufferWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    indicesBufferWrite.dstSet = app.descriptorSets[3];
    indicesBufferWrite.dstBinding = 0;
    indicesBufferWrite.descriptorCount = meshCount;
    indicesBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    indicesBufferWrite.pImageInfo = nullptr;
    indicesBufferWrite.pBufferInfo = app.scene.indicesBufferInfos.data();
    indicesBufferWrite.pTexelBufferView = nullptr;

    std::vector<VkWriteDescriptorSet> descriptorWrites = {
        accelStructWrite,
        resImageWrite,
        camdataBufferWrite,
        meshInstanceDataBufferWrite,
        materialBufferWrite,
        normalsBufferWrite,
        uvsBufferWrite,
        indicesBufferWrite
    };

    if (app.scene.textures.size() > 0)
    {
        VkDescriptorImageInfo linearSamplerImageInfo = {};
        linearSamplerImageInfo.sampler = app.scene.linearSampler;

        VkWriteDescriptorSet samplerBufferWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        samplerBufferWrite.dstSet = app.descriptorSets[4];
        samplerBufferWrite.dstBinding = 0;
        samplerBufferWrite.descriptorCount = 1;
        samplerBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        samplerBufferWrite.pImageInfo = &linearSamplerImageInfo;
        samplerBufferWrite.pBufferInfo = nullptr;
        samplerBufferWrite.pTexelBufferView = nullptr;

        VkWriteDescriptorSet texturesBufferWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        texturesBufferWrite.dstSet = app.descriptorSets[4];
        texturesBufferWrite.dstBinding = 1;
        texturesBufferWrite.descriptorCount = (uint32_t)app.scene.textures.size();
        texturesBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        texturesBufferWrite.pImageInfo = app.scene.textureInfos.data();
        texturesBufferWrite.pBufferInfo = nullptr;
        texturesBufferWrite.pTexelBufferView = nullptr;

        descriptorWrites.push_back(samplerBufferWrite);
        descriptorWrites.push_back(texturesBufferWrite);
    }

    vkUpdateDescriptorSets(vk.device, (uint32_t)std::size(descriptorWrites),
        descriptorWrites.data(), 0, VK_NULL_HANDLE);
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
                (uint32_t)app.descriptorSets.size(), app.descriptorSets.data(),
                0, 0);

            uint32_t stride = vk.rtProps.shaderGroupHandleSize;

            uint32_t hitOffset = stride * 1;  // directly after raygen
            uint32_t missOffset = stride * 3; // after raygen and 2 hit groups

            vkCmdTraceRaysNV(cmdBuffer,
                // raygen
                app.sbtBuffer.buffer, 0,
                // miss
                app.sbtBuffer.buffer, missOffset, stride,
                // hit
                app.sbtBuffer.buffer, hitOffset, stride,
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
    if (app.keysDown[GLFW_KEY_Q]) moveDelta.y -= 1.f;
    if (app.keysDown[GLFW_KEY_E]) moveDelta.y += 1.f;

    const bool shiftPressed = app.keysDown[GLFW_KEY_LEFT_SHIFT] || app.keysDown[GLFW_KEY_RIGHT_SHIFT];
    const float shiftSpeed = shiftPressed ? 6.f : 0.f;

    moveDelta *= (kCameraMoveSpeed + shiftSpeed) * dt;
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

    setupDefaultCamera();

    createScene();

    createSamplers();
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
            cameraRotate(app.scene.camera, delta.x * kCameraRotationSpeed, delta.y * kCameraRotationSpeed);
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
        if (app.keysDown[GLFW_KEY_ESCAPE]) break;
    }

    shutdownApp();

    return 0;
}
