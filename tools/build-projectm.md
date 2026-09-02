# projectM

projectM is built in-tree from the `third_party/projectm` git submodule (tag v4.1.7) plus one local patch,
`tools/projectm-v4.1.7-render-fbo-backport.patch`, which backports `projectm_opengl_render_frame_fbo`
from upstream master: stock v4.1.7 can only composite a frame onto the default framebuffer, and
`core/viz/scenes/MilkdropScene.cpp` renders into its own framebuffer through this call.
The root `CMakeLists.txt` applies the patch at configure time (idempotent) and links `libgeode.so`
against the `projectM` target. LGPL-2.1: the patch is the corresponding source of the modification.
Run `git submodule update --init --recursive` after cloning or pulling; the app build then compiles it per ABI.
`checkNativePageAlignment` in `app/build.gradle.kts` still checks every packaged `.so` for 16 KB alignment.
