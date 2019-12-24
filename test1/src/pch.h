


#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#include <vector>
#include <algorithm>
#include <fstream>

#pragma warning(push)
#pragma warning(disable: 26812)
#include <volk.h>
#pragma warning(pop)

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>

