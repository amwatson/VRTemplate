/*******************************************************************************
Filename    :   VrController.cpp
Content     :   OpenXR controller input and tracking
Authors     :   Amanda Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.
*******************************************************************************/

#include "VrController.h"
#include "../utils/LogUtils.h"
#include "../OpenXR.h"

#include <cstring>
#include <cassert>

// Enable for detailed input logging - control with a #define
#if defined(DEBUG_INPUT_VERBOSE)
#define ALOG_INPUT_VERBOSE(...) ALOGI(__VA_ARGS__)
#else
#define ALOG_INPUT_VERBOSE(...)
#endif

//------------------------------------------------------------------------------
// Static helper functions
//------------------------------------------------------------------------------

XrActionStateBoolean SyncButtonState(const XrSession& session,
                                     const XrAction& action,
                                     const XrPath& subactionPath) {
    XrActionStateGetInfo getInfo = {};
    getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.next = nullptr;
    getInfo.action = action;
    getInfo.subactionPath = subactionPath;

    XrActionStateBoolean state = {};
    state.type = XR_TYPE_ACTION_STATE_BOOLEAN;
    state.next = nullptr;

    OXR(xrGetActionStateBoolean(session, &getInfo, &state));
    return state;
}

XrActionStateVector2f SyncVector2fState(const XrSession& session,
                                        const XrAction& action,
                                        const XrPath& subactionPath) {
    XrActionStateGetInfo getInfo = {};
    getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.next = nullptr;
    getInfo.action = action;
    getInfo.subactionPath = subactionPath;

    XrActionStateVector2f state = {};
    state.type = XR_TYPE_ACTION_STATE_VECTOR2F;
    state.next = nullptr;

    OXR(xrGetActionStateVector2f(session, &getInfo, &state));
    return state;
}

XrSpace CreateActionSpace(const XrSession& session,
                          XrAction poseAction,
                          XrPath subactionPath) {
    XrActionSpaceCreateInfo createInfo = {};
    createInfo.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
    createInfo.next = nullptr;
    createInfo.action = poseAction;
    createInfo.subactionPath = subactionPath;
    createInfo.poseInActionSpace.orientation.w = 1.0f;  // Identity quaternion

    XrSpace space = XR_NULL_HANDLE;
    OXR(xrCreateActionSpace(session, &createInfo, &space));
    return space;
}

//------------------------------------------------------------------------------
// InputStateStatic Implementation
//------------------------------------------------------------------------------

