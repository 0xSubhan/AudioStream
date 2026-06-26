cmake_minimum_required(VERSION 3.12)
project(audiostream_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    # Windows: Use WASAPI and vcpkg for Opus
    find_package(Opus CONFIG REQUIRED)
    add_library(audiostream_core STATIC
        src/wasapi_capture.cpp
        src/opus_encoder.cpp
        src/opus_decoder.cpp
        src/udp_sender.cpp
        src/stream_controller.cpp
    )
    target_include_directories(audiostream_core PUBLIC include)
    target_link_libraries(audiostream_core PUBLIC Opus::opus)
else()
    # Linux: Use PulseAudio and local paths
    set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party/extracted")

    find_path(PULSE_INCLUDE_DIR NAMES pulse/simple.h
        PATHS "${THIRD_PARTY_DIR}/usr/include"
        NO_DEFAULT_PATH
    )
    if(NOT PULSE_INCLUDE_DIR)
        find_path(PULSE_INCLUDE_DIR NAMES pulse/simple.h)
    endif()

    find_library(PULSE_LIBRARY NAMES pulse)
    find_library(PULSE_SIMPLE_LIBRARY NAMES pulse-simple)

    find_path(OPUS_INCLUDE_DIR NAMES opus/opus.h)
    find_library(OPUS_LIBRARY NAMES opus)

    if(NOT PULSE_INCLUDE_DIR OR NOT PULSE_LIBRARY OR NOT PULSE_SIMPLE_LIBRARY)
        message(FATAL_ERROR "PulseAudio Simple API development headers/libraries not found!")
    endif()

    add_library(audiostream_core STATIC
        src/pulse_capture.cpp
        src/opus_encoder.cpp
        src/opus_decoder.cpp
        src/udp_sender.cpp
        src/stream_controller.cpp
    )

    target_include_directories(audiostream_core PUBLIC
        include
        ${PULSE_INCLUDE_DIR}
        ${OPUS_INCLUDE_DIR}
    )

    target_link_libraries(audiostream_core PUBLIC
        ${PULSE_LIBRARY}
        ${PULSE_SIMPLE_LIBRARY}
        ${OPUS_LIBRARY}
    )
endif()
