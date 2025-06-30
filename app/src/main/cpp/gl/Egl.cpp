/*******************************************************************************
Filename    :   Egl.cpp
Content     :   EGL context management for OpenGL ES rendering
Authors     :   Amanda Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.
*******************************************************************************/

#include "Egl.h"
#include "../utils/LogUtils.h"

namespace {

/**
 * Convert EGL error code to descriptive string
 */
    const char* EglErrorToString(const EGLint error) {
        switch (error) {
            case EGL_SUCCESS:             return "EGL_SUCCESS";
            case EGL_NOT_INITIALIZED:     return "EGL_NOT_INITIALIZED";
            case EGL_BAD_ACCESS:          return "EGL_BAD_ACCESS";
            case EGL_BAD_ALLOC:           return "EGL_BAD_ALLOC";
            case EGL_BAD_ATTRIBUTE:       return "EGL_BAD_ATTRIBUTE";
            case EGL_BAD_CONTEXT:         return "EGL_BAD_CONTEXT";
            case EGL_BAD_CONFIG:          return "EGL_BAD_CONFIG";
            case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
            case EGL_BAD_DISPLAY:         return "EGL_BAD_DISPLAY";
            case EGL_BAD_SURFACE:         return "EGL_BAD_SURFACE";
            case EGL_BAD_MATCH:           return "EGL_BAD_MATCH";
            case EGL_BAD_PARAMETER:       return "EGL_BAD_PARAMETER";
            case EGL_BAD_NATIVE_PIXMAP:   return "EGL_BAD_NATIVE_PIXMAP";
            case EGL_BAD_NATIVE_WINDOW:   return "EGL_BAD_NATIVE_WINDOW";
            case EGL_CONTEXT_LOST:        return "EGL_CONTEXT_LOST";
            default:                      return "UNKNOWN_ERROR";
        }
    }

/**
 * Check for EGL errors and log them
 */
    [[maybe_unused]] EGLint CheckEglError(const char* message) {
        EGLint error = eglGetError();
        if (error != EGL_SUCCESS) {
            ALOGE("%s: EGL error: %s (0x%X)", message, EglErrorToString(error), error);
        }
        return error;
    }

/**
 * Choose the best EGL config for VR rendering
 */
    bool ChooseBestEglConfig(EGLDisplay display, EGLConfig& config) {
        const EGLint configAttribs[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                EGL_RED_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, 8,
                EGL_DEPTH_SIZE, 0,
                EGL_STENCIL_SIZE, 0,
                EGL_SAMPLES, 0,
                EGL_NONE
        };

        EGLint numConfigs = 0;
        if (eglChooseConfig(display, configAttribs, &config, 1, &numConfigs) == EGL_FALSE) {
            ALOGE("Failed to choose EGL config: %s", EglErrorToString(eglGetError()));
            return false;
        }

        if (numConfigs < 1) {
            ALOGE("No matching EGL configs found");
            return false;
        }

        return true;
    }

/**
 * Log EGL config attributes for debugging
 */
    void LogEglConfig(EGLDisplay display, EGLConfig config) {
        if (display == EGL_NO_DISPLAY || config == 0) {
            return;
        }

        ALOGD("EGL Config details:");

        EGLint value = 0;

        eglGetConfigAttrib(display, config, EGL_RED_SIZE, &value);
        ALOGD("  EGL_RED_SIZE: %d", value);

        eglGetConfigAttrib(display, config, EGL_GREEN_SIZE, &value);
        ALOGD("  EGL_GREEN_SIZE: %d", value);

        eglGetConfigAttrib(display, config, EGL_BLUE_SIZE, &value);
        ALOGD("  EGL_BLUE_SIZE: %d", value);

        eglGetConfigAttrib(display, config, EGL_ALPHA_SIZE, &value);
        ALOGD("  EGL_ALPHA_SIZE: %d", value);

        eglGetConfigAttrib(display, config, EGL_DEPTH_SIZE, &value);
        ALOGD("  EGL_DEPTH_SIZE: %d", value);

        eglGetConfigAttrib(display, config, EGL_STENCIL_SIZE, &value);
        ALOGD("  EGL_STENCIL_SIZE: %d", value);

        eglGetConfigAttrib(display, config, EGL_SAMPLES, &value);
        ALOGD("  EGL_SAMPLES: %d", value);

        eglGetConfigAttrib(display, config, EGL_SAMPLE_BUFFERS, &value);
        ALOGD("  EGL_SAMPLE_BUFFERS: %d", value);
    }

} // anonymous namespace