XrAction InputStateStatic::CreateAction(XrActionType type,
                                        const char* actionName,
                                        const char* localizedName,
                                        int countSubactionPaths,
                                        XrPath* subactionPaths) {
    ALOG_INPUT_VERBOSE("CreateAction %s with %d subactionPaths",
                       actionName, countSubactionPaths);

    XrActionCreateInfo createInfo = {};
    createInfo.type = XR_TYPE_ACTION_CREATE_INFO;
    createInfo.next = nullptr;
    createInfo.actionType = type;
    createInfo.countSubactionPaths = countSubactionPaths;
    createInfo.subactionPaths = subactionPaths;

    // Use null-safe string copy
    strncpy(createInfo.actionName, actionName, XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(createInfo.localizedActionName,
            localizedName ? localizedName : actionName,
            XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);

    XrAction action = XR_NULL_HANDLE;
    OXR(xrCreateAction(mActionSet, &createInfo, &action));
    return action;
}

void InputStateStatic::CreateCommonBindings(const XrInstance& instance) {
    // Define controller profile
    XrPath touchControllerProfile = XR_NULL_PATH;
    OXR(xrStringToPath(instance, "/interaction_profiles/oculus/touch_controller",
                       &touchControllerProfile));

    // Prepare the binding vector with expected capacity
    // Using a raw vector here to avoid memory manager overhead
    // Fixed capacity for deterministic memory usage
    const int MAX_BINDINGS = 32;
    XrActionSuggestedBinding bindings[MAX_BINDINGS];
    int bindingCount = 0;

    auto AddBinding = [&](XrAction action, const char* bindingPath) {
        if (bindingCount < MAX_BINDINGS) {
            XrPath path = XR_NULL_PATH;
            OXR(xrStringToPath(instance, bindingPath, &path));

            bindings[bindingCount].action = action;
            bindings[bindingCount].binding = path;
            bindingCount++;
        }
    };

    // Menu button (left hand only)
    AddBinding(mButtonActions[4], "/user/hand/left/input/menu/click");

    // Hand poses for both controllers
    AddBinding(mHandPoseAction, "/user/hand/left/input/aim/pose");
    AddBinding(mHandPoseAction, "/user/hand/right/input/aim/pose");

    // Face buttons - right hand (A/B)
    AddBinding(mButtonActions[0], "/user/hand/right/input/a/click");
    AddBinding(mButtonActions[1], "/user/hand/right/input/b/click");

    // Face buttons - left hand (X/Y)
    AddBinding(mButtonActions[2], "/user/hand/left/input/x/click");
    AddBinding(mButtonActions[3], "/user/hand/left/input/y/click");

    // Triggers
    AddBinding(mTriggerActions[0], "/user/hand/left/input/trigger");
    AddBinding(mTriggerActions[0], "/user/hand/right/input/trigger");

    // Grips
    AddBinding(mTriggerActions[1], "/user/hand/left/input/squeeze/value");
    AddBinding(mTriggerActions[1], "/user/hand/right/input/squeeze/value");

    // Thumbsticks
    AddBinding(mThumbstickActions[0], "/user/hand/left/input/thumbstick");
    AddBinding(mThumbstickActions[0], "/user/hand/right/input/thumbstick");

    // Thumbstick clicks
    AddBinding(mThumbstickActions[1], "/user/hand/left/input/thumbstick/click");
    AddBinding(mThumbstickActions[1], "/user/hand/right/input/thumbstick/click");

    // Thumbrest touch
    AddBinding(mThumbstickActions[2], "/user/hand/left/input/thumbrest/touch");
    AddBinding(mThumbstickActions[2], "/user/hand/right/input/thumbrest/touch");

    // Suggest the bindings
    XrInteractionProfileSuggestedBinding suggestedBindings = {};
    suggestedBindings.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
    suggestedBindings.next = nullptr;
    suggestedBindings.interactionProfile = touchControllerProfile;
    suggestedBindings.suggestedBindings = bindings;
    suggestedBindings.countSuggestedBindings = bindingCount;

    // It's ok if this fails on runtimes that don't support touch controllers
    XrResult result = xrSuggestInteractionProfileBindings(instance, &suggestedBindings);
    if (XR_FAILED(result)) {
        ALOGW("Failed to suggest Touch controller bindings: %d", result);
    }
}

InputStateStatic::InputStateStatic(const XrInstance& instance, const XrSession& session) {
    // Create action set
    XrActionSetCreateInfo actionSetInfo = {XR_TYPE_ACTION_SET_CREATE_INFO};
    strncpy(actionSetInfo.actionSetName, "vrtemplate_controls", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    strncpy(actionSetInfo.localizedActionSetName, "VR Template Controls", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    actionSetInfo.priority = 1;
    OXR(xrCreateActionSet(instance, &actionSetInfo, &mActionSet));

    // Get hand subaction paths
    OXR(xrStringToPath(instance, "/user/hand/left", &mHandSubactionPaths[InputStateFrame::LEFT_CONTROLLER]));
    OXR(xrStringToPath(instance, "/user/hand/right", &mHandSubactionPaths[InputStateFrame::RIGHT_CONTROLLER]));

    // Create face button actions
    mButtonActions[0] = CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "a_button", "A Button");
    mButtonActions[1] = CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "b_button", "B Button");
    mButtonActions[2] = CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "x_button", "X Button");
    mButtonActions[3] = CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "y_button", "Y Button");
    mButtonActions[4] = CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu Button");

    // Create hand-specific actions with subaction paths
    // This allows the runtime to distinguish between left and right hands
    mHandPoseAction = CreateAction(
            XR_ACTION_TYPE_POSE_INPUT,
            "hand_pose",
            "Hand Pose",
            MAX_CONTROLLERS,
            mHandSubactionPaths);

    // Triggers and grips with subaction paths
    mTriggerActions[0] = CreateAction(
            XR_ACTION_TYPE_BOOLEAN_INPUT,
            "trigger",
            "Trigger",
            MAX_CONTROLLERS,
            mHandSubactionPaths);

    mTriggerActions[1] = CreateAction(
            XR_ACTION_TYPE_BOOLEAN_INPUT,
            "squeeze",
            "Grip",
            MAX_CONTROLLERS,
            mHandSubactionPaths);

    // Thumbstick actions with subaction paths
    mThumbstickActions[0] = CreateAction(
            XR_ACTION_TYPE_VECTOR2F_INPUT,
            "thumbstick",
            "Thumbstick",
            MAX_CONTROLLERS,
            mHandSubactionPaths);

    mThumbstickActions[1] = CreateAction(
            XR_ACTION_TYPE_BOOLEAN_INPUT,
            "thumbstick_click",
            "Thumbstick Click",
            MAX_CONTROLLERS,
            mHandSubactionPaths);

    mThumbstickActions[2] = CreateAction(
            XR_ACTION_TYPE_BOOLEAN_INPUT,
            "thumbrest_touch",
            "Thumbrest Touch",
            MAX_CONTROLLERS,
            mHandSubactionPaths);

    // Suggest bindings for the controllers
    CreateCommonBindings(instance);

    // Attach the action set to the session
    XrSessionActionSetsAttachInfo attachInfo = {};
    attachInfo.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    attachInfo.next = nullptr;
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &mActionSet;
    OXR(xrAttachSessionActionSets(session, &attachInfo));

    ALOGV("Input actions and bindings initialized");
}

