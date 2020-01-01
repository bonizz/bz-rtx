# Vulkan RTX samples

### 001-sample-triangle

Simple triangle example.

![image](https://user-images.githubusercontent.com/4008312/71532764-d79a0200-28a9-11ea-80ac-7d80a7b21106.png)

Refs:
* https://github.com/iOrange/rtxON/tree/Version_2_2
* https://github.com/SaschaWillems/Vulkan
* https://developer.nvidia.com/rtx/raytracing/vkray


### 002-sample-gltf

Loads single-mesh gltf file.

![image](https://user-images.githubusercontent.com/4008312/71532798-fdbfa200-28a9-11ea-969d-45f0b62cb789.png)


### 003-sample-shadows

Adds another traceNV call to check if object is in shadow.

Also loads multi-mesh gltf.

![image](https://user-images.githubusercontent.com/4008312/71532851-4f682c80-28aa-11ea-8bb6-297177abed2b.png)

Refs:
* https://github.com/KhronosGroup/GLSL/blob/master/extensions/nv/GLSL_NV_ray_tracing.txt
* http://on-demand.gputechconf.com/gtc/2018/presentation/s8521-advanced-graphics-extensions-for-vulkan.pdf


### 004-sample-materials

Loads baseColorFactor from gltf.

![image](https://user-images.githubusercontent.com/4008312/71553072-9e52b680-29bd-11ea-80a4-24fb6354c16e.png)

Refs:
* https://github.com/nvpro-samples/vk_raytrace
* https://github.com/maierfelix/tiny-rtx


### 005-sample-texture

Loads baseColor texture from gltf.

![image](https://user-images.githubusercontent.com/4008312/71569766-48961100-2a86-11ea-95d0-27184f762d97.png)

Refs:

General (and separate samplers/textures)
- https://github.com/KhronosGroup/GLSL/blob/master/extensions/khr/GL_KHR_vulkan_glsl.txt
- https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/chap12.html
- https://vulkan-tutorial.com/Texture_mapping/Image_view_and_sampler

Understanding alignment
- https://www.khronos.org/registry/OpenGL/specs/gl/GLSLangSpec.4.60.html#variables-and-types
- https://www.khronos.org/registry/OpenGL/specs/gl/glspec46.core.pdf

Textures
- https://cc0textures.com

### 006-sample-reflection

Adds a trace call from hit shader for non-red materials.

![image](https://user-images.githubusercontent.com/4008312/71643191-28737700-2c6b-11ea-9ac0-506082b964ef.png)
