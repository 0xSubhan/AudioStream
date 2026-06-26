cmake_minimum_required(VERSION 3.12)
project(AudioStream LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Export compile commands for IDE language servers (clangd)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include core library
add_subdirectory(core)

# Test applications are Linux-only (they directly use PulseAudio)
# On Windows the C++ engine is validated indirectly through the Python bindings
if(NOT WIN32)
    add_executable(test_capture tests/test_capture.cpp)
    add_executable(test_opus    tests/test_opus.cpp)
    add_executable(test_sender  tests/test_sender.cpp)

    target_link_libraries(test_capture PRIVATE audiostream_core)
    target_link_libraries(test_opus    PRIVATE audiostream_core)
    target_link_libraries(test_sender  PRIVATE audiostream_core)

    set_target_properties(test_capture test_opus test_sender PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

# ----------------------------------------------------
# Python Bindings (Phase 4)
# ----------------------------------------------------
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c "import pybind11; print(pybind11.get_cmake_dir())"
    OUTPUT_VARIABLE PYBIND11_CMAKE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(PYBIND11_CMAKE_DIR)
    message(STATUS "Found pybind11 CMake directory: ${PYBIND11_CMAKE_DIR}")
    list(APPEND CMAKE_PREFIX_PATH "${PYBIND11_CMAKE_DIR}")
    find_package(pybind11 REQUIRED)

    pybind11_add_module(audiostream_pybindings core/src/bindings.cpp)
    target_link_libraries(audiostream_pybindings PRIVATE audiostream_core)

    # Place the compiled shared library directly in the pc_app/ directory and rename binary to audiostream_core
    set_target_properties(audiostream_pybindings PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/pc_app"
        OUTPUT_NAME "audiostream_core"
    )
else()
    message(WARNING "pybind11 not found in Python. Python bindings will not be compiled.")
endif()
