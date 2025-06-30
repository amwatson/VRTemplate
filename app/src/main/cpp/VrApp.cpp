/*******************************************************************************

Filename    :   VrApp.cpp
Content     :   Main VR application implementation
Authors     :   Amanda Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.

*******************************************************************************/

#include "VrApp.h"
#include "OpenXR.h"
#include "utils/LogUtils.h"
#include "utils/MathUtils.h"
#include "utils/MessageQueue.h"

#include <xr_linear.h>

#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#define XR_USE_PLATFORM_ANDROID       1

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#include <android/native_window_jni.h>
#include <jni.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <cassert>
#include <sys/prctl.h>
#include <unistd.h>

#ifndef NDEBUG
#define DEBUG_LIFECYCLE_VERBOSE
#endif

#if defined(DEBUG_LAYERS_VERBOSE)
#define ALOG_LAYERS_VERBOSE ALOGI
#else
#define ALOG_LAYERS_VERBOSE(...)
#endif

#if defined(DEBUG_LIFECYCLE_VERBOSE)
#define ALOG_LIFECYCLE_VERBOSE ALOGI
#else
#define ALOG_LIFECYCLE_VERBOSE(...)
#endif

namespace {
    constexpr XrPerfSettingsLevelEXT kGpuPerfLevel = XR_PERF_SETTINGS_LEVEL_BOOST_EXT;
    std::chrono::time_point<std::chrono::steady_clock> gOnCreateStartTime;
    std::unique_ptr<OpenXr> gOpenXr;
    MessageQueue gMessageQueue;

 /**
 * Logs an OpenGL shader-compile or program-link error.
 *
 * @param id      The object handle returned by glCreateShader() or glCreateProgram().
 * @param isProg  if true, treat @p id as a program, use glGetProgram* calls
 *                if false, treat @p id as a shader, use glGetShader*  calls
 * @param label   Human-readable label (“VS”, “FS”, “Program”, …) for the log message.
 * @param isFatal if true, terminal program (FATAL) on error, log non-fatal error otherwise.
 */
    void LogShaderError(const GLuint id, const bool isProg, const char* label, const bool isFatal ) noexcept
    {
        GLint status = 0;
        if (isProg)
            glGetProgramiv(id, GL_LINK_STATUS, &status);
        else
            glGetShaderiv(id,  GL_COMPILE_STATUS, &status);

        if (status == GL_TRUE)   // no error → nothing to log
            return;

        GLint logLen = 0;
        if (isProg)
            glGetProgramiv(id, GL_INFO_LOG_LENGTH, &logLen);
        else
            glGetShaderiv(id,  GL_INFO_LOG_LENGTH, &logLen);

        std::vector<char> log(logLen > 1 ? logLen : 1);
        if (isProg)
            glGetProgramInfoLog(id, logLen, nullptr, log.data());
        else
            glGetShaderInfoLog(id,  logLen, nullptr, log.data());

        if (isFatal) {
            FAIL("%s error:\n%s", label ? label : "GL object", log.data());
        } else {
            ALOGE("%s error:\n%s", label ? label : "GL object", log.data());
        }
    }

    [[maybe_unused]] const char *XrSessionStateToString(const XrSessionState state) {
        switch (state) {
            case XR_SESSION_STATE_UNKNOWN:
                return "XR_SESSION_STATE_UNKNOWN";
            case XR_SESSION_STATE_IDLE:
                return "XR_SESSION_STATE_IDLE";
            case XR_SESSION_STATE_READY:
                return "XR_SESSION_STATE_READY";
            case XR_SESSION_STATE_SYNCHRONIZED:
                return "XR_SESSION_STATE_SYNCHRONIZED";
            case XR_SESSION_STATE_VISIBLE:
                return "XR_SESSION_STATE_VISIBLE";
            case XR_SESSION_STATE_FOCUSED:
                return "XR_SESSION_STATE_FOCUSED";
            case XR_SESSION_STATE_STOPPING:
                return "XR_SESSION_STATE_STOPPING";
            case XR_SESSION_STATE_LOSS_PENDING:
                return "XR_SESSION_STATE_LOSS_PENDING";
            case XR_SESSION_STATE_EXITING:
                return "XR_SESSION_STATE_EXITING";
            case XR_SESSION_STATE_MAX_ENUM:
                return "XR_SESSION_STATE_MAX_ENUM";
            default:
                return "Unknown";
        }
    }

