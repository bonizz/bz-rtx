#version 460
#extension GL_NV_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "shader-common.h"

// BONI note: I can use this structure and the Face elements will get tightly packed
//  without padding.  If I put a uvec3 within this struct, however, padding will be added.
// struct Face
// {
//     uint i0;
//     uint i1;
//     uint i2;
// };
// layout(set = 2, binding = 0) readonly buffer IndicesBuffer { uint i[]; } Indices[];

layout(set = 1, binding = 0) readonly buffer NormalsBuffer { float n[]; } Normals[];
layout(set = 2, binding = 0) readonly buffer IndicesBuffer { uint i[]; } Indices[];

layout(location = 0) rayPayloadInNV RayPayload PrimaryRay;

hitAttributeNV vec2 HitAttribs;

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

vec3 getOneNormal(uint index)
{
    // vec3 n0 = Normals[nonuniformEXT(gl_InstanceID)].n[ind.x].xyz;
    // vec3 n1 = Normals[nonuniformEXT(gl_InstanceID)].n[ind.y].xyz;
    // vec3 n2 = Normals[nonuniformEXT(gl_InstanceID)].n[ind.z].xyz;

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

void main()
{
    uvec3 faceIndex = getFaceIndex();
    vec3 barycentric = getBarycentric();

    vec3 N = getNormalWS(faceIndex, barycentric);

    vec3 L = normalize(vec3(1., 1., 1.));

    float ndotl = max(0., dot(N, L));

    vec3 color = vec3(ndotl + 0.1);

    PrimaryRay.color_distance = vec4(color, gl_HitTNV);
    PrimaryRay.normal = vec4(N, 0.);
}

