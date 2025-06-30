/*******************************************************************************

Filename    :   OpenXR.cpp
Content     :   OpenXR initialization and management implementation
Authors     :   Amanda Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.

*******************************************************************************/

#include "OpenXR.h"
#include "utils/LogUtils.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <vector>

// Global XrInstance for error handling access
static XrInstance gXrInstance = XR_NULL_HANDLE;

// Error checking macro for internal use
#define BAIL_ON_XR_ERROR(func, returnCode) \
    do { \
        XrResult xrResult = (func); \
        if (XR_FAILED(xrResult)) { \
            ALOGE("ERROR (%s): %s() returned XrResult %d", __FUNCTION__, #func, xrResult); \
            return (returnCode); \
        } \
    } while (0)

//------------------------------------------------------------------------------
// OpenXr Implementation
//------------------------------------------------------------------------------

OpenXr::OpenXr() :
        mViewConfigurationViews{} {
    // Initialize view config structs
    for (auto& view : mViewConfigurationViews) {
        view.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
        view.next = nullptr;
    }

    // Initialize viewport config
    mViewportConfig.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;
    mViewportConfig.next = nullptr;

    // Initialize head location
    headLocation.type = XR_TYPE_SPACE_LOCATION;
    headLocation.next = nullptr;
}

OpenXr::~OpenXr() {
    Shutdown();
}

const XrInstance& OpenXr::GetInstance() {
    return gXrInstance;
}

int32_t OpenXr::Init(JavaVM* jvm, jobject activityObject) {
    // Step-by-step initialization sequence
    int32_t result;

    // 1. Initialize OpenXR loader
    result = InitLoader(jvm, activityObject);
    if (result < 0) {
        ALOGE("Failed to initialize OpenXR loader: %d", result);
        return result;
    }

    // 2. Create OpenXR instance
    result = InitInstance();
    if (result < 0) {
        ALOGE("Failed to create OpenXR instance: %d", result);
        return result;
    }

    // Update global instance for error handling
    gXrInstance = mInstance;

    // 3. Get system ID and properties
    result = InitSystem();
    if (result < 0) {
        ALOGE("Failed to initialize OpenXR system: %d", result);
        DestroyInstance();
        return result;
    }

    // 4. Create EGL context for rendering
    mEglContext = std::make_unique<EglContext>();
    if (!mEglContext) {
        ALOGE("Failed to create EGL context");
        DestroyInstance();
        return -7;
    }

    // 5. Create OpenXR session
    result = InitSession();
    if (result < 0) {
        ALOGE("Failed to create OpenXR session: %d", result);
        DestroyInstance();
        return result;
    }

    // 6. Initialize view configuration
    result = InitViewConfig();
    if (result < 0) {
        ALOGE("Failed to initialize view configuration: %d", result);
        DestroySession();
        DestroyInstance();
        return result;
    }

    // 7. Initialize reference spaces
    result = InitSpaces();
    if (result < 0) {
        ALOGE("Failed to initialize spaces: %d", result);
        DestroySession();
        DestroyInstance();
        return result;
    }

    ALOGV("OpenXR initialization complete");
    return 0;
}

void OpenXr::Shutdown() {

    // 1. Destroy spaces
    DestroySpaces();

    // 2. Destroy session
    DestroySession();

    // 3. Destroy instance
    DestroyInstance();

    // 4. Clean up extension list
    mEnabledExtensions.reset();
    mEnabledExtensionCount = 0;

    // 5. Clear EGL context
    mEglContext.reset();
}

int32_t OpenXr::InitLoader(JavaVM* jvm, jobject activityObject) {
    // Get the loader initialization function
    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR = nullptr;
    XrResult result = xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                                            (PFN_xrVoidFunction*)&xrInitializeLoaderKHR);

    if (XR_FAILED(result) || xrInitializeLoaderKHR == nullptr) {
        ALOGE("Failed to get xrInitializeLoaderKHR function pointer: %d", result);
        return -1;
    }

    // Initialize the loader with Android-specific info
    XrLoaderInitInfoAndroidKHR loaderInfo{};
    loaderInfo.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
    loaderInfo.next = nullptr;
    loaderInfo.applicationVM = jvm;
    loaderInfo.applicationContext = activityObject;

    result = xrInitializeLoaderKHR(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&loaderInfo));
    if (XR_FAILED(result)) {
        ALOGE("Failed to initialize OpenXR loader: %d", result);
        return -2;
    }

    return 0;
}