    // Called whenever a session is started/resumed
    void CreateRuntimeInitiatedReferenceSpaces(const XrTime predictedDisplayTime) {
        // Create a reference space with the forward direction from the
        // starting frame.
        {
            const XrReferenceSpaceCreateInfo sci = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO, nullptr,
                                                    XR_REFERENCE_SPACE_TYPE_LOCAL,
                                                    MathUtils::Posef::Identity()};
            OXR(xrCreateReferenceSpace(gOpenXr->mSession, &sci, &gOpenXr->mForwardDirectionSpace));
        }

        {
            const XrReferenceSpaceCreateInfo sci = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO, nullptr,
                                                    XR_REFERENCE_SPACE_TYPE_VIEW,
                                                    MathUtils::Posef::Identity()};
            OXR(xrCreateReferenceSpace(gOpenXr->mSession, &sci, &gOpenXr->mViewSpace));
        }

        // Get the pose of the local space.
        XrSpaceLocation lsl = {XR_TYPE_SPACE_LOCATION};
        OXR(xrLocateSpace(gOpenXr->mForwardDirectionSpace, gOpenXr->mLocalSpace,
                          predictedDisplayTime,
                          &lsl));

        // Set the forward direction of the new space.
        const XrPosef forwardDirectionPose = lsl.pose;

        // Create a reference space with the same position and rotation as
        // local.
        const XrReferenceSpaceCreateInfo sci = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO, nullptr,
                                                XR_REFERENCE_SPACE_TYPE_LOCAL,
                                                forwardDirectionPose};
        OXR(xrCreateReferenceSpace(gOpenXr->mSession, &sci, &gOpenXr->mHeadSpace));
    }

} // anonymous namespace

//-----------------------------------------------------------------------------
// VRApp

void VrApp::MainLoop() {
    //////////////////////////////////////////////////
    // Init
    //////////////////////////////////////////////////
    Init();

    //////////////////////////////////////////////////
    // Frame loop
    //////////////////////////////////////////////////

    while (true) {
        // Handle events/state-changes.
        AppState appState = HandleEvents();
        if (appState.mIsStopRequested) { break; }
        HandleStateChanges(appState);

        if (appState.mIsXrSessionActive) {
            // Increment the frame index.
            // Frame index starts at 1. I don't know why, we've always done this.
            // Doesn't actually matter, except to make the indices
            // consistent in traces
            mFrameIndex++;
            // Log time to first frame
            if (mFrameIndex == 1) {
                std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
                ALOGI("Time to first frame: %lld ms",
                      std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                            gOnCreateStartTime)
                              .count());
            }

            // Update non-tracking-dependent-state.
            mInputStateFrame.SyncButtonsAndThumbSticks(gOpenXr->mSession, *mInputStateStatic);
            HandleInput(mInputStateFrame, appState);

            Frame(appState);
        } else {
            // If no XR session is active, just handle the message queue and wait for events
            mFrameIndex = 0;
        }
        mLastAppState = appState;
    }

    //////////////////////////////////////////////////
    // Exit
    //////////////////////////////////////////////////
    ALOG_LIFECYCLE_VERBOSE("::MainLoop() exiting");
}

void VrApp::Init() {
    assert(gOpenXr != nullptr);
    mInputStateStatic = std::make_unique<InputStateStatic>(OpenXr::GetInstance(),
                                                           gOpenXr->mSession);

    // Initialize framebuffers for both eyes
    const uint32_t eyeWidth = gOpenXr->mViewConfigurationViews[0].recommendedImageRectWidth;
    const uint32_t eyeHeight = gOpenXr->mViewConfigurationViews[0].recommendedImageRectHeight;

    for (size_t eye = 0; eye < MAX_EYES; eye++) {
        if (!mFramebuffers[eye].Create(gOpenXr->mSession, GL_SRGB8_ALPHA8, eyeWidth, eyeHeight,
                                       4, false)) {
            ALOGE("Failed to create framebuffer for eye %zu", eye);
        }
    }

    InitSceneResources();
    ALOGD("Initialized VR App with eye buffers %dx%d", eyeWidth, eyeHeight);
}

