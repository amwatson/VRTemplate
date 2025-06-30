/*******************************************************************************

Filename    :   Common.h

Content     :   Common utilities

Authors     :   Amanda Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.

*******************************************************************************/

#pragma once

#include <openxr/openxr.h>

#define BAIL_ON_COND(cond, errorStr, returnCode)                                                   \
    do {                                                                                           \
        if (cond) {                                                                                \
            ALOGE("ERROR (%s): %s", __FUNCTION__, errorStr);                                       \
            return (returnCode);                                                                   \
        }                                                                                          \
    } while (0)

#define BAIL_ON_ERR(fn, returnCode)                                                                \
    do {                                                                                           \
        const int32_t ret = fn;                                                                    \
        if (ret < 0) {                                                                             \
            ALOGE("ERROR (%s): %s() returned %d", __FUNCTION__, #fn, ret);                         \
            return (returnCode);                                                                   \
        }                                                                                          \
    } while (0)

union XrCompositionLayer {
    XrCompositionLayerQuad          mQuad;
    XrCompositionLayerCylinderKHR   mCylinder;
    XrCompositionLayerPassthroughFB mPassthrough;
    XrCompositionLayerProjection    mProjection = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
};

enum VertexAttributeLocation {
    VERTEX_ATTRIBUTE_LOCATION_POSITION,
    VERTEX_ATTRIBUTE_LOCATION_COLOR,
    VERTEX_ATTRIBUTE_LOCATION_UV,
    VERTEX_ATTRIBUTE_LOCATION_TRANSFORM
};