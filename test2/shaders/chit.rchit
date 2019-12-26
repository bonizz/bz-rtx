#version 460
#extension GL_NV_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "shader-common.h"

layout(set = 0, binding = 3, std430) buffer PositionsBuffer { vec3 p[]; } Positions;
layout(set = 0, binding = 4, std430) buffer NormalsBuffer { vec3 n[]; } Normals;
layout(set = 0, binding = 5, std430) buffer IndicesBuffer { uint i[]; } Indices;

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
        Indices.i[3 * gl_PrimitiveID],
        Indices.i[3 * gl_PrimitiveID + 1],
        Indices.i[3 * gl_PrimitiveID + 2]
    );

    vec3 barycentrics = vec3(1.0 - HitAttribs.x - HitAttribs.y, HitAttribs.x, HitAttribs.y);

    // vec3 p0 = Positions.p[ind.x];
    // vec3 p1 = Positions.p[ind.y];
    // vec3 p2 = Positions.p[ind.z];

    vec3 n0 = Normals.n[ind.x];
    vec3 n1 = Normals.n[ind.y];
    vec3 n2 = Normals.n[ind.z];

    vec3 N = normalize(interpolate(n0, n1, n2, barycentrics));

    vec3 L = normalize(vec3(1., 1., 1.));

    float ndotl = max(0., dot(N, L));

    // PrimaryRay.color = p0;
    PrimaryRay.color = barycentrics;

    // PrimaryRay.color = normal;

    PrimaryRay.color = vec3(ndotl + 0.01);


    // PrimaryRay.color = vec3(0., 0., 1.);
}


