#include "pch.h"

#include "App.h"
#include "logging.h"
#include "gltfLoader.h"

static bool readVec3(const std::vector<double>& src, glm::vec3* dst,
    const glm::vec3& defaultValue = glm::vec3(0.f))
{
    if (src.size() > 0)
    {
        *dst = glm::vec3((float)src[0], (float)src[1], (float)src[2]);
        return true;
    }
    else
    {
        *dst = defaultValue;
        return false;
    }
}

static bool readVec4(const std::vector<double>& src, glm::vec4* dst,
    const glm::vec4& defaultValue = glm::vec4(0.f))
{
    if (src.size() > 0)
    {
        *dst = glm::vec4((float)src[0], (float)src[1], (float)src[2], (float)src[3]);
        return true;
    }
    else
    {
        *dst = defaultValue;
        return false;
    }
}

static bool readQuat(std::vector<double>& src, glm::quat* dst,
    const glm::quat& defaultValue = glm::quat(1.f, 0.f, 0.f, 0.f))
{
    // glm uses w, x, y, z
    if (src.size() > 0)
    {
        *dst = glm::quat((float)src[3], (float)src[0], (float)src[1], (float)src[2]);
        return true;
    }
    else
    {
        *dst = defaultValue;
        return false;
    }
};

static void loadMesh(DeviceVulkan& vk, tinygltf::Model& gltfModel,
    tinygltf::Mesh& gltfMesh, Mesh* pMesh)
{
    // Only handle single triangle primitives (for now).
    BASSERT(gltfMesh.primitives.size() == 1);

    auto& primitive = gltfMesh.primitives[0];

    BASSERT(primitive.mode == TINYGLTF_MODE_TRIANGLES);

    auto& attrs = primitive.attributes;

    int idPosition = attrs.count("POSITION") == 1 ? attrs["POSITION"] : -1;
    int idNormal = attrs.count("NORMAL") == 1 ? attrs["NORMAL"] : -1;
    int idTexcoord0 = attrs.count("TEXCOORD_0") == 1 ? attrs["TEXCOORD_0"] : -1;
    int idIndices = primitive.indices;
    int idMaterial = primitive.material;

    pMesh->materialID = idMaterial;

    BASSERT(idPosition >= 0);

    // Load positions
    {
        BASSERT(gltfModel.accessors[idPosition].componentType == TINYGLTF_COMPONENT_TYPE_FLOAT
            && gltfModel.accessors[idPosition].type == TINYGLTF_TYPE_VEC3);

        pMesh->vertexCount = (uint32_t)gltfModel.accessors[idPosition].count;

        auto& bv = gltfModel.bufferViews[gltfModel.accessors[idPosition].bufferView];

        //std::vector<glm::vec3> dbgPositions(bv.byteLength / 12);
        //memcpy(dbgPositions.data(),
        //    &gltfModel.buffers[bv.buffer].data[bv.byteOffset], bv.byteLength);

        createBufferVulkan(vk, { bv.byteLength,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &gltfModel.buffers[bv.buffer].data[bv.byteOffset] },
            &pMesh->positions);
    }

    // Load normals
    {
        BASSERT(idNormal >= 0);

        BASSERT(gltfModel.accessors[idNormal].componentType == TINYGLTF_COMPONENT_TYPE_FLOAT
            && gltfModel.accessors[idNormal].type == TINYGLTF_TYPE_VEC3);

        auto& bv = gltfModel.bufferViews[gltfModel.accessors[idNormal].bufferView];

        auto normalCount = gltfModel.accessors[idNormal].count;

        createBufferVulkan(vk, { bv.byteLength,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &gltfModel.buffers[bv.buffer].data[bv.byteOffset] },
            &pMesh->normals);
    }

    // Load indices
    {
        BASSERT(gltfModel.accessors[idIndices].componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT
            && gltfModel.accessors[idIndices].type == TINYGLTF_TYPE_SCALAR);

        uint32_t indexCount = (uint32_t)gltfModel.accessors[idIndices].count;
        pMesh->indexCount = indexCount;

        std::vector<uint16_t> indices16(indexCount);

        auto& bv = gltfModel.bufferViews[gltfModel.accessors[idIndices].bufferView];

        memcpy(indices16.data(), &gltfModel.buffers[bv.buffer].data[bv.byteOffset],
            bv.byteLength);

        std::vector<uint32_t> indices32(indices16.begin(), indices16.end());

        createBufferVulkan(vk, { indexCount * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            indices32.data() }, &pMesh->indices);
    }
}