void VrApp::InitSceneResources() {
    // Vertex shader
    const GLchar *vsSource = R"(
        #version 300 es
        layout(location = 0) in vec3 aPosition;
        uniform mat4 uModelViewProjection;
        void main() {
            gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
        }
    )";

    // Fragment shader
    const GLchar *fsSource = R"(
        #version 300 es
        precision mediump float;
        out vec4 fragColor;
        void main() {
            fragColor = vec4(1.0, 0.0, 0.0, 1.0); // red
        }
    )";

    // Compile shaders
    GLint ok = 0;

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsSource, nullptr);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS,&ok);
    if (!ok) {
        LogShaderError(vs, false, "Vertex Shader", true);
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsSource, nullptr);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS,&ok);
    if (!ok) {
        LogShaderError(fs, false, "Fragment Shader", true);
    }

    // Link proggitram
    mSquareProgram = glCreateProgram();
    glAttachShader(mSquareProgram, vs);
    glAttachShader(mSquareProgram, fs);
    glLinkProgram(mSquareProgram);
    glGetProgramiv(mSquareProgram, GL_COMPILE_STATUS,&ok);
    if (!ok) {
        LogShaderError(mSquareProgram, true, "Square Program", true);
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    // Square vertices (centered on 0,0,-2)
    constexpr GLfloat quadVerts[] = {
            -0.5f, -0.5f, -2.0f,
            0.5f, -0.5f, -2.0f,
            0.5f, 0.5f, -2.0f,
            -0.5f, -0.5f, -2.0f,
            0.5f, 0.5f, -2.0f,
            -0.5f, 0.5f, -2.0f
    };

    glGenBuffers(1, &mSquareVBO);
    glBindBuffer(GL_ARRAY_BUFFER, mSquareVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);

    glGenVertexArrays(1, &mSquareVAO);
    glBindVertexArray(mSquareVAO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 3, (void *) 0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VrApp::Frame([[maybe_unused]] const AppState &appState) noexcept {
    ////////////////////////////////
    // XrWaitFrame()
    ////////////////////////////////
    XrFrameState frameState = {XR_TYPE_FRAME_STATE, nullptr};
    {
        XrFrameWaitInfo wfi = {XR_TYPE_FRAME_WAIT_INFO, nullptr};
        OXR(xrWaitFrame(gOpenXr->mSession, &wfi, &frameState));
    }

    ////////////////////////////////
    // XrBeginFrame()
    ////////////////////////////////
    {
        XrFrameBeginInfo bfd = {XR_TYPE_FRAME_BEGIN_INFO, nullptr};
        OXR(xrBeginFrame(gOpenXr->mSession, &bfd));
    }

    ///////////////////////////////////////////////////
    // Get tracking, space, projection info for frame.
    ///////////////////////////////////////////////////

    // Re-initialize the reference spaces on the first frame so
    // it is in-sync with user
    if (mFrameIndex == 1) {
        CreateRuntimeInitiatedReferenceSpaces(frameState.predictedDisplayTime);
    }

    // Get head location in local space
    gOpenXr->headLocation = {XR_TYPE_SPACE_LOCATION};
    OXR(xrLocateSpace(gOpenXr->mViewSpace, gOpenXr->mLocalSpace, frameState.predictedDisplayTime,
                      &gOpenXr->headLocation));

    // Update hand/controller poses
    mInputStateFrame.SyncHandPoses(*mInputStateStatic, gOpenXr->mLocalSpace,
                                   frameState.predictedDisplayTime);

    //////////////////////////////////////////////////
    //  Set the compositor layers for this frame.
    //////////////////////////////////////////////////

    std::array<XrCompositionLayer, 2> layers = {};
    std::array<XrCompositionLayerBaseHeader *, 2> layerHeaders = {};
    uint32_t layerCount = 0;

    // Render cube scene to a layer
    RenderScene(layers, layerCount, frameState.predictedDisplayTime);

    // Check if any layers were added
    if (layerCount == 0) {
        layerCount = 1;  // Ensure at least one layer is submitted
    }

    // Populate layer headers
    for (uint32_t i = 0; i < layerCount; i++) {
        layerHeaders[i] = reinterpret_cast<XrCompositionLayerBaseHeader *>(&layers[i]);
    }

    ////////////////////////////////
    // XrEndFrame()
    ////////////////////////////////
    const XrFrameEndInfo endFrameInfo = {
            XR_TYPE_FRAME_END_INFO,
            nullptr,
            frameState.predictedDisplayTime,
            XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
            layerCount,
            layerHeaders.data()
    };

    // Debug logging to understand layer details
#if defined(DEBUG_LAYERS_VERBOSE)
    for (uint32_t i = 0; i < layerCount; i++) {
        ALOG_LAYERS_VERBOSE("Layer %u: Type %d", i, layerHeaders[i]->type);
    }
#endif

    OXR(xrEndFrame(gOpenXr->mSession, &endFrameInfo));
}

void VrApp::RenderScene(std::array<XrCompositionLayer, 2>& layers,
                        uint32_t& layerCount,
                        const XrTime predictedDisplayTime) noexcept {
    OpenXr& xr = *gOpenXr;

    const XrViewLocateInfo locateInfo = {
            XR_TYPE_VIEW_LOCATE_INFO,
            nullptr,
            OpenXr::VIEW_CONFIG_TYPE,
            predictedDisplayTime,
            xr.mLocalSpace
    };

    XrViewState viewState = { XR_TYPE_VIEW_STATE };
    std::array<XrView, MAX_EYES> views{};
    for (auto& v : views) {
        v.type = XR_TYPE_VIEW;
        v.next = nullptr;
    }

    uint32_t viewCount = 0;
    OXR(xrLocateViews(xr.mSession, &locateInfo, &viewState,
                      static_cast<uint32_t>(views.size()), &viewCount, views.data()));

    // Ensure view pose is valid
    if ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) == 0 ||
        (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) == 0) {
        ALOGE("RenderScene: Invalid view pose!");
        return;
    }

    XrCompositionLayerProjection& layer = layers[0].mProjection;
    layer = {};
    layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    layer.space = xr.mLocalSpace;
    layer.layerFlags = 0;

    static XrCompositionLayerProjectionView projViews[MAX_EYES];
    layer.viewCount = MAX_EYES;
    layer.views = projViews;
    for (size_t eye = 0; eye < MAX_EYES; ++eye) {
        XrCompositionLayerProjectionView& view = projViews[eye];
        view = {};
        view.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        view.pose = views[eye].pose;
        view.fov  = views[eye].fov;

        Framebuffer& fb = mFramebuffers[eye];
        fb.Acquire();
        fb.SetCurrent();

        while (glGetError() != GL_NO_ERROR) { /* eat errors */ }

        // Setup GL
        glViewport(0, 0, fb.GetWidth(), fb.GetHeight());
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glClearDepthf(1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        GLint linkStatus = 0;
        glGetProgramiv(mSquareProgram, GL_LINK_STATUS, &linkStatus);
        if (linkStatus == GL_FALSE) {
            ALOGE("Shader program failed to link.");
            return;
        }
        glUseProgram(mSquareProgram);

        // Build MVP matrix
        XrMatrix4x4f projMatrix;
        XrMatrix4x4f_CreateProjectionFov(&projMatrix,
                                         GraphicsAPI::GRAPHICS_OPENGL, view.fov, 0.1f, 100.0f);

        XrPosef invertedPose;
        XrPosef_Invert(&invertedPose, &view.pose);

        XrMatrix4x4f viewMatrix;
        XrMatrix4x4f_CreateFromRigidTransform(&viewMatrix, &invertedPose);

        XrMatrix4x4f mvpMatrix;
        XrMatrix4x4f_Multiply(&mvpMatrix, &projMatrix, &viewMatrix);

        GLint mvpLoc = glGetUniformLocation(mSquareProgram, "uModelViewProjection");
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvpMatrix.m[0]);

        glBindVertexArray(mSquareVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            ALOGE("OpenGL error during draw: 0x%x", err);
        }

        fb.Resolve();
        fb.Release();

        view.subImage = {
                fb.GetColorSwapChain().mHandle,
                { {0, 0}, {static_cast<int32_t>(fb.GetWidth()), static_cast<int32_t>(fb.GetHeight())} },
                0
        };
    }
    layers[layerCount].mProjection = layer;
    layerCount++;
}

