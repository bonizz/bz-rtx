#version 460
#extension GL_NV_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "shader-common.h"

layout(set = 1, binding = 0) readonly buffer NormalsBuffer { vec4 n[]; } Normals[];
layout(set = 2, binding = 0) readonly buffer IndicesBuffer { uint i[]; } Indices[];

layout(location = 0) rayPayloadInNV RayPayload PrimaryRay;

hitAttributeNV vec2 HitAttribs;

vec3 interpolate(vec3 a, vec3 b, vec3 c, vec3 barycentrics)
{
    return a * barycentrics.x + 
           b * barycentrics.y +
           c * barycentrics.z;
}

void main()
{
    ivec3 ind = ivec3(
        Indices[nonuniformEXT(gl_InstanceCustomIndexNV)].i[3 * gl_PrimitiveID],
        Indices[nonuniformEXT(gl_InstanceCustomIndexNV)].i[3 * gl_PrimitiveID + 1],
        Indices[nonuniformEXT(gl_InstanceCustomIndexNV)].i[3 * gl_PrimitiveID + 2]
    );

    vec3 barycentrics = vec3(1.0 - HitAttribs.x - HitAttribs.y, HitAttribs.x, HitAttribs.y);

    vec3 n0 = Normals[nonuniformEXT(gl_InstanceCustomIndexNV)].n[ind.x].xyz;
    vec3 n1 = Normals[nonuniformEXT(gl_InstanceCustomIndexNV)].n[ind.y].xyz;
    vec3 n2 = Normals[nonuniformEXT(gl_InstanceCustomIndexNV)].n[ind.z].xyz;

    // normalize before or after? or both?
    vec3 N = interpolate(n0, n1, n2, barycentrics);
    N = normalize(vec3(gl_ObjectToWorldNV * vec4(N, 0.)));

    vec3 L = normalize(vec3(1., 1., 1.));

    float ndotl = max(0., dot(N, L));

    vec3 color = vec3(ndotl + 0.1);

    PrimaryRay.color_distance = vec4(color, gl_HitTNV);
    PrimaryRay.normal = vec4(N, 0.);
}

