
// packed std140
struct UniformParams
{
    mat4 viewInverse;
    mat4 projInverse;
};

struct RayPayload
{
    vec4 color_distance;
    vec4 normal;
};

struct ShadowRayPayload
{
    float distance;
};

vec3 srgbToLinear(vec3 c)
{
    return pow(c, vec3(2.2));
}

vec3 linearToSrgb(vec3 c)
{
    return pow(c, vec3(1.0 / 2.2));
}
