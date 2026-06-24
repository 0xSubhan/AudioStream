cmake_minimum_required(VERSION 3.10)

# Clear Snap-injected host include environment variables to prevent compiler header pollution
set(ENV{CPATH} "")
set(ENV{C_INCLUDE_PATH} "")
set(ENV{CPLUS_INCLUDE_PATH} "")

project(audiostream_receiver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Download and cross-compile Opus codec library dynamically for Android NDK architectures
include(FetchContent)

set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE INTERNAL "")
set(OPUS_BUILD_TESTING OFF CACHE INTERNAL "")
set(OPUS_BUILD_PROGRAMS OFF CACHE INTERNAL "")

FetchContent_Declare(
    opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG v1.4
)
FetchContent_MakeAvailable(opus)

# 2. Define our native JNI receiver library
add_library(audiostream_receiver SHARED
    android_receiver_engine.cpp
)

# CRITICAL: Suppress ALL default C++ include paths (blocks Snap-injected GCC 9 headers),
# then explicitly re-add only the NDK's own libc++ standard library headers.
target_compile_options(audiostream_receiver PRIVATE
    -nostdinc++
    -isystem ${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/c++/v1
)

# 3. Link AAudio, Android Log, and the static Opus library
target_link_libraries(audiostream_receiver
    PRIVATE
    aaudio
    log
    opus
)
