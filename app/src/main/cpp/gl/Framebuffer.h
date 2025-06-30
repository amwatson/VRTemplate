/*******************************************************************************

Filename    :   Framebuffer.h
Content     :   OpenGL framebuffer management with multiview support
Authors     :   Amanda Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.

*******************************************************************************/

#pragma once

#include "../utils/LogUtils.h"
#include "../utils/Common.h"
#include "../OpenXR.h"

#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
// Needed for openxr_platform? without XR_USE_PLATFORM_ANDROID,
// EGLEnum doesn't get defined, and with that, jni.h is needed
#include <jni.h>
// Define XR-specific preprocessor directives before including OpenXR headers
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#define XR_USE_PLATFORM_ANDROID 1
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <memory>
#include <vector>

// Simple macro for checking OpenGL errors
#define GL(func) func; CheckGLError(#func, __FILE__, __LINE__)

// OpenGL error checking function
void CheckGLError(const char* function, const char* file, int line);

// Swapchain structure that matches the project style
struct Swapchain {
    XrSwapchain mHandle = XR_NULL_HANDLE;
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
};

class Framebuffer {
public:
    Framebuffer();
    ~Framebuffer();

    // Prevent copy but allow move
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&&) = default;
    Framebuffer& operator=(Framebuffer&&) = default;

    // Create the framebuffer with specified parameters
    // Added useMultiview parameter to support multiview rendering
    bool Create(XrSession session, GLenum colorFormat, int width, int height, int multisamples, bool useMultiview = false);

    // Clean up resources
    void Destroy();

    // Framebuffer operations
    void SetCurrent() const;
    static void SetNone();
    void Resolve() const;
    void Acquire();
    void Release() const;

    // Accessors
    int GetWidth() const { return mWidth; }
    int GetHeight() const { return mHeight; }
    Swapchain& GetColorSwapChain() { return mColorSwapChain; }
    const Swapchain& GetColorSwapChain() const { return mColorSwapChain; }
    bool UsesMultiview() const;

    // Dumps detailed framebuffer state for debugging
    void DumpState() const;

private:
    int mWidth;
    int mHeight;
    int mMultisamples;
    bool mUseMultiview;  // Whether this framebuffer uses multiview rendering
    uint32_t mTextureSwapChainLength;
    uint32_t mTextureSwapChainIndex;
    Swapchain mColorSwapChain;
    std::vector<XrSwapchainImageOpenGLESKHR> mColorSwapChainImages;
    std::vector<GLuint> mDepthBuffers;
    std::vector<GLuint> mFrameBuffers;
    std::vector<GLuint> mMsaaColorBuffers; // Multisampled renderbuffers
};