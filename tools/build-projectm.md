# projectM

projectM is built in-tree from the `third_party/projectm` git submodule (tag v4.1.7, stock, no patches).
The root `CMakeLists.txt` adds it as a subdirectory and links `libgeode.so` against its `projectM` target.
Run `git submodule update --init --recursive` after cloning or pulling; the app build then compiles it per ABI.
The JNI bridge is `app/src/main/cpp/milkdrop_jni.cpp`; it renders on the default framebuffer and the scene copies the frame.
`checkNativePageAlignment` in `app/build.gradle.kts` still checks every packaged `.so` for 16 KB alignment.
