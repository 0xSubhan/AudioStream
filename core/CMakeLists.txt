cmake_minimum_required(VERSION 3.12)
project(audiostream_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure search paths for third-party libraries (PulseAudio & Opus)
set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party/extracted")

# 1. PulseAudio Simple API Headers
find_path(PULSE_INCLUDE_DIR NAMES pulse/simple.h
    PATHS "${THIRD_PARTY_DIR}/usr/include"
    NO_DEFAULT_PATH
)
if(NOT PULSE_INCLUDE_DIR)
    find_path(PULSE_INCLUDE_DIR NAMES pulse/simple.h)
endif()

# 2. PulseAudio Main Library
find_library(PULSE_LIBRARY NAMES pulse
    PATHS "${THIRD_PARTY_DIR}/usr/lib/x86_64-linux-gnu"
    NO_DEFAULT_PATH
)
if(NOT PULSE_LIBRARY)
    find_library(PULSE_LIBRARY NAMES pulse)
endif()

# 3. PulseAudio Simple Library
find_library(PULSE_SIMPLE_LIBRARY NAMES pulse-simple
    PATHS "${THIRD_PARTY_DIR}/usr/lib/x86_64-linux-gnu"
    NO_DEFAULT_PATH
)
if(NOT PULSE_SIMPLE_LIBRARY)
    find_library(PULSE_SIMPLE_LIBRARY NAMES pulse-simple)
endif()

# 4. Opus Headers
find_path(OPUS_INCLUDE_DIR NAMES opus/opus.h
    PATHS "${THIRD_PARTY_DIR}/usr/include"
    NO_DEFAULT_PATH
)
if(NOT OPUS_INCLUDE_DIR)
    find_path(OPUS_INCLUDE_DIR NAMES opus/opus.h)
endif()

# 5. Opus Library
find_library(OPUS_LIBRARY NAMES opus
    PATHS "${THIRD_PARTY_DIR}/usr/lib/x86_64-linux-gnu"
    NO_DEFAULT_PATH
)
if(NOT OPUS_LIBRARY)
    find_library(OPUS_LIBRARY NAMES opus)
endif()

# Print resolved search paths for verification
message(STATUS "PulseAudio include: ${PULSE_INCLUDE_DIR}")
message(STATUS "PulseAudio lib:     ${PULSE_LIBRARY}")
message(STATUS "PulseSimple lib:    ${PULSE_SIMPLE_LIBRARY}")
message(STATUS "Opus include:       ${OPUS_INCLUDE_DIR}")
message(STATUS "Opus lib:           ${OPUS_LIBRARY}")

# Check that PulseAudio was found (required for capture)
if(NOT PULSE_INCLUDE_DIR OR NOT PULSE_LIBRARY OR NOT PULSE_SIMPLE_LIBRARY)
    message(FATAL_ERROR "PulseAudio Simple API development headers/libraries not found!")
endif()

# Define the core library
add_library(audiostream_core STATIC
    src/pulse_capture.cpp
    src/opus_encoder.cpp
    src/opus_decoder.cpp
    src/udp_sender.cpp
)

# Set compile definitions and include directories
target_include_directories(audiostream_core PUBLIC
    include
    ${PULSE_INCLUDE_DIR}
    ${OPUS_INCLUDE_DIR}
)

# Link dependencies
target_link_libraries(audiostream_core PUBLIC
    ${PULSE_LIBRARY}
    ${PULSE_SIMPLE_LIBRARY}
    ${OPUS_LIBRARY}
)
