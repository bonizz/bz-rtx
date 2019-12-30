#pragma once

#include "DeviceVulkan.h"
#include "camera.h"


// https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/chap33.html#acceleration-structure
struct VkGeometryInstance
{
    float transform[12];
    uint32_t instanceCustomIndex : 24; // gl_InstanceCustomIndexNV
    uint32_t mask : 8;
    uint32_t instanceOffset : 24;
    uint32_t flags : 8;
    uint64_t accelerationStructureHandle;
};

struct Mesh
{
    uint32_t vertexCount;
    uint32_t indexCount;

    int materialID;

    BufferVulkan positions;
    BufferVulkan normals;
    BufferVulkan uvs;
    BufferVulkan indices;

    AccelerationStructureVulkan blas;
};

// BONI NOTE: keep this padded to 16 bytes
struct Material
{
    glm::vec4 baseColor;
    uint32_t baseColorTextureID;
    uint32_t pad[3];
};

struct CameraUniformData
{
    glm::mat4 viewInverse;
    glm::mat4 projInverse;
};

struct SceneNode
{
    std::string name;

    glm::vec3 translation;
    glm::vec3 scale;
    glm::quat rotation;

    glm::mat4 matrix;

    std::vector<int> children;

    int meshID;
    bool matrixValid;
};

struct MeshInstanceData
{
    int materialID;
};

struct Scene
{
    std::vector<SceneNode> nodes;

    std::vector<Mesh> meshes;

    std::vector<VkDescriptorBufferInfo> normalsBufferInfos;
    std::vector<VkDescriptorBufferInfo> uvsBufferInfos;
    std::vector<VkDescriptorBufferInfo> indicesBufferInfos;

    std::vector<ImageVulkan> textures;
    std::vector<VkDescriptorImageInfo> textureInfos;


    BufferVulkan cameraBuffer;
    BufferVulkan meshInstanceDataBuffer;
    BufferVulkan materialsBuffer;


    VkSampler linearSampler;

    Camera camera;

    AccelerationStructureVulkan topLevelStruct;
};

struct App
{
    GLFWwindow* window;

    ImageVulkan offscreenImage;

    VkShaderModule raygenShader;
    VkShaderModule chitShader;
    VkShaderModule shadowChitShader;
    VkShaderModule missShader;
    VkShaderModule shadowMissShader;

    BufferVulkan instancesBuffer;
    BufferVulkan sbtBuffer;

    VkPipelineLayout pipelineLayout;
    VkPipeline rtPipeline;

    VkDescriptorPool descriptorPool;
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
    std::vector<VkDescriptorSet> descriptorSets;

    bool keysDown[GLFW_KEY_LAST + 1];
    bool mouseDown[GLFW_MOUSE_BUTTON_LAST + 1];
    glm::vec2 cursorPos;

    Scene scene;
};

