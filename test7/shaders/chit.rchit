#version 460
#extension GL_NV_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "shader-common.h"

const int kMaxDepth = 4;

struct InstanceData
{
    int materialID;
};

struct Material
{
    vec4 baseColorFactor;
    float metalness;
    float roughness;
    float pad[2];
};

layout(set = 0, binding = 0) uniform accelerationStructureNV Scene;
layout(set = 0, binding = 3) readonly buffer MeshInstanceDataBuffer { InstanceData d[]; } MeshInstanceData;
layout(set = 0, binding = 4) readonly buffer MaterialsBuffer { Material m[]; } Materials;

layout(set = 1, binding = 0) readonly buffer PositionsBuffer { float p[]; } Positions[];
layout(set = 2, binding = 0) readonly buffer NormalsBuffer { float n[]; } Normals[];
layout(set = 3, binding = 0) readonly buffer UvsBuffer { vec2 uv[]; } Uvs[];
layout(set = 4, binding = 0) readonly buffer IndicesBuffer { uint i[]; } Indices[];

layout(set = 5, binding = 0) uniform sampler LinearSampler;
layout(set = 5, binding = 1) uniform texture2D BaseColorTextures[];

layout(location = 0) rayPayloadInNV RayPayload PrimaryRay;

hitAttributeNV vec2 HitAttribs;

vec2 interpolate(vec2 a, vec2 b, vec2 c, vec3 barycentrics)
{
    return a * barycentrics.x + 
           b * barycentrics.y +
           c * barycentrics.z;
}

vec3 interpolate(vec3 a, vec3 b, vec3 c, vec3 barycentrics)
{
    return a * barycentrics.x + 
           b * barycentrics.y +
           c * barycentrics.z;
}

vec3 getBarycentric()
{
    vec3 b;
    b.x = 1.0 - HitAttribs.x - HitAttribs.y;
    b.y = HitAttribs.x;
    b.z = HitAttribs.y;
    return b;
}

uvec3 getFaceIndex()
{
    // BONI TODO: when do we _need_ to use nonuniformEXT ?
    // ivec3 ind = ivec3(
    //     Indices[nonuniformEXT(gl_InstanceID)].i[3 * gl_PrimitiveID],
    //     Indices[nonuniformEXT(gl_InstanceID)].i[3 * gl_PrimitiveID + 1],
    //     Indices[nonuniformEXT(gl_InstanceID)].i[3 * gl_PrimitiveID + 2]
    // );

    uvec3 f;
    f.x = Indices[gl_InstanceID].i[3 * gl_PrimitiveID + 0];
    f.y = Indices[gl_InstanceID].i[3 * gl_PrimitiveID + 1];
    f.z = Indices[gl_InstanceID].i[3 * gl_PrimitiveID + 2];
    return f;
}

vec3 getOnePosition(uint index)
{
    vec3 p;
    p.x = Positions[gl_InstanceID].p[3 * index + 0];
    p.y = Positions[gl_InstanceID].p[3 * index + 1];
    p.z = Positions[gl_InstanceID].p[3 * index + 2];
    return p;
}

vec3 getGeometricNormalWS(uvec3 faceIndex)
{
    vec3 p0 = getOnePosition(faceIndex.x);
    vec3 p1 = getOnePosition(faceIndex.y);
    vec3 p2 = getOnePosition(faceIndex.z);

    vec3 geoN = normalize(cross(p1 - p0, p2 - p0));

    return normalize(vec3(gl_ObjectToWorldNV * vec4(geoN, 0.)));
}

vec3 getOneNormal(uint index)
{
    vec3 n;
    n.x = Normals[gl_InstanceID].n[3 * index + 0];
    n.y = Normals[gl_InstanceID].n[3 * index + 1];
    n.z = Normals[gl_InstanceID].n[3 * index + 2];
    return n;
}

vec3 getNormalWS(uvec3 faceIndex, vec3 barycentric)
{
    vec3 n0 = getOneNormal(faceIndex.x);
    vec3 n1 = getOneNormal(faceIndex.y);
    vec3 n2 = getOneNormal(faceIndex.z);

    vec3 n = normalize(interpolate(n0, n1, n2, barycentric));

    vec3 N = normalize(vec3(gl_ObjectToWorldNV * vec4(n, 0.)));

    return N;
}

vec2 getUv(uvec3 faceIndex, vec3 barycentric)
{
    vec2 uv0 = Uvs[gl_InstanceID].uv[faceIndex.x];
    vec2 uv1 = Uvs[gl_InstanceID].uv[faceIndex.y];
    vec2 uv2 = Uvs[gl_InstanceID].uv[faceIndex.z];

    vec2 uv = interpolate(uv0, uv1, uv2, barycentric);
    return uv;
}

void main()
{
    uvec3 faceIndex = getFaceIndex();
    vec3 barycentric = getBarycentric();

    vec3 baseColor = vec3(1., 1., 1.);

    int materialID = MeshInstanceData.d[gl_InstanceID].materialID;
    vec2 uv = getUv(faceIndex, barycentric);

    // BONI TODO: remove these if checks by having defaults
    if (materialID >= 0)
    {
        vec3 baseColorSample = texture(sampler2D(BaseColorTextures[materialID], LinearSampler), uv).rgb;
        baseColorSample = srgbToLinear(baseColorSample);

        Material m = Materials.m[materialID];
        baseColor = baseColorSample * m.baseColorFactor.rgb;

        if (m.roughness == 0.)
            baseColor = vec3(1,0,0);

        if (m.baseColorFactor.a < 0.5)
            baseColor = vec3(0,0,0);
    }

    vec3 geoN = getGeometricNormalWS(faceIndex);

    vec3 N = getNormalWS(faceIndex, barycentric);

    bool inside = false;

    if (dot(geoN, gl_WorldRayDirectionNV) > 0)
    {
        N = -N;
        inside = true;
    }

    // if (dot(N, gl_WorldRayDirectionNV) > 0)
    // {
    //     PrimaryRay.color_distance = vec4(vec3(0,0,1), gl_HitTNV);
    //     PrimaryRay.normal = vec4(N, 0.);
    //     return;
    // }

    vec3 L = normalize(vec3(1., 1., 1.));

    float ndotl = max(0., dot(N, L));

    vec3 color = vec3(ndotl * baseColor + 0.1);

    if (inside)
    {
        color = vec3(0,0,1);
    }

    PrimaryRay.depth += 1;

    if (baseColor.r < 1.0 && PrimaryRay.depth < kMaxDepth)
    {
        vec3 hitPos = gl_WorldRayOriginNV + gl_WorldRayDirectionNV * gl_HitTNV;
        vec3 origin = hitPos + N * 0.01;
        vec3 dir = reflect(gl_WorldRayDirectionNV, N);

        uint flags = gl_RayFlagsOpaqueNV;

        traceNV(Scene, flags, 0xFF, 0, 0, 0, origin, 0.001, dir, 1000, 0);

        PrimaryRay.color_distance.rgb *= color;
    }
    else
    {
        PrimaryRay.color_distance = vec4(color, gl_HitTNV);
        PrimaryRay.normal = vec4(N, 0.);
    }

    PrimaryRay.depth -= 1;
}

