#version 460
#extension GL_NV_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "shader-common.h"

layout(location = 0) rayPayloadInNV RayPayload PrimaryRay;

void main()
{
    PrimaryRay.color = vec3(0., 0., 1.);
}