static void loadMeshes(DeviceVulkan& vk, tinygltf::Model& model, Scene* pScene)
{
    // Load meshes
    size_t meshCount = model.meshes.size();
    BASSERT(meshCount > 0);

    pScene->meshes.resize(meshCount);

    for (size_t i = 0; i < model.meshes.size(); i++)
        loadMesh(vk, model, model.meshes[i], &pScene->meshes[i]);
}

static void loadSceneNodes(tinygltf::Model& model, Scene* pScene)
{
    // Load scene nodes
    BASSERT(model.defaultScene == 0);
    tinygltf::Scene& gltfScene = model.scenes[model.defaultScene];

    pScene->nodes.reserve(gltfScene.nodes.size());

    for (auto i : gltfScene.nodes)
    {
        auto& srcNode = model.nodes[i];

        if (srcNode.mesh == -1)
        {
            // might be a light or camera.

            if (srcNode.children.size())
            {
                auto& childNode = model.nodes[srcNode.children[0]];
                if (childNode.camera >= 0)
                {
                    readVec3(srcNode.translation, &pScene->camera.position, glm::vec3(0.f));

                    glm::quat rot1, rot2;
                    if (readQuat(srcNode.rotation, &rot1))
                    {
                        if (readQuat(childNode.rotation, &rot2))
                        {
                            cameraRotate(pScene->camera, rot1 * rot2);
                        }
                    }

                    cameraUpdateView(pScene->camera);
                }
            }

            continue;
        }

        pScene->nodes.push_back({});
        SceneNode& dstNode = pScene->nodes.back();

        dstNode.name = srcNode.name;
        dstNode.meshID = srcNode.mesh;
        dstNode.children = srcNode.children;

        readVec3(srcNode.translation, &dstNode.translation, glm::vec3(0.f));
        readVec3(srcNode.scale, &dstNode.scale, glm::vec3(1.f));
        readQuat(srcNode.rotation, &dstNode.rotation);

        if (srcNode.matrix.size() > 0)
        {
            dstNode.matrixValid = true;
            memcpy(glm::value_ptr(dstNode.matrix), srcNode.matrix.data(),
                sizeof(float) * 16);
        }
        else
        {
            dstNode.matrixValid = false;
            dstNode.matrix = glm::mat4(1.f);
        }
    }
}

static void loadMeshInstanceData(DeviceVulkan& vk, Scene* pScene)
{
    std::vector<MeshInstanceData> meshInstanceData;
    meshInstanceData.reserve(pScene->nodes.size());

    for (auto& node : pScene->nodes)
    {
        MeshInstanceData mid = {};
        mid.materialID = pScene->meshes[node.meshID].materialID;

        meshInstanceData.push_back(mid);
    }

    createBufferVulkan(vk, { sizeof(MeshInstanceData) * meshInstanceData.size(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        meshInstanceData.data() }, &pScene->meshInstanceDataBuffer);
}

static void loadMaterials(DeviceVulkan& vk, tinygltf::Model& model, Scene* pScene)
{
    // Note: Only loading PBR Metal/Roughness properties (not specular extension)
    std::vector<Material> materials;
    materials.reserve(model.materials.size());

    for (auto& m : model.materials)
    {
        Material mat = {};

        readVec4(m.pbrMetallicRoughness.baseColorFactor, &mat.baseColor);

        materials.push_back(mat);
    }

    createBufferVulkan(vk, { sizeof(Material) * materials.size(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        materials.data() }, &pScene->materialsBuffer);
}

bool loadGltfFile(DeviceVulkan& vk, const char* fn, Scene* pScene)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;

    std::string err;
    std::string warn;
    bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, fn);
    BASSERT(ret);

    if (ret == false)
    {
        DebugPrint("Error: could not load %s\n", fn);
        return false;
    }

    if (model.scenes.size() == 0)
    {
        DebugPrint("Error: no scene found in %s\n", fn);
        return false;
    }

    loadMeshes(vk, model, pScene);

    loadSceneNodes(model, pScene);

    loadMeshInstanceData(vk, pScene);

    loadMaterials(vk, model, pScene);

    return true;
}


