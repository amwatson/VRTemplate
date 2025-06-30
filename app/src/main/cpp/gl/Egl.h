/*******************************************************************************

Filename    :   Egl.h
Content     :   EGL context management for OpenGL ES rendering
Authors     :   Amanda Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.

*******************************************************************************/

#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

/**
 * EglContext - RAII wrapper for EGL rendering context
 *
 * This class manages the lifecycle of an EGL context for OpenGL ES rendering.
 * It automatically creates and destroys the necessary EGL resources.
 */
class EglContext {
public:
    /**
     * Constructor - Creates an EGL context suitable for use with OpenXR
     */
    EglContext();

    /**
     * Destructor - Cleans up all EGL resources
     */
    ~EglContext();

    /**
     * Check if the context was successfully initialized
     * @return true if the context is valid, false otherwise
     */
    bool IsValid() const { return mContext != EGL_NO_CONTEXT; }

    /**
     * Make this context current on the calling thread
     * @return true if successful, false on error
     */
    bool MakeCurrent();

    /**
     * Release the current context
     * @return true if successful, false on error
     */
    bool ReleaseCurrent();

    // EGL handles
    EGLDisplay mDisplay = EGL_NO_DISPLAY;
    EGLConfig  mConfig  = 0;
    EGLContext mContext = EGL_NO_CONTEXT;

private:
    // Private implementation
    int32_t Init();
    void    Shutdown();

    // Dummy surface for context creation
    // (needed because OpenGL ES doesn't support surfaceless contexts on all platforms)
    EGLSurface mDummySurface = EGL_NO_SURFACE;
};