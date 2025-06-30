/*******************************************************************************
Filename    :   VrController.h
Content     :   OpenXR controller input and tracking
Authors     :   Amanda Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.
*******************************************************************************/

#pragma once

#include <openxr/openxr.h>

// Maximum number of controllers supported
static constexpr int MAX_CONTROLLERS = 2;

/**
 * InputStateStatic - Static controller input configuration
 *
 * This class manages the lifetime of OpenXR action sets, actions, and spaces
 * related to controller input.
 */
class InputStateStatic {
public:
    /**
     * Constructor - Creates actions and bindings for VR controllers
     *
     * @param instance OpenXR instance
     * @param session OpenXR session
     */
    InputStateStatic(const XrInstance& instance, const XrSession& session);

    /**
     * Destructor - Cleans up all OpenXR resources
     */
    ~InputStateStatic();

    // Controller spaces - one per controller
    XrSpace mHandSpaces[MAX_CONTROLLERS] = {XR_NULL_HANDLE, XR_NULL_HANDLE};

    // Subaction paths for each hand
    XrPath mHandSubactionPaths[MAX_CONTROLLERS] = {XR_NULL_PATH, XR_NULL_PATH};

    // The action set that contains all controller actions
    XrActionSet mActionSet = XR_NULL_HANDLE;

    // Controller actions grouped by type
    // Buttons
    XrAction mButtonActions[5] = {
            XR_NULL_HANDLE,  // A
            XR_NULL_HANDLE,  // B
            XR_NULL_HANDLE,  // X
            XR_NULL_HANDLE,  // Y
            XR_NULL_HANDLE   // Menu
    };

    // Triggers & grips
    XrAction mTriggerActions[2] = {
            XR_NULL_HANDLE,  // Index trigger
            XR_NULL_HANDLE   // Squeeze/grip trigger
    };

    // Thumbsticks
    XrAction mThumbstickActions[3] = {
            XR_NULL_HANDLE,  // Thumbstick position
            XR_NULL_HANDLE,  // Thumbstick click
            XR_NULL_HANDLE   // Thumbrest touch
    };

    // Hand tracking
    XrAction mHandPoseAction = XR_NULL_HANDLE;

private:
    // Helper method for creating actions with appropriate subaction paths
    XrAction CreateAction(
            XrActionType type,
            const char* actionName,
            const char* localizedName = nullptr,
            int countSubactionPaths = 0,
            XrPath* subactionPaths = nullptr);

    // Helper for creating common bindings
    void CreateCommonBindings(const XrInstance& instance);

    // Prevent copy and assignment
    InputStateStatic(const InputStateStatic&) = delete;
    InputStateStatic& operator=(const InputStateStatic&) = delete;
};

/**
 * InputStateFrame - Per-frame controller input state
 *
 * This class encapsulates the controller input state for a single frame,
 * including button states, poses, and tracking information.
 *
 * Data is organized for optimal cache access when processing all controllers.
 */
struct InputStateFrame {
    // Controller indices for clarity
    enum ControllerIndex : uint32_t {
        LEFT_CONTROLLER = 0,
        RIGHT_CONTROLLER = 1,
        NUM_CONTROLLERS = 2  // Must match MAX_CONTROLLERS
    };
    static_assert(NUM_CONTROLLERS == MAX_CONTROLLERS, "NUM_CONTROLLERS must match MAX_CONTROLLERS");

    /**
     * Sync button and thumbstick states from OpenXR
     *
     * @param session OpenXR session
     * @param staticState Input state static configuration
     */
    void SyncButtonsAndThumbSticks(const XrSession& session,
                                   const InputStateStatic& staticState);

    /**
     * Sync hand poses from OpenXR - called after buttons since it might depend on them
     *
     * @param staticState Input state static configuration
     * @param referenceSpace Reference space for poses
     * @param predictedDisplayTime Time for pose prediction
     */
    void SyncHandPoses(
                       const InputStateStatic& staticState,
                       const XrSpace& referenceSpace,
                       const XrTime predictedDisplayTime);

    // Currently preferred controller (based on most recent activity)
    ControllerIndex mPreferredHand = RIGHT_CONTROLLER;

    // Hand tracking state - grouped by controller for cache locality
    XrSpaceLocation mHandPositions[NUM_CONTROLLERS] =
            { {XR_TYPE_SPACE_LOCATION}, {XR_TYPE_SPACE_LOCATION} };
    bool mIsHandActive[NUM_CONTROLLERS] = { false, false };

    // Button states - grouped by button type for organization
    XrActionStateBoolean mFaceButtonStates[4] = {}; // A, B, X, Y
    XrActionStateBoolean mMenuButtonState = {};

    // Thumbstick and trigger states - grouped by controller for cache locality
    XrActionStateVector2f mThumbStickState[NUM_CONTROLLERS] = {};
    XrActionStateBoolean mThumbStickClickState[NUM_CONTROLLERS] = {};
    XrActionStateBoolean mThumbrestTouchState[NUM_CONTROLLERS] = {};
    XrActionStateBoolean mIndexTriggerState[NUM_CONTROLLERS] = {};
    XrActionStateBoolean mSqueezeTriggerState[NUM_CONTROLLERS] = {};

    // Helper method to efficiently determine if any button changed
    bool HasButtonChanges() const;
};

/**
 * Helper function to sync button state from OpenXR
 * Static function to avoid binding to an object
 *
 * @param session OpenXR session
 * @param action Button action
 * @param subactionPath Optional subaction path for hand-specific buttons
 * @return Button state
 */
XrActionStateBoolean SyncButtonState(const XrSession& session,
                                     const XrAction& action,
                                     const XrPath& subactionPath = XR_NULL_PATH);

/**
 * Helper function to sync vector2f state from OpenXR (for thumbsticks)
 * Static function to avoid binding to an object
 *
 * @param session OpenXR session
 * @param action Vector2f action
 * @param subactionPath Optional subaction path for hand-specific controls
 * @return Vector2f state
 */
XrActionStateVector2f SyncVector2fState(const XrSession& session,
                                        const XrAction& action,
                                        const XrPath& subactionPath = XR_NULL_PATH);

/**
 * Helper function to create an action space
 * Static function to avoid binding to an object
 *
 * @param session OpenXR session
 * @param poseAction Pose action
 * @param subactionPath Subaction path
 * @return Action space
 */
XrSpace CreateActionSpace(const XrSession& session,
                          XrAction poseAction,
                          XrPath subactionPath);