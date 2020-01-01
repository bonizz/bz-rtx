#version 460
#extension GL_NV_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#include "shader-common.h"

layout(location = 1) rayPayloadInNV ShadowRayPayload ShadowRay;

void main()
{
    ShadowRay.distance = -1.0;
}