void
VrApp::HandleInput(const InputStateFrame &inputState, AppState &newState) const {
    // Check for quit gesture/command (menu button on left controller)
    if (inputState.mMenuButtonState.changedSinceLastSync &&
        inputState.mMenuButtonState.currentState == XR_TRUE) {
        // In a real app, you might want to show a confirmation dialog first
        // For now, we'll just request to stop the app
        newState.mIsStopRequested = true;
    }
}

VrApp::AppState VrApp::HandleEvents() const {
    AppState newState = mLastAppState;
    OXRPollEvents(newState);
    HandleMessageQueueEvents(newState);
    return newState;
}

void VrApp::HandleStateChanges(AppState &) const {
    // In this simple app, we don't have much state to handle
    // This would be where you'd handle transitions between different app states
}

void VrApp::OXRPollEvents(AppState &newAppState) const {
    XrEventDataBuffer eventDataBuffer;

    // Process all pending messages.
    for (;;) {
        eventDataBuffer = {};
        XrEventDataBaseHeader *baseEventHeader = (XrEventDataBaseHeader *) (&eventDataBuffer);
        baseEventHeader->type = XR_TYPE_EVENT_DATA_BUFFER;
        baseEventHeader->next = nullptr;
        XrResult r;
        OXR(r = xrPollEvent(gOpenXr->mInstance, &eventDataBuffer));
        if (r != XR_SUCCESS) { break; }

        switch (static_cast<int>(baseEventHeader->type)) {
            case XR_TYPE_EVENT_DATA_EVENTS_LOST:
                ALOGD("%s(): Received XR_TYPE_EVENT_DATA_EVENTS_LOST event", __func__);
                break;
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                ALOGD("%s(): Received XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING event",
                      __func__);
                break;
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                const XrEventDataSessionStateChanged *ssce =
                        (XrEventDataSessionStateChanged *) (baseEventHeader);
                if (ssce != nullptr) {
                    ALOGD("%s(): Received XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED", __func__);
                    OXRHandleSessionStateChangedEvent(newAppState, *ssce);
                } else {
                    ALOGE("%s(): Received XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: nullptr",
                          __func__);
                }
            }
                break;
            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                ALOGD("%s(): Received XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED event",
                      __func__);
                break;
            case XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT: {
                const XrEventDataPerfSettingsEXT *pfs =
                        (XrEventDataPerfSettingsEXT *) (baseEventHeader);
                ALOGD("%s(): Received XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT event: type %d subdomain %d : level %d -> level %d",
                      __func__, pfs->type, pfs->subDomain, pfs->fromLevel, pfs->toLevel);
            }
                break;
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                ALOGD("%s(): Received XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING event",
                      __func__);
                break;
            default:
                ALOGD("%s(): Unknown event", __func__);
                break;
        }
    }
}

