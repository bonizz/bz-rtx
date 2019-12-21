workspace "test1"
    configurations { "Debug", "Release" }
    platforms { "Win64" }

project "test1"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    includedirs {
        "external/glfw/include",
        "external/glm",
        "external/volk",
        "$(VULKAN_SDK)/Include",
        "src"
    }
    files {
        "external/glfw/include/**.h",
        "external/glm/glm/**.hpp",
        "external/volk/volk.c",
        "external/volk/volk.h",
        "src/**.h",
        "src/**.cpp"
    }

    libdirs {
        "$(VULKAN_SDK)/Lib",
        "external/glfw/lib-vc2019"
    }
    links {
        "vulkan-1",
        "glfw3"
    }

    filter "platforms:Win64"
        system "Windows"
        architecture "x64"

    filter "configurations:Debug"
        targetsuffix "_D"
        defines { "DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"

