/*******************************************************************************

Filename    :   LogUtils.h

Content     :   Logging macros I define in every project

Authors     :   Amanda M. Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.

*******************************************************************************/
#pragma once

#include <android/log.h>

#ifndef LOG_TAG
#define LOG_TAG "VrTemplate"
#endif

#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
// For reasons unknown to the author, ANDROID_LOG_DEBUG does not automatically
// mute when not running a debug build. 
#if defined(NDEBUG)
#define ALOGD(...)
#else
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#endif
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)

#define FAIL(...)                                                                                  \
    do {                                                                                           \
        __android_log_print(ANDROID_LOG_FATAL, LOG_TAG, __VA_ARGS__);                              \
        abort();                                                                                   \
    } while (0)