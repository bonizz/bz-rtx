workspace "sample-triangle"
    configurations { "Debug", "Release" }
    platforms { "Win64" }

project "sample-triangle"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"

    pchheader "pch.h"
    pchsource "src/pch.cpp"

    libdirs {
        "../external/glfw/lib-vc2019"
    }
    links {
        "glfw3"
    }

    defines {
        "VOLK_STATIC_DEFINES",
        "VK_USE_PLATFORM_WIN32_KHR"
    }

    includedirs {
        "../external/glfw/include",
        "../external/glm",
        "../external/volk",
        "$(VULKAN_SDK)/Include",
        "src"
    }

    files {
        "../external/glfw/include/**.h",
        "../external/glm/glm/**.hpp",
        "../external/volk/volk.h",
        "../external/volk/volk.c",
        "src/**.h",
        "src/**.cpp"
    }

    filter {
        "files:../external/volk/volk.c"
    }
    flags { "NoPCH" }



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

