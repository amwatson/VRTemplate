# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html.
# For more examples on how to use CMake, see https://github.com/android/ndk-samples.

# Sets the minimum CMake version required for this project.
cmake_minimum_required(VERSION 3.22.1)

# Declares the project name
project("vrtemplate")
set(PROJECT_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)

# Define path to the Meta loader (if available)
set(META_LOADER_PATH "${PROJECT_ROOT_DIR}/externals/meta_openxr_loader")

# If Meta loader exists, add its directory to the CMAKE_PREFIX_PATH
if(EXISTS "${META_LOADER_PATH}")
    message(STATUS "Meta OpenXR loader directory found")
    list(APPEND CMAKE_PREFIX_PATH "${META_LOADER_PATH}")
endif()

# Try to find OpenXR package (will look in CMAKE_PREFIX_PATH first)
find_package(OpenXR QUIET)

# If OpenXR wasn't found through find_package, use the git submodule
if(NOT TARGET OpenXR::openxr_loader)
    message(STATUS "Using OpenXR from git submodule")
    # Verify that the open source OpenXR SDK exists
    if(NOT EXISTS "${PROJECT_ROOT_DIR}/externals/openxr-sdk")
        message(FATAL_ERROR "\n\n Missing OpenXR SDK!\nPlease run:\n    git submodule update --init --recursive\n")
    endif()

    # Add OpenXR SDK as a subdirectory
    add_subdirectory(${PROJECT_ROOT_DIR}/externals/openxr-sdk openxr-build EXCLUDE_FROM_ALL)
else()
    message(STATUS "Found OpenXR package")
endif()

# Add xr_linear.h
set(XR_LINEAR_DIR "${PROJECT_ROOT_DIR}/externals/openxr-sdk/src/common")
add_library(OpenXRLinear INTERFACE)
target_include_directories(OpenXRLinear INTERFACE
        $<BUILD_INTERFACE:${XR_LINEAR_DIR}>
)

# Creates and names a library
add_library(${CMAKE_PROJECT_NAME} SHARED
        gl/Egl.cpp
        gl/Framebuffer.cpp
        input/VrController.cpp
        OpenXR.cpp
        VrApp.cpp)

# Link libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
        # Common libraries
        android
        EGL
        GLESv3
        log
        OpenXR::openxr_loader
        OpenXRLinear)  # Use the target provided by find_package or the submodule