InputStateStatic::~InputStateStatic() {
    // Destroy hand spaces first as they depend on pose actions
    for (int i = 0; i < MAX_CONTROLLERS; i++) {
        if (mHandSpaces[i] != XR_NULL_HANDLE) {
            xrDestroySpace(mHandSpaces[i]);
            mHandSpaces[i] = XR_NULL_HANDLE;
        }
    }

    // Destroy actions in reverse order of dependency
    // This isn't strictly necessary, but it's a good practice

    // Destroy thumb actions
    for (int i = 0; i < 3; i++) {
        if (mThumbstickActions[i] != XR_NULL_HANDLE) {
            xrDestroyAction(mThumbstickActions[i]);
            mThumbstickActions[i] = XR_NULL_HANDLE;
        }
    }

    // Destroy trigger actions
    for (int i = 0; i < 2; i++) {
        if (mTriggerActions[i] != XR_NULL_HANDLE) {
            xrDestroyAction(mTriggerActions[i]);
            mTriggerActions[i] = XR_NULL_HANDLE;
        }
    }

    // Destroy face button actions
    for (int i = 0; i < 5; i++) {
        if (mButtonActions[i] != XR_NULL_HANDLE) {
            xrDestroyAction(mButtonActions[i]);
            mButtonActions[i] = XR_NULL_HANDLE;
        }
    }

    // Destroy hand pose action
    if (mHandPoseAction != XR_NULL_HANDLE) {
        xrDestroyAction(mHandPoseAction);
        mHandPoseAction = XR_NULL_HANDLE;
    }

    // Finally destroy the action set
    if (mActionSet != XR_NULL_HANDLE) {
        xrDestroyActionSet(mActionSet);
        mActionSet = XR_NULL_HANDLE;
    }
}

//------------------------------------------------------------------------------
// InputStateFrame Implementation
//------------------------------------------------------------------------------

void InputStateFrame::SyncButtonsAndThumbSticks(
        const XrSession& session,
        const InputStateStatic& staticState) {

    assert(staticState.mActionSet != XR_NULL_HANDLE);

    // Sync actions with the runtime - all actions are updated at once
    XrActiveActionSet activeActionSet = {};
    activeActionSet.actionSet = staticState.mActionSet;
    activeActionSet.subactionPath = XR_NULL_PATH;  // Update all actions

    XrActionsSyncInfo syncInfo = {};
    syncInfo.type = XR_TYPE_ACTIONS_SYNC_INFO;
    syncInfo.next = nullptr;
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;

    OXR(xrSyncActions(session, &syncInfo));

    // Sync face button states
    mFaceButtonStates[0] = SyncButtonState(session, staticState.mButtonActions[0]);  // A
    mFaceButtonStates[1] = SyncButtonState(session, staticState.mButtonActions[1]);  // B
    mFaceButtonStates[2] = SyncButtonState(session, staticState.mButtonActions[2]);  // X
    mFaceButtonStates[3] = SyncButtonState(session, staticState.mButtonActions[3]);  // Y
    mMenuButtonState = SyncButtonState(session, staticState.mButtonActions[4]);      // Menu

    // Sync triggers
    for (unsigned int hand = 0; hand < NUM_CONTROLLERS; hand++) {
        // Get the appropriate subaction path for this hand
        const XrPath handPath = staticState.mHandSubactionPaths[hand];

        // Sync index trigger and squeeze trigger
        mIndexTriggerState[hand] = SyncButtonState(
                session,
                staticState.mTriggerActions[0],
                handPath);

        mSqueezeTriggerState[hand] = SyncButtonState(
                session,
                staticState.mTriggerActions[1],
                handPath);

        // Sync thumbstick and related inputs
        mThumbStickState[hand] = SyncVector2fState(
                session,
                staticState.mThumbstickActions[0],
                handPath);

        mThumbStickClickState[hand] = SyncButtonState(
                session,
                staticState.mThumbstickActions[1],
                handPath);

        mThumbrestTouchState[hand] = SyncButtonState(
                session,
                staticState.mThumbstickActions[2],
                handPath);
    }

    // Check if hand spaces need to be created
    for (unsigned int hand = 0; hand < NUM_CONTROLLERS; hand++) {
        if (staticState.mHandSpaces[hand] == XR_NULL_HANDLE) {
            // Note: this const_cast is necessary because the CreateActionSpace function
            // modifies the mHandSpaces array, but the staticState is passed as const
            // This is a rare case where const_cast is appropriate
            XrSpace* handSpaces = const_cast<XrSpace*>(staticState.mHandSpaces);

            handSpaces[hand] = CreateActionSpace(
                    session,
                    staticState.mHandPoseAction,
                    staticState.mHandSubactionPaths[hand]);
        }
    }

    // Update hand active states
    for (unsigned int hand = 0; hand < NUM_CONTROLLERS; hand++) {
        if (staticState.mHandSpaces[hand] != XR_NULL_HANDLE) {
            XrActionStateGetInfo getInfo = {};
            getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
            getInfo.next = nullptr;
            getInfo.action = staticState.mHandPoseAction;
            getInfo.subactionPath = staticState.mHandSubactionPaths[hand];

            XrActionStatePose poseState = {};
            poseState.type = XR_TYPE_ACTION_STATE_POSE;
            poseState.next = nullptr;

            OXR(xrGetActionStatePose(session, &getInfo, &poseState));
            mIsHandActive[hand] = poseState.isActive;
        }
    }
}

