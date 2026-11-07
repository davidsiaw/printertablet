# Uncomment this if you're using STL in your project
# See CPLUSPLUS-SUPPORT.html in the NDK documentation for more information
APP_STL := gnustl_static 
# Optimise the native code (mruby, FreeType, SDL, ours). Without this, the NDK
# builds everything with -O0, because AndroidManifest.xml says
# android:debuggable="true"; Ruby then took 200-750 ms per status update.
APP_OPTIM := release
NDK_TOOLCHAIN_VERSION := clang3.2
# ARMv7 with hardware floating point: armeabi (ARMv5) does every float
# operation in software. The tablet is arm64-v8a, which runs 32-bit v7a code;
# NDK r9 (the build image) can't build arm64 itself.
APP_ABI := armeabi-v7a
