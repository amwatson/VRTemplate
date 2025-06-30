/*******************************************************************************

Filename    :   OpenXR.h
Content     :   OpenXR initialization and management
Authors     :   Amanda Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.

*******************************************************************************/

#pragma once

#include "gl/Egl.h"

#include <jni.h>
#include <array>

// Define XR-specific preprocessor directives before including OpenXR headers
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#define XR_USE_PLATFORM_ANDROID 1

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#define OXR(func)                                                        \
    do {                                                                 \
        XrResult result = (func);                                        \
        if (XR_FAILED(result)) {                                         \
            char errorString[XR_MAX_RESULT_STRING_SIZE];                 \
            xrResultToString(OpenXr::GetInstance(), result, errorString);\
            FAIL("OpenXR error: %s (%d / %s) at %s:%d",                 \
                  #func, result, errorString, __FILE__, __LINE__);       \
        }                                                                \
    } while (0)

/**
 * OpenXr - Main interface for OpenXR instance, session, and spaces management.
 *
 * This class encapsulates all OpenXR initialization, shutdown, and management operations.
 * It provides direct access to OpenXR resources through public members.
 */
class OpenXr {
public:
    // Core OpenXR types and constants
    static constexpr XrViewConfigurationType VIEW_CONFIG_TYPE = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    static constexpr uint32_t MAX_VIEW_COUNT = 2;

    OpenXr();
    ~OpenXr();

    // Explicit initialization and shutdown methods
    int32_t Init(JavaVM* jvm, jobject activityObject);
    void Shutdown();

    // Get the global OpenXR instance
    static const XrInstance& GetInstance();

    // Public view and space data
    XrInstance mInstance = XR_NULL_HANDLE;
    XrSystemId mSystemId = XR_NULL_SYSTEM_ID;
    XrSession mSession = XR_NULL_HANDLE;

    // View configuration
    std::array<XrViewConfigurationView, MAX_VIEW_COUNT> mViewConfigurationViews;
    XrViewConfigurationProperties mViewportConfig = {};

    // Reference spaces
    XrSpace mHeadSpace = XR_NULL_HANDLE;
    XrSpace mViewSpace = XR_NULL_HANDLE;
    XrSpace mLocalSpace = XR_NULL_HANDLE;
    XrSpace mStageSpace = XR_NULL_HANDLE;
    XrSpace mForwardDirectionSpace = XR_NULL_HANDLE;

    // Tracking state
    XrSpaceLocation headLocation = {};

    // Swapchain configuration
    uint32_t mMaxLayerCount = 0;

    // EGL context for rendering
    std::unique_ptr<EglContext> mEglContext;

    // Current session state (for state management)
    XrSessionState mSessionState = XR_SESSION_STATE_UNKNOWN;

private:
    // Private initialization methods - explicit steps
    int32_t InitLoader(JavaVM* jvm, jobject activityObject);
    int32_t InitInstance();
    int32_t InitSystem();
    int32_t InitSession();
    int32_t InitViewConfig();
    int32_t InitSpaces();

    // Private cleanup methods
    void DestroySpaces();
    void DestroySession();
    void DestroyInstance();

    // Required extensions - defined at compile time
    static constexpr const char* REQUIRED_EXTENSIONS[] = {
            XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
            XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME,
            XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME,
    };

    // Optional extensions - defined at compile time
// In OpenXR.h, update OPTIONAL_EXTENSIONS:
    static constexpr const char* OPTIONAL_EXTENSIONS[] = {
            XR_FB_PASSTHROUGH_EXTENSION_NAME,
            XR_META_PERFORMANCE_METRICS_EXTENSION_NAME,
            XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME,
            XR_EXT_HAND_TRACKING_EXTENSION_NAME,        // Add hand tracking
            XR_FB_TOUCH_CONTROLLER_PRO_EXTENSION_NAME,  // For Quest 3 controllers
            XR_KHR_VISIBILITY_MASK_EXTENSION_NAME,
    };

    // Enabled extensions cache (populated during initialization)
    std::unique_ptr<const char*[]> mEnabledExtensions;
    uint32_t mEnabledExtensionCount = 0;
};