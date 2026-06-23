cmake_minimum_required(VERSION 3.12)
project(AudioStream LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export compile commands for IDE language servers (clangd)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include core library
add_subdirectory(core)

# Add the test application for capturing system audio
add_executable(test_capture tests/test_capture.cpp)

# Link the test application with the audiostream_core library
target_link_libraries(test_capture PRIVATE audiostream_core)

# Set output directory for executables
set_target_properties(test_capture PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