int32_t OpenXr::InitInstance() {
    // 1. Enumerate available extensions
    uint32_t extCount = 0;
    BAIL_ON_XR_ERROR(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr), -1);

    if (extCount == 0) {
        ALOGE("No OpenXR extensions available");
        return -1;
    }

    std::vector<XrExtensionProperties> extProps(extCount);
    for (auto& e : extProps) {
        e.type = XR_TYPE_EXTENSION_PROPERTIES;
        e.next = nullptr;
    }

    // Get extension properties
    BAIL_ON_XR_ERROR(xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, extProps.data()), -2);

    // 2. Validate required extensions
    const size_t requiredExtCount = sizeof(REQUIRED_EXTENSIONS) / sizeof(REQUIRED_EXTENSIONS[0]);
    const size_t optionalExtCount = sizeof(OPTIONAL_EXTENSIONS) / sizeof(OPTIONAL_EXTENSIONS[0]);

    // Check required extensions
    for (size_t i = 0; i < requiredExtCount; i++) {
        bool found = false;
        for (uint32_t j = 0; j < extCount; j++) {
            if (strcmp(REQUIRED_EXTENSIONS[i], extProps[j].extensionName) == 0) {
                found = true;
                break;
            }
        }

        if (!found) {
            ALOGE("Required OpenXR extension not available: %s", REQUIRED_EXTENSIONS[i]);
            return -3;
        }
    }

    // 3. Determine enabled extensions
    mEnabledExtensionCount = requiredExtCount;

    // Count available optional extensions
    for (size_t i = 0; i < optionalExtCount; i++) {
        for (uint32_t j = 0; j < extCount; j++) {
            if (strcmp(OPTIONAL_EXTENSIONS[i], extProps[j].extensionName) == 0) {
                mEnabledExtensionCount++;
                break;
            }
        }
    }

    // Add required extensions
    mEnabledExtensions =
            std::make_unique<const char*[]>(mEnabledExtensionCount);
    for (size_t i = 0; i < requiredExtCount; ++i) {
        mEnabledExtensions[i] = REQUIRED_EXTENSIONS[i];
    }

    // Add available optional extensions
    size_t enabledIdx = requiredExtCount;
    for (size_t i = 0; i < optionalExtCount; i++) {
        for (uint32_t j = 0; j < extCount; j++) {
            if (strcmp(OPTIONAL_EXTENSIONS[i], extProps[j].extensionName) == 0) {
                mEnabledExtensions[enabledIdx++] = OPTIONAL_EXTENSIONS[i];
                ALOGD("Enabling optional extension: %s", OPTIONAL_EXTENSIONS[i]);
                break;
            }
        }
    }

    // 4. Create the OpenXR instance
    XrApplicationInfo appInfo{};
    std::snprintf(appInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "VR Template");
    appInfo.applicationVersion = 1;
    std::snprintf(appInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "VRTemplateEngine");
    appInfo.engineVersion = 1;
    appInfo.apiVersion = XR_CURRENT_API_VERSION;

    XrInstanceCreateInfo createInfo;
    createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
    createInfo.next = nullptr;
    createInfo.createFlags = 0;
    createInfo.applicationInfo = appInfo;
    createInfo.enabledApiLayerCount = 0;
    createInfo.enabledApiLayerNames = nullptr;
    createInfo.enabledExtensionCount = mEnabledExtensionCount;
    createInfo.enabledExtensionNames = mEnabledExtensions.get();

    BAIL_ON_XR_ERROR(xrCreateInstance(&createInfo, &mInstance), -4);

    // 5. Log runtime information
    XrInstanceProperties instanceProps{};
    instanceProps.type = XR_TYPE_INSTANCE_PROPERTIES;
    instanceProps.next = nullptr;

    if (XR_SUCCEEDED(xrGetInstanceProperties(mInstance, &instanceProps))) {
        ALOGV("Connected to OpenXR runtime: %s (version %d.%d.%d)",
              instanceProps.runtimeName,
              XR_VERSION_MAJOR(instanceProps.runtimeVersion),
              XR_VERSION_MINOR(instanceProps.runtimeVersion),
              XR_VERSION_PATCH(instanceProps.runtimeVersion));
    }

    return 0;
}