void InputStateFrame::SyncHandPoses(
        const InputStateStatic& staticState,
        const XrSpace& referenceSpace,
        const XrTime predictedDisplayTime) {

    // Get controller poses in one batch for better cache locality
    for (unsigned int hand = 0; hand < NUM_CONTROLLERS; hand++) {
        if (staticState.mHandSpaces[hand] != XR_NULL_HANDLE) {
            OXR(xrLocateSpace(
                    staticState.mHandSpaces[hand],
                    referenceSpace,
                    predictedDisplayTime,
                    &mHandPositions[hand]));
        }
    }

    // Update hand active state based on pose validity
    for (unsigned int hand = 0; hand < NUM_CONTROLLERS; hand++) {
        // Only consider a hand active if it has valid position data
        const bool positionValid = (mHandPositions[hand].locationFlags &
                                    XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;

        mIsHandActive[hand] = mIsHandActive[hand] && positionValid;
    }

    // Update preferred hand
    const bool leftActive = mIsHandActive[LEFT_CONTROLLER];
    const bool rightActive = mIsHandActive[RIGHT_CONTROLLER];

    // If only one controller is active, use that one
    if (leftActive && !rightActive) {
        mPreferredHand = LEFT_CONTROLLER;
    } else if (!leftActive && rightActive) {
        mPreferredHand = RIGHT_CONTROLLER;
    } else if (leftActive && rightActive) {
        // If both controllers are active, use whichever one
        // recently pressed the trigger
        if (mIndexTriggerState[LEFT_CONTROLLER].changedSinceLastSync &&
            mIndexTriggerState[LEFT_CONTROLLER].currentState == XR_TRUE) {
            mPreferredHand = LEFT_CONTROLLER;
        } else if (mIndexTriggerState[RIGHT_CONTROLLER].changedSinceLastSync &&
                   mIndexTriggerState[RIGHT_CONTROLLER].currentState == XR_TRUE) {
            mPreferredHand = RIGHT_CONTROLLER;
        }
        // Otherwise, keep using the currently preferred hand
    }
    // If neither is active, keep the current preference

    // Log controller state for debugging
    ALOG_INPUT_VERBOSE("Controller state: L=%s R=%s Preferred=%s",
                       leftActive ? "active" : "inactive",
                       rightActive ? "active" : "inactive",
                       mPreferredHand == LEFT_CONTROLLER ? "LEFT" : "RIGHT");
}

bool InputStateFrame::HasButtonChanges() const {
    // Check face buttons
    for (int i = 0; i < 4; i++) {
        if (mFaceButtonStates[i].changedSinceLastSync) {
            return true;
        }
    }

    if (mMenuButtonState.changedSinceLastSync) {
        return true;
    }

    // Check thumb and trigger states for both controllers
    for (unsigned int hand = 0; hand < NUM_CONTROLLERS; hand++) {
        if (mThumbStickClickState[hand].changedSinceLastSync ||
            mThumbrestTouchState[hand].changedSinceLastSync ||
            mIndexTriggerState[hand].changedSinceLastSync ||
            mSqueezeTriggerState[hand].changedSinceLastSync) {
            return true;
        }
    }

    return false;
}