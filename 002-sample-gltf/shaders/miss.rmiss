#version 460
#extension GL_NV_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#include "shader-common.h"

layout(location = 0) rayPayloadInNV RayPayload PrimaryRay;

void main()
{
    const vec3 backgroundColor = vec3(0., 1., 0.);
    PrimaryRay.color = backgroundColor;
}
