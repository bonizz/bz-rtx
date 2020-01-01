
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


