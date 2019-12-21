
#include "pch.h"

#define VK_CHECK(_error) do { \
    if (_error != VK_SUCCESS) { \
        assert(false && ##_error); \
    } \
} while (false)


const char* kAppName = "test vulkan app";
bool kEnableValidation = true;

VkInstance g_instance;
GLFWwindow* g_window;


bool init()
{
    if (!glfwInit())
        return false;

    if (!glfwVulkanSupported())
        return false;

    if (volkInitialize() != VK_SUCCESS)
        return false;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    g_window = glfwCreateWindow(800, 600, kAppName, nullptr, nullptr);

    if (!g_window)
        return false;


    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = kAppName;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "VulkanApp";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };

    std::vector<const char*> layers;

    if (kEnableValidation)
    {
        extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        layers.push_back("VK_LAYER_LUNARG_standard_validation");
    }

    VkInstanceCreateInfo instInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instInfo.flags = 0;
    instInfo.pApplicationInfo = &appInfo;
    instInfo.enabledExtensionCount = uint32_t(extensions.size());
    instInfo.ppEnabledExtensionNames = extensions.data();
    instInfo.enabledLayerCount = uint32_t(layers.size());
    instInfo.ppEnabledLayerNames = layers.data();

    VkResult res = vkCreateInstance(&instInfo, nullptr, &g_instance);
    if (res != VK_SUCCESS)
    {
        assert(0);
        return false;
    }

    volkLoadInstance(g_instance);


    return true;
}

int main()
{
    if (!init())
        return 1;





    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

    printf("%d extensions supported\n", extensionCount);

    glm::mat4 matrix;
    glm::vec4 vec;
    auto test = matrix * vec;

    while (!glfwWindowShouldClose(g_window)) {
        glfwPollEvents();
    }

    glfwDestroyWindow(g_window);
    glfwTerminate();

    return 0;
}
