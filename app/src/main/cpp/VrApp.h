/*******************************************************************************

Filename    :   VrApp.h
Content     :   Main VR application header
Authors     :   Amanda Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.

*******************************************************************************/

#pragma once

#include "input/VrController.h"
#include "utils/Common.h"
#include "gl/Framebuffer.h"

#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#include <memory>
#include <thread>
#include <vector>
#include <array>

class VrApp {
public:
    explicit VrApp() = default;
    ~VrApp() = default;

    void MainLoop();

private:
    static constexpr std::size_t MAX_EYES = 2;

    struct AppState;

    void Init();
    void InitSceneResources();
    void Frame(const AppState& appState) noexcept;

    void HandleInput(const InputStateFrame& inputState, AppState& newState) const;

    AppState HandleEvents() const;
    void HandleStateChanges(AppState& newState) const;
    void HandleMessageQueueEvents(AppState& newState) const;

    void OXRPollEvents(AppState& newAppState) const;
    void OXRHandleSessionStateChangedEvent(AppState& newAppState,
                                           const XrEventDataSessionStateChanged& newState) const;
    void OXRHandleSessionStateChanges(const XrSessionState state, AppState& newAppState) const;

    void RenderScene(std::array<XrCompositionLayer, 2>& layers,
                     uint32_t& layerCount,
                     const XrTime predictedDisplayTime) noexcept;

    struct AppState {
        bool mIsStopRequested = false;
        bool mIsXrSessionActive = false;
        bool mHasFocus = false;
    };

    GLuint mSquareProgram = 0;
    GLuint mSquareVBO = 0;
    GLuint mSquareVAO = 0;

    // App state from previous frame.
    AppState mLastAppState;

    // Eye framebuffers
    std::array<Framebuffer, MAX_EYES> mFramebuffers;

    uint64_t mFrameIndex = 0;

    std::unique_ptr<InputStateStatic> mInputStateStatic;
    InputStateFrame mInputStateFrame;
};