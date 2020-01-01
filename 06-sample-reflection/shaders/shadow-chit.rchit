#version 460
#extension GL_NV_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "shader-common.h"

layout(location = 1) rayPayloadInNV ShadowRayPayload ShadowRay;

void main()
{
    ShadowRay.distance = gl_HitTNV;
}