//------------------------------------------------------------------------------
// EglContext implementation
//------------------------------------------------------------------------------

EglContext::EglContext() {
    const int32_t result = Init();
    if (result < 0) {
        ALOGE("EGL context initialization failed: error %d", result);
        Shutdown();
    }
}

EglContext::~EglContext() {
    Shutdown();
}

bool EglContext::MakeCurrent() {
    if (mContext == EGL_NO_CONTEXT || mDisplay == EGL_NO_DISPLAY) {
        ALOGE("Cannot make EGL context current: invalid context");
        return false;
    }

    if (eglMakeCurrent(mDisplay, mDummySurface, mDummySurface, mContext) == EGL_FALSE) {
        ALOGE("eglMakeCurrent failed: %s", EglErrorToString(eglGetError()));
        return false;
    }

    return true;
}

bool EglContext::ReleaseCurrent() {
    if (mDisplay == EGL_NO_DISPLAY) {
        return false;
    }

    if (eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) == EGL_FALSE) {
        ALOGE("eglMakeCurrent(EGL_NO_CONTEXT) failed: %s", EglErrorToString(eglGetError()));
        return false;
    }

    return true;
}

int32_t EglContext::Init() {
    mDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (mDisplay == EGL_NO_DISPLAY) {
        ALOGE("eglGetDisplay failed: %s", EglErrorToString(eglGetError()));
        return -1;
    }

    EGLint majorVersion = 0;
    EGLint minorVersion = 0;
    if (eglInitialize(mDisplay, &majorVersion, &minorVersion) == EGL_FALSE) {
        ALOGE("eglInitialize failed: %s", EglErrorToString(eglGetError()));
        return -2;
    }

    ALOGD("EGL initialized: version %d.%d", majorVersion, minorVersion);

    if (!ChooseBestEglConfig(mDisplay, mConfig)) {
        return -3;
    }

    LogEglConfig(mDisplay, mConfig);

    if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
        ALOGE("eglBindAPI failed: %s", EglErrorToString(eglGetError()));
        return -4;
    }

    const EGLint contextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
    };

    mContext = eglCreateContext(mDisplay, mConfig, EGL_NO_CONTEXT, contextAttribs);
    if (mContext == EGL_NO_CONTEXT) {
        ALOGE("eglCreateContext failed: %s", EglErrorToString(eglGetError()));
        return -5;
    }

    const EGLint surfaceAttribs[] = {
            EGL_WIDTH, 16,
            EGL_HEIGHT, 16,
            EGL_NONE
    };

    mDummySurface = eglCreatePbufferSurface(mDisplay, mConfig, surfaceAttribs);
    if (mDummySurface == EGL_NO_SURFACE) {
        ALOGE("eglCreatePbufferSurface failed: %s", EglErrorToString(eglGetError()));
        return -6;
    }

    if (eglMakeCurrent(mDisplay, mDummySurface, mDummySurface, mContext) == EGL_FALSE) {
        ALOGE("Initial eglMakeCurrent failed: %s", EglErrorToString(eglGetError()));
        return -7;
    }

    const GLubyte* glVersion = glGetString(GL_VERSION);
    if (glVersion) {
        ALOGV("OpenGL ES version: %s", (const char*)glVersion);
    }

    return 0;
}

void EglContext::Shutdown() {
    if (mContext != EGL_NO_CONTEXT && mDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    if (mDummySurface != EGL_NO_SURFACE && mDisplay != EGL_NO_DISPLAY) {
        eglDestroySurface(mDisplay, mDummySurface);
        mDummySurface = EGL_NO_SURFACE;
    }

    if (mContext != EGL_NO_CONTEXT && mDisplay != EGL_NO_DISPLAY) {
        eglDestroyContext(mDisplay, mContext);
        mContext = EGL_NO_CONTEXT;
    }

    if (mDisplay != EGL_NO_DISPLAY) {
        eglTerminate(mDisplay);
        mDisplay = EGL_NO_DISPLAY;
    }
}
