#version 460
#extension GL_NV_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#include "shader-common.h"

layout(location = 0) rayPayloadInNV RayPayload PrimaryRay;

void main()
{
    const vec3 skyBlue = vec3(0.529, 0.808, 0.922);
    PrimaryRay.color_distance = vec4(skyBlue, -1.0);
}