int32_t OpenXr::InitSystem() {
    // 1. Get system ID
    XrSystemGetInfo systemInfo{};
    systemInfo.type = XR_TYPE_SYSTEM_GET_INFO;
    systemInfo.next = nullptr;
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    BAIL_ON_XR_ERROR(xrGetSystem(mInstance, &systemInfo, &mSystemId), -1);

    // 2. Get system properties
    XrSystemProperties properties{};
    properties.type = XR_TYPE_SYSTEM_PROPERTIES;
    properties.next = nullptr;

    BAIL_ON_XR_ERROR(xrGetSystemProperties(mInstance, mSystemId, &properties), -2);

    // Store max layer count
    mMaxLayerCount = properties.graphicsProperties.maxLayerCount;

    // 3. Log system information
    ALOGV("System properties:");
    ALOGV("  System name: %s", properties.systemName);
    ALOGV("  Vendor ID: %d", properties.vendorId);
    ALOGV("  Graphics properties:");
    ALOGV("    Max swapchain image width: %d", properties.graphicsProperties.maxSwapchainImageWidth);
    ALOGV("    Max swapchain image height: %d", properties.graphicsProperties.maxSwapchainImageHeight);
    ALOGV("    Max layer count: %d", properties.graphicsProperties.maxLayerCount);
    ALOGV("  Tracking properties:");
    ALOGV("    Orientation tracking: %s", properties.trackingProperties.orientationTracking ? "Yes" : "No");
    ALOGV("    Position tracking: %s", properties.trackingProperties.positionTracking ? "Yes" : "No");

    return 0;
}

int32_t OpenXr::InitSession() {
    // 1. Get graphics requirements
    PFN_xrGetOpenGLESGraphicsRequirementsKHR getGraphicsRequirements = nullptr;
    BAIL_ON_XR_ERROR(
            xrGetInstanceProcAddr(mInstance, "xrGetOpenGLESGraphicsRequirementsKHR",
                                  (PFN_xrVoidFunction*)&getGraphicsRequirements),
            -1
    );

    XrGraphicsRequirementsOpenGLESKHR graphicsRequirements{};
    graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR;
    graphicsRequirements.next = nullptr;

    BAIL_ON_XR_ERROR(getGraphicsRequirements(mInstance, mSystemId, &graphicsRequirements), -2);

    // 2. Check graphics requirements
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    const XrVersion glVersion = XR_MAKE_VERSION(major, minor, 0);
    if (glVersion < graphicsRequirements.minApiVersionSupported ||
        glVersion > graphicsRequirements.maxApiVersionSupported) {
        ALOGE("OpenGL ES version %d.%d is not supported. Required: %d.%d to %d.%d",
              major, minor,
              XR_VERSION_MAJOR(graphicsRequirements.minApiVersionSupported),
              XR_VERSION_MINOR(graphicsRequirements.minApiVersionSupported),
              XR_VERSION_MAJOR(graphicsRequirements.maxApiVersionSupported),
              XR_VERSION_MINOR(graphicsRequirements.maxApiVersionSupported));
        return -3;
    }

    // 3. Create session with graphics binding
    XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding{};
    graphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR;
    graphicsBinding.next = nullptr;
    graphicsBinding.display = mEglContext->mDisplay;
    graphicsBinding.config = mEglContext->mConfig;
    graphicsBinding.context = mEglContext->mContext;

    XrSessionCreateInfo createInfo{};
    createInfo.type = XR_TYPE_SESSION_CREATE_INFO;
    createInfo.next = &graphicsBinding;
    createInfo.createFlags = 0;
    createInfo.systemId = mSystemId;

    BAIL_ON_XR_ERROR(xrCreateSession(mInstance, &createInfo, &mSession), -4);

    return 0;
}