void VrApp::OXRHandleSessionStateChangedEvent(AppState &newAppState,
                                              const XrEventDataSessionStateChanged &newState) const {
    static XrSessionState lastState = XR_SESSION_STATE_UNKNOWN;
    if (newState.state != lastState) {
        ALOGD("%s(): Received XR_SESSION_STATE_CHANGED state %s->%s session=%p time=%lld",
              __func__, XrSessionStateToString(lastState),
              XrSessionStateToString(newState.state), (void *) newState.session,
              (long long) newState.time);
    }
    lastState = newState.state;

    switch (newState.state) {
        case XR_SESSION_STATE_FOCUSED:
            ALOGD("%s(): Received XR_SESSION_STATE_FOCUSED event", __func__);
            newAppState.mHasFocus = true;
            break;
        case XR_SESSION_STATE_VISIBLE:
            ALOGD("%s(): Received XR_SESSION_STATE_VISIBLE event", __func__);
            newAppState.mHasFocus = false;
            break;
        case XR_SESSION_STATE_READY:
        case XR_SESSION_STATE_STOPPING:
            OXRHandleSessionStateChanges(newState.state, newAppState);
            break;
        case XR_SESSION_STATE_EXITING:
            newAppState.mIsStopRequested = true;
            break;
        case XR_SESSION_STATE_UNKNOWN:
            ALOGD("%s(): Received XR_SESSION_STATE_UNKNOWN event", __func__);
            break;
        case XR_SESSION_STATE_IDLE:
            ALOGD("%s(): Received XR_SESSION_STATE_IDLE event", __func__);
            // This state is not used in this app, but could be used to pause the app
            // if needed.
            break;
        case XR_SESSION_STATE_SYNCHRONIZED:
            ALOGD("%s(): Received XR_SESSION_STATE_SYNCHRONIZED event", __func__);
            // This state is not used in this app, but could be used to synchronize
            // the app with the runtime.
            break;
        case XR_SESSION_STATE_LOSS_PENDING:
            ALOGD("%s(): Received XR_SESSION_STATE_LOSS_PENDING event", __func__);
            // This state is not used in this app, but could be used to handle
            // session loss gracefully.
            break;
        case XR_SESSION_STATE_MAX_ENUM:
        default:
            break;
    }
}

