#include "pch.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
// #define TINYGLTF_NOEXCEPTION // optional. disable exception handling.
#pragma warning(push)
#pragma warning(disable : 4996) // printf unsafe
#pragma warning(disable : 4267) // size_t -> uint32_t
#include "tiny_gltf.h"
#pragma warning(pop)