int32_t OpenXr::InitViewConfig() {
    // 1. Enumerate view configurations
    uint32_t viewConfigCount = 0;
    BAIL_ON_XR_ERROR(
            xrEnumerateViewConfigurations(mInstance, mSystemId, 0, &viewConfigCount, nullptr),
            -1
    );

    if (viewConfigCount == 0) {
        ALOGE("No view configurations available");
        return -2;
    }

    std::vector<XrViewConfigurationType> viewConfigTypes(viewConfigCount);
    BAIL_ON_XR_ERROR(
            xrEnumerateViewConfigurations(mInstance, mSystemId, viewConfigCount, &viewConfigCount, viewConfigTypes.data()),
            -3
    );

    // 2. Find stereo view configuration
    bool foundStereoView = false;
    for (uint32_t i = 0; i < viewConfigCount; i++) {
        const char* configName = "Unknown";
        switch (viewConfigTypes[i]) {
            case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO: configName = "PRIMARY_MONO"; break;
            case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO: configName = "PRIMARY_STEREO"; break;
            case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO: configName = "PRIMARY_QUAD_VARJO"; break;
            case XR_VIEW_CONFIGURATION_TYPE_SECONDARY_MONO_FIRST_PERSON_OBSERVER_MSFT:
            case XR_VIEW_CONFIGURATION_TYPE_MAX_ENUM:
            default: break;
        }

        ALOGD("  [%d] %s%s", i, configName,
              (viewConfigTypes[i] == VIEW_CONFIG_TYPE) ? " (Selected)" : "");

        if (viewConfigTypes[i] == VIEW_CONFIG_TYPE) {
            foundStereoView = true;
        }
    }

    if (!foundStereoView) {
        ALOGE("Required view configuration type not found: PRIMARY_STEREO");
        return -4;
    }

    // 3. Get view configuration properties
    BAIL_ON_XR_ERROR(
            xrGetViewConfigurationProperties(mInstance, mSystemId, VIEW_CONFIG_TYPE, &mViewportConfig),
            -5
    );

    ALOGD("View configuration properties:");
    ALOGD("  FOV mutable: %s", mViewportConfig.fovMutable ? "Yes" : "No");

    // 4. Get view configuration views
    uint32_t viewCount = 0;
    BAIL_ON_XR_ERROR(
            xrEnumerateViewConfigurationViews(mInstance, mSystemId, VIEW_CONFIG_TYPE, 0, &viewCount, nullptr),
            -6
    );

    if (viewCount > MAX_VIEW_COUNT) {
        ALOGW("More views available (%d) than supported (%d)", viewCount, MAX_VIEW_COUNT);
        viewCount = MAX_VIEW_COUNT;
    }

    BAIL_ON_XR_ERROR(
            xrEnumerateViewConfigurationViews(mInstance, mSystemId, VIEW_CONFIG_TYPE, viewCount, &viewCount, mViewConfigurationViews.data()),
            -7
    );

    // 5. Log view configuration details
    for (uint32_t i = 0; i < viewCount; i++) {
        ALOGD("  View [%d] configuration:", i);
        ALOGD("    Recommended size: %dx%d",
              mViewConfigurationViews[i].recommendedImageRectWidth,
              mViewConfigurationViews[i].recommendedImageRectHeight);
        ALOGD("    Max size: %dx%d",
              mViewConfigurationViews[i].maxImageRectWidth,
              mViewConfigurationViews[i].maxImageRectHeight);
        ALOGD("    Recommended samples: %d",
              mViewConfigurationViews[i].recommendedSwapchainSampleCount);
    }

    return 0;
}