void
VrApp::OXRHandleSessionStateChanges(const XrSessionState state, AppState &newAppState) const {
    if (state == XR_SESSION_STATE_READY) {
        assert(mLastAppState.mIsXrSessionActive == false);

        XrSessionBeginInfo sbi = {};
        sbi.type = XR_TYPE_SESSION_BEGIN_INFO;
        sbi.next = nullptr;
        sbi.primaryViewConfigurationType = gOpenXr->mViewportConfig.viewConfigurationType;

        {
            XrResult result;
            result = xrBeginSession(gOpenXr->mSession, &sbi);
            newAppState.mIsXrSessionActive = (result == XR_SUCCESS);
        }

        // Set session state once we have entered VR mode and have a valid
        // session object.
        if (newAppState.mIsXrSessionActive) {
            ALOG_LIFECYCLE_VERBOSE("%s(): Entered XR_SESSION_STATE_READY", __func__);

            // Set performance levels for CPU and GPU
            PFN_xrPerfSettingsSetPerformanceLevelEXT pfnPerfSettingsSetPerformanceLevelEXT = nullptr;
            OXR(xrGetInstanceProcAddr(
                    gOpenXr->mInstance, "xrPerfSettingsSetPerformanceLevelEXT",
                    (PFN_xrVoidFunction *) (&pfnPerfSettingsSetPerformanceLevelEXT)));

            OXR(pfnPerfSettingsSetPerformanceLevelEXT(gOpenXr->mSession,
                                                      XR_PERF_SETTINGS_DOMAIN_CPU_EXT,
                                                      XR_PERF_SETTINGS_LEVEL_BOOST_EXT));
            OXR(pfnPerfSettingsSetPerformanceLevelEXT(
                    gOpenXr->mSession, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, kGpuPerfLevel));

            // Set application thread priority
            PFN_xrSetAndroidApplicationThreadKHR pfnSetAndroidApplicationThreadKHR = nullptr;
            OXR(xrGetInstanceProcAddr(
                    gOpenXr->mInstance, "xrSetAndroidApplicationThreadKHR",
                    (PFN_xrVoidFunction *) (&pfnSetAndroidApplicationThreadKHR)));

            OXR(pfnSetAndroidApplicationThreadKHR(
                    gOpenXr->mSession, XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR, gettid()));
        }
    } else if (state == XR_SESSION_STATE_STOPPING) {
        assert(mLastAppState.mIsXrSessionActive);
        ALOG_LIFECYCLE_VERBOSE("%s(): Entered XR_SESSION_STATE_STOPPING", __func__);
        OXR(xrEndSession(gOpenXr->mSession));
        newAppState.mIsXrSessionActive = false;
    }
}

void VrApp::HandleMessageQueueEvents(AppState &newAppState) const {
    // Arbitrary limit to prevent the render thread from blocking too long
    // on a single frame. This may happen if the app is paused in an edge
    // case.
    constexpr size_t kMaxNumMessagesPerFrame = 20;

    size_t numMessagesHandled = 0;
    Message message;
    while (numMessagesHandled < kMaxNumMessagesPerFrame) {
        if (!gMessageQueue.Poll(message)) { break; }
        numMessagesHandled++;

        switch (message.mType) {
            case Message::Type::EXIT_NEEDED: {
                ALOGD("Received EXIT_NEEDED message");
                newAppState.mIsStopRequested = true;
                break;
            }
            default:
                ALOGE("Unknown message type: %d", static_cast<int>(message.mType));
                break;
        }
    }
}

