/*******************************************************************************

Filename    :   MathUtils.h
Content     :   OpenXR math utility functions
Authors     :   Amanda Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.

*******************************************************************************/

#pragma once

#include <openxr/openxr.h>

#include <limits>

// Math constants
#ifndef MATH_PI
#define MATH_PI 3.14159265358979323846f
#endif

#ifndef MATH_DEG_TO_RAD
#define MATH_DEG_TO_RAD (MATH_PI / 180.0f)
#endif

#ifndef MATH_RAD_TO_DEG
#define MATH_RAD_TO_DEG (180.0f / MATH_PI)
#endif

#ifndef MATH_FLOAT_EPSILON
#define MATH_FLOAT_EPSILON 1.19209290e-07f
#endif

// Vector math operator overloads
constexpr XrVector2f operator+(const XrVector2f &a, const XrVector2f &b) {
    return XrVector2f{a.x + b.x, a.y + b.y};
}

constexpr XrVector2f operator-(const XrVector2f &a, const XrVector2f &b) {
    return XrVector2f{a.x - b.x, a.y - b.y};
}

constexpr XrVector2f operator*(const XrVector2f &a, float s) {
    return XrVector2f{a.x * s, a.y * s};
}

constexpr XrVector2f operator*(float s, const XrVector2f &a) {
    return XrVector2f{s * a.x, s * a.y};
}

constexpr XrVector3f operator+(const XrVector3f &a, const XrVector3f &b) {
    return XrVector3f{a.x + b.x, a.y + b.y, a.z + b.z};
}

constexpr XrVector3f operator-(const XrVector3f &a, const XrVector3f &b) {
    return XrVector3f{a.x - b.x, a.y - b.y, a.z - b.z};
}

constexpr XrVector3f operator*(const XrVector3f &a, float s) {
    return XrVector3f{a.x * s, a.y * s, a.z * s};
}

constexpr XrVector3f operator*(float s, const XrVector3f &a) {
    return XrVector3f{s * a.x, s * a.y, s * a.z};
}

inline XrQuaternionf operator*(const XrQuaternionf &a, const XrQuaternionf &b) {
    XrQuaternionf result;
    result.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    result.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    result.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    result.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    return result;
}

/**
 * MathUtils - Utility functions for handling XR math operations
 */
namespace MathUtils {

inline constexpr XrQuaternionf kIdentityQuat{0.f, 0.f, 0.f, 1.f};
inline constexpr XrPosef       kIdentityPose{kIdentityQuat, {0.f, 0.f, 0.f}};

    /**
     * Posef - operations for position and orientation transforms
     */
    class Posef {
    public:
        static constexpr XrPosef Identity() {
            return kIdentityPose;
        }
    };
} // namespace MathUtils