int32_t OpenXr::InitSpaces() {
    // 1. Enumerate available reference space types
    uint32_t spaceCount = 0;
    BAIL_ON_XR_ERROR(
            xrEnumerateReferenceSpaces(mSession, 0, &spaceCount, nullptr),
            -1
    );

    if (spaceCount == 0) {
        ALOGE("No reference spaces available");
        return -2;
    }

    std::vector<XrReferenceSpaceType> spaceTypes(spaceCount);
    BAIL_ON_XR_ERROR(
            xrEnumerateReferenceSpaces(mSession, spaceCount, &spaceCount, spaceTypes.data()),
            -3
    );

    // 2. Check for required space types
    bool hasStageSpace = false;

    for (uint32_t i = 0; i < spaceCount; i++) {
        const char* spaceName = "Unknown";
        switch (spaceTypes[i]) {
            case XR_REFERENCE_SPACE_TYPE_VIEW: spaceName = "VIEW"; break;
            case XR_REFERENCE_SPACE_TYPE_LOCAL: spaceName = "LOCAL"; break;
            case XR_REFERENCE_SPACE_TYPE_STAGE: spaceName = "STAGE"; break;
            case XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR: spaceName = "LOCAL_FLOOR"; break;
            case XR_REFERENCE_SPACE_TYPE_UNBOUNDED_MSFT: spaceName = "UNBOUNDED_MSFT"; break;
            case XR_REFERENCE_SPACE_TYPE_COMBINED_EYE_VARJO: spaceName = "COMBINED_EYE_VARJO"; break;
            case XR_REFERENCE_SPACE_TYPE_LOCALIZATION_MAP_ML: spaceName = "XR_REFERENCE_SPACE_TYPE_LOCALIZATION_MAP_ML"; break;
            case XR_REFERENCE_SPACE_TYPE_MAX_ENUM:
            default: break;
        }

        ALOGD("  [%d] %s", i, spaceName);

        if (spaceTypes[i] == XR_REFERENCE_SPACE_TYPE_STAGE) {
            hasStageSpace = true;
        }
    }

    // 3. Create reference spaces
    XrReferenceSpaceCreateInfo spaceCreateInfo{};
    spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    spaceCreateInfo.next = nullptr;
    spaceCreateInfo.poseInReferenceSpace = XrPosef{
            /* orientation */ XrQuaternionf{0.0f, 0.0f, 0.0f, 1.0f},
            /* position    */ XrVector3f{0.0f, 0.0f, 0.0f}
    };

    // Create view space
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    BAIL_ON_XR_ERROR(
            xrCreateReferenceSpace(mSession, &spaceCreateInfo, &mViewSpace),
            -4
    );

    // Create head space
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    BAIL_ON_XR_ERROR(
            xrCreateReferenceSpace(mSession, &spaceCreateInfo, &mHeadSpace),
            -5
    );

    // Create local space
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    BAIL_ON_XR_ERROR(
            xrCreateReferenceSpace(mSession, &spaceCreateInfo, &mLocalSpace),
            -6
    );

    // Create forward direction space (same as local initially)
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    BAIL_ON_XR_ERROR(
            xrCreateReferenceSpace(mSession, &spaceCreateInfo, &mForwardDirectionSpace),
            -7
    );

    // Create stage space if available
    if (hasStageSpace) {
        spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        BAIL_ON_XR_ERROR(
                xrCreateReferenceSpace(mSession, &spaceCreateInfo, &mStageSpace),
                -8
        );
        ALOGD("Stage space created successfully");
    } else {
        ALOGW("Stage space not available on this device");
    }

    return 0;
}

void OpenXr::DestroySpaces() {
    // Destroy all reference spaces in reverse order of creation
    if (mStageSpace != XR_NULL_HANDLE) {
        OXR(xrDestroySpace(mStageSpace));
        mStageSpace = XR_NULL_HANDLE;
    }

    if (mForwardDirectionSpace != XR_NULL_HANDLE) {
        OXR(xrDestroySpace(mForwardDirectionSpace));
        mForwardDirectionSpace = XR_NULL_HANDLE;
    }

    if (mLocalSpace != XR_NULL_HANDLE) {
        OXR(xrDestroySpace(mLocalSpace));
        mLocalSpace = XR_NULL_HANDLE;
    }

    if (mHeadSpace != XR_NULL_HANDLE) {
        OXR(xrDestroySpace(mHeadSpace));
        mHeadSpace = XR_NULL_HANDLE;
    }

    if (mViewSpace != XR_NULL_HANDLE) {
        OXR(xrDestroySpace(mViewSpace));
        mViewSpace = XR_NULL_HANDLE;
    }
}

void OpenXr::DestroySession() {
    if (mSession != XR_NULL_HANDLE) {
        // Only end the session if it's in a state that allows ending
        if (mSessionState == XR_SESSION_STATE_READY ||
            mSessionState == XR_SESSION_STATE_SYNCHRONIZED ||
            mSessionState == XR_SESSION_STATE_VISIBLE ||
            mSessionState == XR_SESSION_STATE_FOCUSED) {
            ALOGD("Ending active XrSession");
            xrEndSession(mSession);
        }

        ALOGD("Destroying XrSession");
        xrDestroySession(mSession);
        mSession = XR_NULL_HANDLE;
    }
}

void OpenXr::DestroyInstance() {
    // Destroy the OpenXR instance
    if (mInstance != XR_NULL_HANDLE) {
        ALOGD("Destroying XrInstance");
        xrDestroyInstance(mInstance);
        mInstance = XR_NULL_HANDLE;
        gXrInstance = XR_NULL_HANDLE;
    }
}