//-----------------------------------------------------------------------------
// VRAppThread

class VRAppThread {
public:
    VRAppThread(JavaVM *const jvm, JNIEnv *const jni, const jobject activityObject) {
        assert(jvm != nullptr);
        assert(activityObject != nullptr);

        const jobject activityGlobalRef = jni->NewGlobalRef(activityObject);
        mThread = std::thread([jvm, activityGlobalRef]() {
            ThreadFn(jvm, activityGlobalRef);
        });
    }

    ~VRAppThread() {
        gMessageQueue.Post(Message(Message::Type::EXIT_NEEDED, 0));
        ALOG_LIFECYCLE_VERBOSE("Waiting for VRAppThread to join");
        if (mThread.joinable()) {
            mThread.join();
        }
        ALOG_LIFECYCLE_VERBOSE("VRAppThread joined");
    }

private:
    static void ThreadFn(JavaVM *const jvm, const jobject activityObjectGlobalRef) {
        ALOG_LIFECYCLE_VERBOSE("VRAppThread: starting");

        JNIEnv *jni = nullptr;
        if (jvm->AttachCurrentThread(&jni, nullptr) != JNI_OK) {
            FAIL("%s(): Could not attach to JVM", __FUNCTION__);
        }

        prctl(PR_SET_NAME, (long) "VR::Main", 0, 0, 0);
        ThreadFnJNI(jvm, jni, activityObjectGlobalRef);

        jvm->DetachCurrentThread();
        ALOG_LIFECYCLE_VERBOSE("VRAppThread: exited");
    }

    static void
    ThreadFnJNI(JavaVM *const jvm, JNIEnv *const jni, const jobject activityObjectGlobalRef) {
        assert(jni != nullptr);
        assert(activityObjectGlobalRef != nullptr);

        if (gOpenXr == nullptr) {
            gOpenXr = std::make_unique<OpenXr>();
            const int32_t ret = gOpenXr->Init(jvm, activityObjectGlobalRef);
            if (ret < 0) {
                FAIL("OpenXR::Init() failed: error code %d", ret);
            }
        }

        std::make_unique<VrApp>()->MainLoop();

        ALOG_LIFECYCLE_VERBOSE("::MainLoop() exited");

        gOpenXr->Shutdown();
        jni->DeleteGlobalRef(activityObjectGlobalRef);
    }

    std::thread mThread;
};

//-----------------------------------------------------------------------------
// Handle -> pointer conversion methods

inline jlong ToHandle(VRAppThread *thread) {
    return reinterpret_cast<jlong>(thread);
}

inline VRAppThread *FromHandle(jlong handle) {
    return reinterpret_cast<VRAppThread *>(handle);
}

//-----------------------------------------------------------------------------
// JNI functions

extern "C" JNIEXPORT jlong JNICALL
Java_com_amwatson_vrtemplate_MainActivity_nativeOnCreate(JNIEnv *env, jobject thiz) {
    // Log the create start time, which will be used to calculate the total
    // time to first frame.
    gOnCreateStartTime = std::chrono::steady_clock::now();

    JavaVM *jvm;
    env->GetJavaVM(&jvm);
    const jlong ret = ToHandle(new VRAppThread(jvm, env, thiz));
    ALOG_LIFECYCLE_VERBOSE("nativeOnCreate %ld", static_cast<long>(ret));
    return ret;
}

extern "C" JNIEXPORT void JNICALL
Java_com_amwatson_vrtemplate_MainActivity_nativeOnDestroy([[maybe_unused]] JNIEnv *env,
                                                          [[maybe_unused]] jobject thiz,
                                                          jlong handle) {
    ALOG_LIFECYCLE_VERBOSE("nativeOnDestroy %ld", static_cast<long>(handle));
    if (handle != 0) { delete FromHandle(handle); }
}
