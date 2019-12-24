
// packed std140
struct UniformParams
{
    mat4 viewInverse;
    mat4 projInverse;
};

struct RayPayload
{
    vec3 color;
};
