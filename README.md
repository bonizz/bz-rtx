# Vulkan RTX samples

```
Update 12/21/20

I plan to update these samples, migrating to the official extensions and cleaning up the code.

For now I'm archiving this repo until I get to it.

```


| Sample  | Screenshot |
| ------------- | ------------- |
| <b>01-sample-triangle</b> <br><br>Simple triangle sample<br><br> Refs: <br> * https://github.com/iOrange/rtxON/tree/Version_2_2 <br> * https://github.com/SaschaWillems/Vulkan <br> * https://developer.nvidia.com/rtx/raytracing/vkray <br> | ![image](https://user-images.githubusercontent.com/4008312/71532764-d79a0200-28a9-11ea-80ac-7d80a7b21106.png) |
| <b>02-sample-gltf</b> <br><br>Loads single-mesh gltf file.<br><br> | ![image](https://user-images.githubusercontent.com/4008312/71532798-fdbfa200-28a9-11ea-969d-45f0b62cb789.png) |
| <b>03-sample-shadows</b> <br><br>Adds another traceNV call to check if object is in shadow.<br>Also loads multi-mesh gltf.<br><br>Refs:<br> * [GLSL_NV_ray_tracing.txt](https://github.com/KhronosGroup/GLSL/blob/master/extensions/nv/GLSL_NV_ray_tracing.txt)<br>* [gtc 2018 s8521](http://on-demand.gputechconf.com/gtc/2018/presentation/s8521-advanced-graphics-extensions-for-vulkan.pdf) | ![image](https://user-images.githubusercontent.com/4008312/71532851-4f682c80-28aa-11ea-8bb6-297177abed2b.png) |
| <b>04-sample-materials</b><br><br>Loads baseColorFactor from gltf.<br><br>Refs:<br>* https://github.com/nvpro-samples/vk_raytrace<br>* https://github.com/maierfelix/tiny-rtx | ![image](https://user-images.githubusercontent.com/4008312/71553072-9e52b680-29bd-11ea-80a4-24fb6354c16e.png) |
| <b>05-sample-texture</b><br><br>Loads baseColor texture from gltf.<br><br>Refs:<br>General (and separate samplers/textures)<br>[GL_KHR_vulkan_glsl.txt](https://github.com/KhronosGroup/GLSL/blob/master/extensions/khr/GL_KHR_vulkan_glsl.txt)<br>[Vulkan spec chap12](https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/chap12.html)<br>[Image view/sampler](https://vulkan-tutorial.com/Texture_mapping/Image_view_and_sampler)<br><br>Understanding alignment<br>[GLSL spec variables-and-types](https://www.khronos.org/registry/OpenGL/specs/gl/GLSLangSpec.4.60.html#variables-and-types)<br>[GLSL spec](https://www.khronos.org/registry/OpenGL/specs/gl/glspec46.core.pdf)<br><br>Textures<br>https://cc0textures.com | ![image](https://user-images.githubusercontent.com/4008312/71569766-48961100-2a86-11ea-95d0-27184f762d97.png) |
| <b>06-sample-reflection</b><br><br>Adds a trace call from hit shader for non-red materials. | ![image](https://user-images.githubusercontent.com/4008312/71643191-28737700-2c6b-11ea-9ac0-506082b964ef.png) |
| <b>07-sample-refraction</b><br><br>Refraction.<br>Spheres in image are glass balls.<br>Uses recursive traceNV calls which should go away in next sample.<br><br>Refs:<br>[SO Refract](https://stackoverflow.com/questions/20801561/glsl-refract-function-explanation-available)<br>[Ray Tracing Weekend](https://raytracing.github.io/books/RayTracingInOneWeekend.html#dielectrics)<br>[Reboot Red Vulkan RTX](https://github.com/GPSnoopy/RayTracingInVulkan<br>https://www.youtube.com/watch?v=xpxVAoXaVgg) | ![image](https://user-images.githubusercontent.com/4008312/72675907-0e45e100-3a40-11ea-9e80-4e1eff138dc8.png) |

